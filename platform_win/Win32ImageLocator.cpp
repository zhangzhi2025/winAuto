#include "Win32ImageLocator.h"

#include <windows.h>

#include <algorithm>
#include <filesystem>

#include "ImageFile.h"
#include "ScreenCapture.h"
#include "core/GrayImage.h"
#include "core/PathSafety.h"
#include "core/TemplateMatch.h"

namespace wa::platform {

namespace fs = std::filesystem;

Result<const GrayImage*> Win32ImageLocator::templateFor(const ImageLocator& loc) {
    using R = Result<const GrayImage*>;
    Result<std::string> path = resolveTemplatePath(baseDir_, loc.templatePath);
    if (!path.ok()) return failAs<const GrayImage*>(path);

    std::error_code ec;
    const fs::path p = fs::u8path(path.value);
    const auto size = static_cast<long long>(fs::file_size(p, ec));
    const auto time = static_cast<long long>(fs::last_write_time(p, ec).time_since_epoch().count());
    const long long stamp = size * 1000003LL + time;

    auto it = cache_.find(path.value);
    if (it != cache_.end() && it->second.stamp == stamp) return R::success(&it->second.gray);

    Result<Bitmap> bmp = loadImage(path.value);
    if (!bmp.ok()) return failAs<const GrayImage*>(bmp);
    if (bmp.value.hasTransparency) {
        return R::failure(ErrCode::InvalidWorkflow,
                          "template \"" + loc.templatePath +
                              "\" has transparent pixels; use an opaque screenshot of the target");
    }
    Result<GrayImage> gray = grayFromBgra(bmp.value.bgra.data(), bmp.value.bgra.size(), bmp.value.w, bmp.value.h,
                                          bmp.value.w * 4);
    if (!gray.ok()) return failAs<const GrayImage*>(gray);
    Cached& c = cache_[path.value];
    c.stamp = stamp;
    c.gray = std::move(gray.value);
    return R::success(&c.gray);
}

Result<ImageHit> Win32ImageLocator::search(std::uint64_t windowHandle, const ImageLocator& loc,
                                           const CancelToken& token) {
    using R = Result<ImageHit>;
    HWND h = reinterpret_cast<HWND>(static_cast<UINT_PTR>(windowHandle));
    if (!IsWindow(h)) return R::failure(ErrCode::WindowNotFound, "target window is gone");
    if (IsIconic(h)) return R::failure(ErrCode::ElementNotFound, "window is minimized; activate it first");

    Result<const GrayImage*> tpl = templateFor(loc);
    if (!tpl.ok()) return failAs<ImageHit>(tpl);

    RECT cr{};
    if (!GetClientRect(h, &cr)) return R::failure(ErrCode::Internal, "GetClientRect failed");
    POINT origin{0, 0};
    if (!ClientToScreen(h, &origin)) return R::failure(ErrCode::Internal, "ClientToScreen failed");
    Rect area{static_cast<int>(origin.x), static_cast<int>(origin.y), static_cast<int>(cr.right - cr.left),
              static_cast<int>(cr.bottom - cr.top)};
    if (loc.searchRegion) {
        // 相对 client 区域左上角；与 client 区域求交，不允许跑到窗口之外
        const long long x0 = std::max<long long>(area.x, static_cast<long long>(area.x) + loc.searchRegion->x);
        const long long y0 = std::max<long long>(area.y, static_cast<long long>(area.y) + loc.searchRegion->y);
        const long long x1 = std::min<long long>(static_cast<long long>(area.x) + area.w,
                                                 static_cast<long long>(area.x) + loc.searchRegion->x + loc.searchRegion->w);
        const long long y1 = std::min<long long>(static_cast<long long>(area.y) + area.h,
                                                 static_cast<long long>(area.y) + loc.searchRegion->y + loc.searchRegion->h);
        if (x1 <= x0 || y1 <= y0) return R::failure(ErrCode::ElementNotFound, "searchRegion is outside the window");
        area = Rect{static_cast<int>(x0), static_cast<int>(y0), static_cast<int>(x1 - x0), static_cast<int>(y1 - y0)};
    }

    Rect actual;
    Result<Bitmap> shot = captureScreen(area, &actual);
    if (!shot.ok()) return failAs<ImageHit>(shot);
    Result<GrayImage> gray = grayFromBgra(shot.value.bgra.data(), shot.value.bgra.size(), shot.value.w,
                                          shot.value.h, shot.value.w * 4);
    if (!gray.ok()) return failAs<ImageHit>(gray);

    Result<TemplateMatch> m = findTemplate(gray.value, *tpl.value, loc.minConfidence, loc.ambiguity, token);
    if (!m.ok()) return failAs<ImageHit>(m);

    ImageHit hit;
    hit.screenRect = Rect{actual.x + m.value.rect.x, actual.y + m.value.rect.y, m.value.rect.w, m.value.rect.h};
    hit.score = m.value.score;

    // 命中点必须在目标窗口自己身上，不是被别的窗口盖住的同样图案
    const Point c = hit.screenRect.center();
    POINT pt{c.x, c.y};
    HWND top = WindowFromPoint(pt);
    HWND root = top ? GetAncestor(top, GA_ROOT) : nullptr;
    if (root != h) {
        return R::failure(ErrCode::ElementNotFound,
                          "the image was found on screen but that spot is covered by another window");
    }
    return R::success(hit);
}

Result<ElementRef> Win32ImageLocator::locate(std::uint64_t windowHandle, const ImageLocator& loc,
                                             const CancelToken& token) {
    Result<ImageHit> hit = search(windowHandle, loc, token);
    if (!hit.ok()) return failAs<ElementRef>(hit);
    ElementRef e;
    e.bounds = hit.value.screenRect;
    e.name = "image";
    e.controlType = "Image";
    return Result<ElementRef>::success(std::move(e));
}

}  // namespace wa::platform

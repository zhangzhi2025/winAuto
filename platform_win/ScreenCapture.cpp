#include "ScreenCapture.h"

#include <windows.h>

#include <algorithm>
#include <cstring>

#include "DpiHelper.h"

namespace wa::platform {

Result<Bitmap> captureScreen(Rect want, Rect* actual) {
    using R = Result<Bitmap>;
    if (!isPerMonitorV2Aware()) {
        return R::failure(ErrCode::Internal,
                          "process is not Per-Monitor V2 DPI aware (Windows 10 1703+ required); "
                          "screen coordinates would be wrong");
    }
    const Rect vs = virtualScreenRect();
    const long long x0 = std::max<long long>(want.x, vs.x);
    const long long y0 = std::max<long long>(want.y, vs.y);
    const long long x1 = std::min<long long>(static_cast<long long>(want.x) + want.w, static_cast<long long>(vs.x) + vs.w);
    const long long y1 = std::min<long long>(static_cast<long long>(want.y) + want.h, static_cast<long long>(vs.y) + vs.h);
    if (want.w <= 0 || want.h <= 0 || x1 <= x0 || y1 <= y0) {
        return R::failure(ErrCode::ElementNotFound, "the search area is outside the screen");
    }
    const int w = static_cast<int>(x1 - x0), h = static_cast<int>(y1 - y0);

    HDC screen = GetDC(nullptr);
    if (!screen) return R::failure(ErrCode::Internal, "GetDC failed");
    HDC mem = CreateCompatibleDC(screen);
    BITMAPINFO bi{};
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = w;
    bi.bmiHeader.biHeight = -h;  // 自上而下
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;
    void* bits = nullptr;
    HBITMAP bmp = mem ? CreateDIBSection(screen, &bi, DIB_RGB_COLORS, &bits, nullptr, 0) : nullptr;

    Result<Bitmap> out = R::failure(ErrCode::Internal, "screen capture failed");
    if (mem && bmp && bits) {
        HGDIOBJ old = SelectObject(mem, bmp);
        // CAPTUREBLT：把分层窗口（半透明/无边框窗口）也截进来
        if (BitBlt(mem, 0, 0, w, h, screen, static_cast<int>(x0), static_cast<int>(y0), SRCCOPY | CAPTUREBLT)) {
            Bitmap b;
            b.w = w;
            b.h = h;
            b.bgra.resize(static_cast<size_t>(w) * static_cast<size_t>(h) * 4);
            std::memcpy(b.bgra.data(), bits, b.bgra.size());
            for (size_t i = 3; i < b.bgra.size(); i += 4) b.bgra[i] = 255;
            if (actual) *actual = Rect{static_cast<int>(x0), static_cast<int>(y0), w, h};
            out = R::success(std::move(b));
        }
        SelectObject(mem, old);
    }
    if (bmp) DeleteObject(bmp);
    if (mem) DeleteDC(mem);
    ReleaseDC(nullptr, screen);
    return out;
}

}  // namespace wa::platform

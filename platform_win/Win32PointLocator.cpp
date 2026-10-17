#include "Win32PointLocator.h"

#include <windows.h>

#include "core/InputPlan.h"

namespace wa::platform {

namespace {
ElementRef pointRef(Point p, const char* name) {
    ElementRef e;
    e.bounds = Rect{p.x, p.y, 1, 1};  // 1x1，center() 恰为该点
    e.name = name;
    e.controlType = "Point";
    return e;
}
}  // namespace

Result<ElementRef> Win32PointLocator::locate(std::uint64_t windowHandle, const LeafLocator& locator,
                                             const CancelToken&) {
    using R = Result<ElementRef>;

    if (const auto* sp = std::get_if<ScreenPointLocator>(&locator)) {
        return R::success(pointRef(Point{sp->x, sp->y}, "screen_point"));
    }

    if (const auto* wp = std::get_if<WindowPointLocator>(&locator)) {
        HWND h = reinterpret_cast<HWND>(static_cast<UINT_PTR>(windowHandle));
        if (!IsWindow(h)) return R::failure(ErrCode::WindowNotFound, "target window is gone");
        if (IsIconic(h)) {
            return R::failure(ErrCode::ElementNotFound, "window is minimized; activate it first");
        }
        RECT cr{};
        if (!GetClientRect(h, &cr)) return R::failure(ErrCode::Internal, "GetClientRect failed");
        POINT origin{0, 0};
        if (!ClientToScreen(h, &origin)) return R::failure(ErrCode::Internal, "ClientToScreen failed");

        const Rect client{static_cast<int>(origin.x), static_cast<int>(origin.y),
                          static_cast<int>(cr.right - cr.left), static_cast<int>(cr.bottom - cr.top)};
        Result<Point> p = resolveWindowPoint(client, *wp);
        if (!p.ok()) return failAs<ElementRef>(p);
        return R::success(pointRef(p.value, "window_point"));
    }

    if (std::holds_alternative<UiaLocator>(locator)) {
        return R::failure(ErrCode::Unsupported, "UI Automation locator is not available yet (stage 3)");
    }
    return R::failure(ErrCode::Unsupported, "image locator is not available yet (stage 5)");
}

}  // namespace wa::platform

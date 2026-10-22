#include "WindowGrab.h"

#include <windows.h>

#include "ScreenCapture.h"

namespace wa::platform {

Result<Bitmap> grabWindowClient(std::uint64_t windowHandle) {
    using R = Result<Bitmap>;
    HWND h = reinterpret_cast<HWND>(static_cast<UINT_PTR>(windowHandle));
    if (!IsWindow(h)) return R::failure(ErrCode::WindowNotFound, "the window is gone");
    if (IsIconic(h)) return R::failure(ErrCode::ElementNotFound, "the window is minimized");
    RECT cr{};
    POINT origin{0, 0};
    if (!GetClientRect(h, &cr) || !ClientToScreen(h, &origin)) {
        return R::failure(ErrCode::Internal, "cannot read the window's client area");
    }
    const Rect want{static_cast<int>(origin.x), static_cast<int>(origin.y), static_cast<int>(cr.right - cr.left),
                    static_cast<int>(cr.bottom - cr.top)};
    Rect actual;
    Result<Bitmap> shot = captureScreen(want, &actual);
    if (!shot.ok()) return shot;
    if (actual.x != want.x || actual.y != want.y || actual.w != want.w || actual.h != want.h) {
        // 窗口有一部分在屏幕之外：截到的图坐标会和 client 区域错位，模板的 searchRegion 就不对了
        return R::failure(ErrCode::ElementNotFound,
                          "part of the window is outside the screen; move it fully onto the screen and try again");
    }
    return shot;
}

}  // namespace wa::platform

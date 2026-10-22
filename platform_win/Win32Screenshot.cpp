#include "Win32Screenshot.h"

#include <windows.h>

#include <dwmapi.h>

#include <filesystem>

#include "ImageFile.h"
#include "ScreenCapture.h"
#include "Win32Util.h"

namespace wa::platform {

namespace fs = std::filesystem;

Status Win32Screenshot::captureWindow(std::uint64_t windowHandle, const std::string& pngPath) {
    HWND h = reinterpret_cast<HWND>(static_cast<UINT_PTR>(windowHandle));
    if (!IsWindow(h)) return Status::failure(ErrCode::WindowNotFound, "the window is gone");
    if (IsIconic(h)) return Status::failure(ErrCode::ElementNotFound, "the window is minimized");

    // 优先用 DWM 的"可见边框"：GetWindowRect 在 Win10 上包含不可见的缩放边框，会多截一圈别的内容
    RECT rc{};
    if (FAILED(DwmGetWindowAttribute(h, DWMWA_EXTENDED_FRAME_BOUNDS, &rc, sizeof(rc)))) {
        if (!GetWindowRect(h, &rc)) return Status::failure(ErrCode::Internal, "GetWindowRect failed");
    }
    const Rect want{static_cast<int>(rc.left), static_cast<int>(rc.top), static_cast<int>(rc.right - rc.left),
                    static_cast<int>(rc.bottom - rc.top)};

    Rect actual;
    Result<Bitmap> shot = captureScreen(want, &actual);
    if (!shot.ok()) return failAs<Unit>(shot);

    std::error_code ec;
    const fs::path file = fs::u8path(pngPath);
    if (file.has_parent_path()) fs::create_directories(file.parent_path(), ec);
    if (ec) return Status::failure(ErrCode::Internal, "cannot create screenshot folder: " + ec.message());
    return savePng(pngPath, shot.value);
}

}  // namespace wa::platform

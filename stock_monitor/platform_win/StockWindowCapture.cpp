#include "StockWindowCapture.h"

#include <windows.h>

#include <algorithm>
#include <cstring>

namespace stock_monitor {
namespace {

BOOL CALLBACK enumerateWindow(HWND hwnd, LPARAM parameter) {
    if (!IsWindowVisible(hwnd) || IsIconic(hwnd)) return TRUE;
    DWORD processId = 0;
    GetWindowThreadProcessId(hwnd, &processId);
    if (processId == GetCurrentProcessId()) return TRUE;
    const int length = GetWindowTextLengthW(hwnd);
    if (length <= 0) return TRUE;
    std::wstring title(static_cast<std::size_t>(length) + 1U, L'\0');
    const int copied = GetWindowTextW(hwnd, &title[0], length + 1);
    if (copied <= 0) return TRUE;
    title.resize(static_cast<std::size_t>(copied));
    auto* windows = reinterpret_cast<std::vector<WindowEntry>*>(parameter);
    windows->push_back({static_cast<std::uint64_t>(reinterpret_cast<UINT_PTR>(hwnd)), std::move(title)});
    return TRUE;
}

bool fullyOnVirtualScreen(const RECT& rect) {
    const int left = GetSystemMetrics(SM_XVIRTUALSCREEN);
    const int top = GetSystemMetrics(SM_YVIRTUALSCREEN);
    const int right = left + GetSystemMetrics(SM_CXVIRTUALSCREEN);
    const int bottom = top + GetSystemMetrics(SM_CYVIRTUALSCREEN);
    return rect.left >= left && rect.top >= top && rect.right <= right && rect.bottom <= bottom;
}

bool perMonitorV2Aware() {
    using GetContextFn = DPI_AWARENESS_CONTEXT(WINAPI*)();
    using AreEqualFn = BOOL(WINAPI*)(DPI_AWARENESS_CONTEXT, DPI_AWARENESS_CONTEXT);
    const HMODULE user32 = GetModuleHandleW(L"user32.dll");
    const auto getContext = user32 ? reinterpret_cast<GetContextFn>(
                                        GetProcAddress(user32, "GetThreadDpiAwarenessContext"))
                                   : nullptr;
    const auto areEqual = user32 ? reinterpret_cast<AreEqualFn>(
                                      GetProcAddress(user32, "AreDpiAwarenessContextsEqual"))
                                 : nullptr;
    return getContext && areEqual &&
           areEqual(getContext(), DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2) != FALSE;
}

}  // namespace

std::vector<WindowEntry> listCapturableWindows() {
    std::vector<WindowEntry> windows;
    EnumWindows(enumerateWindow, reinterpret_cast<LPARAM>(&windows));
    return windows;
}

CaptureResult captureVisibleClientArea(std::uint64_t windowHandle) {
    if (!perMonitorV2Aware()) {
        return {false, {}, "Process is not Per-Monitor V2 DPI aware; physical capture coordinates are unsafe."};
    }
    const HWND hwnd = reinterpret_cast<HWND>(static_cast<UINT_PTR>(windowHandle));
    if (!IsWindow(hwnd)) return {false, {}, "The selected window no longer exists."};
    if (IsIconic(hwnd)) return {false, {}, "The selected window is minimized."};

    RECT client{};
    POINT origin{0, 0};
    if (!GetClientRect(hwnd, &client) || !ClientToScreen(hwnd, &origin)) {
        return {false, {}, "Could not read the selected window's client area."};
    }
    const int width = client.right - client.left;
    const int height = client.bottom - client.top;
    if (width <= 0 || height <= 0) return {false, {}, "The selected window has an empty client area."};

    RECT requested{origin.x, origin.y, origin.x + width, origin.y + height};
    if (!fullyOnVirtualScreen(requested)) {
        return {false, {}, "The selected window is partly outside the virtual screen."};
    }

    HDC screen = GetDC(nullptr);
    if (!screen) return {false, {}, "Could not access the desktop capture device."};
    HDC memory = CreateCompatibleDC(screen);
    BITMAPINFO info{};
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = width;
    info.bmiHeader.biHeight = -height;
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;
    void* bits = nullptr;
    HBITMAP bitmap = memory ? CreateDIBSection(screen, &info, DIB_RGB_COLORS, &bits, nullptr, 0) : nullptr;
    bool captured = false;
    if (memory && bitmap && bits) {
        HGDIOBJ previous = SelectObject(memory, bitmap);
        captured = BitBlt(memory, 0, 0, width, height, screen, origin.x, origin.y, SRCCOPY | CAPTUREBLT) != FALSE;
        SelectObject(memory, previous);
    }
    CaptureResult result;
    if (captured) {
        result.image.width = width;
        result.image.height = height;
        result.image.stride = static_cast<std::size_t>(width) * 4U;
        result.image.rgba.resize(result.image.stride * static_cast<std::size_t>(height));
        const auto* bgra = static_cast<const std::uint8_t*>(bits);
        for (std::size_t i = 0; i < result.image.rgba.size(); i += 4U) {
            result.image.rgba[i] = bgra[i + 2U];
            result.image.rgba[i + 1U] = bgra[i + 1U];
            result.image.rgba[i + 2U] = bgra[i];
            result.image.rgba[i + 3U] = 255;
        }
        result.success = true;
    } else {
        result.error = "Desktop capture failed. The window may be occluded or protected.";
    }
    if (bitmap) DeleteObject(bitmap);
    if (memory) DeleteDC(memory);
    ReleaseDC(nullptr, screen);
    return result;
}

}  // namespace stock_monitor

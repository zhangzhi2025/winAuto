#include "StockWindowCapture.h"

#include <windows.h>

#include <utility>

#include "RegionGeometry.h"

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
    if (user32 == nullptr) return false;
    const auto getContext =
        reinterpret_cast<GetContextFn>(GetProcAddress(user32, "GetThreadDpiAwarenessContext"));
    const auto areEqual =
        reinterpret_cast<AreEqualFn>(GetProcAddress(user32, "AreDpiAwarenessContextsEqual"));
    return getContext != nullptr && areEqual != nullptr &&
           areEqual(getContext(), DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2) != FALSE;
}

bool measureClient(HWND hwnd, Size& size, Point& origin) {
    RECT client{};
    POINT screenOrigin{0, 0};
    if (!GetClientRect(hwnd, &client) || !ClientToScreen(hwnd, &screenOrigin)) {
        return false;
    }
    size.width = client.right - client.left;
    size.height = client.bottom - client.top;
    origin.x = screenOrigin.x;
    origin.y = screenOrigin.y;
    return true;
}

ClientCaptureError copyClientArea(const Size& size, const Point& origin, Image& image) {
    HDC screen = GetDC(nullptr);
    if (screen == nullptr) {
        return ClientCaptureError::DeviceUnavailable;
    }
    HDC memory = CreateCompatibleDC(screen);
    BITMAPINFO info{};
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = size.width;
    info.bmiHeader.biHeight = -size.height;
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;
    void* bits = nullptr;
    HBITMAP bitmap =
        memory != nullptr ? CreateDIBSection(screen, &info, DIB_RGB_COLORS, &bits, nullptr, 0) : nullptr;
    ClientCaptureError error = ClientCaptureError::DeviceUnavailable;
    if (memory != nullptr && bitmap != nullptr && bits != nullptr) {
        HGDIOBJ previous = SelectObject(memory, bitmap);
        const BOOL copied =
            BitBlt(memory, 0, 0, size.width, size.height, screen, origin.x, origin.y,
                   SRCCOPY | CAPTUREBLT);
        SelectObject(memory, previous);
        error = copied != FALSE ? ClientCaptureError::None : ClientCaptureError::CopyFailed;
    }
    if (error == ClientCaptureError::None) {
        Image captured;
        captured.width = size.width;
        captured.height = size.height;
        captured.stride = static_cast<std::size_t>(size.width) * 4U;
        captured.rgba.resize(captured.stride * static_cast<std::size_t>(size.height));
        const auto* bgra = static_cast<const std::uint8_t*>(bits);
        for (std::size_t i = 0; i + 3U < captured.rgba.size(); i += 4U) {
            captured.rgba[i] = bgra[i + 2U];
            captured.rgba[i + 1U] = bgra[i + 1U];
            captured.rgba[i + 2U] = bgra[i];
            captured.rgba[i + 3U] = 255U;
        }
        image = std::move(captured);
    }
    if (bitmap != nullptr) DeleteObject(bitmap);
    if (memory != nullptr) DeleteDC(memory);
    ReleaseDC(nullptr, screen);
    return error;
}

const char* copyFailureMessage(ClientCaptureError error) {
    return error == ClientCaptureError::CopyFailed
               ? "Desktop capture failed. The window may be occluded or protected."
               : "Could not access the desktop capture device.";
}

ClientSnapshot snapshotFailure(ClientCaptureError error, std::string message) {
    ClientSnapshot snapshot;
    snapshot.error = error;
    snapshot.message = std::move(message);
    return snapshot;
}

RegionCrop cropFailure(ClientCaptureError error, std::string message) {
    RegionCrop crop;
    crop.error = error;
    crop.message = std::move(message);
    return crop;
}

ClientCaptureError mapGeometryError(GeometryError error) {
    switch (error) {
        case GeometryError::OutOfBounds:
            return ClientCaptureError::RegionOutOfBounds;
        case GeometryError::InvalidWindow:
        case GeometryError::ClientSizeMismatch:
            return ClientCaptureError::ClientSizeMismatch;
        default:
            return ClientCaptureError::RegionInvalid;
    }
}

}  // namespace

const char* toString(ClientCaptureError error) {
    switch (error) {
        case ClientCaptureError::None:
            return "none";
        case ClientCaptureError::NotPerMonitorV2:
            return "not-per-monitor-v2";
        case ClientCaptureError::WindowInvalid:
            return "window-invalid";
        case ClientCaptureError::WindowMinimized:
            return "window-minimized";
        case ClientCaptureError::ClientAreaEmpty:
            return "client-area-empty";
        case ClientCaptureError::OffScreen:
            return "off-screen";
        case ClientCaptureError::DeviceUnavailable:
            return "device-unavailable";
        case ClientCaptureError::CopyFailed:
            return "copy-failed";
        case ClientCaptureError::ClientSizeChanged:
            return "client-size-changed";
        case ClientCaptureError::ClientSizeMismatch:
            return "client-size-mismatch";
        case ClientCaptureError::RegionInvalid:
            return "region-invalid";
        case ClientCaptureError::RegionOutOfBounds:
            return "region-out-of-bounds";
    }
    return "unknown";
}

std::vector<WindowEntry> listCapturableWindows() {
    std::vector<WindowEntry> windows;
    EnumWindows(enumerateWindow, reinterpret_cast<LPARAM>(&windows));
    return windows;
}

ClientSnapshot captureClientSnapshot(std::uint64_t windowHandle) {
    if (!perMonitorV2Aware()) {
        return snapshotFailure(ClientCaptureError::NotPerMonitorV2,
                               "Process is not Per-Monitor V2 DPI aware; physical capture coordinates are unsafe.");
    }
    const HWND hwnd = reinterpret_cast<HWND>(static_cast<UINT_PTR>(windowHandle));
    if (!IsWindow(hwnd)) {
        return snapshotFailure(ClientCaptureError::WindowInvalid, "The selected window no longer exists.");
    }
    if (IsIconic(hwnd)) {
        return snapshotFailure(ClientCaptureError::WindowMinimized, "The selected window is minimized.");
    }

    Size before;
    Point origin;
    if (!measureClient(hwnd, before, origin)) {
        return snapshotFailure(ClientCaptureError::ClientAreaEmpty,
                               "Could not read the selected window's client area.");
    }
    if (before.width <= 0 || before.height <= 0) {
        return snapshotFailure(ClientCaptureError::ClientAreaEmpty,
                               "The selected window has an empty client area.");
    }
    const RECT requested{origin.x, origin.y, origin.x + before.width, origin.y + before.height};
    if (!fullyOnVirtualScreen(requested)) {
        return snapshotFailure(ClientCaptureError::OffScreen,
                               "The selected window is partly outside the virtual screen.");
    }

    Image image;
    const ClientCaptureError copyError = copyClientArea(before, origin, image);
    if (copyError != ClientCaptureError::None) {
        return snapshotFailure(copyError, copyFailureMessage(copyError));
    }

    Size after;
    Point afterOrigin;
    if (!measureClient(hwnd, after, afterOrigin) || after.width != before.width ||
        after.height != before.height) {
        return snapshotFailure(ClientCaptureError::ClientSizeChanged,
                               "The client area changed while it was captured; the capture was discarded.");
    }

    ClientSnapshot snapshot;
    snapshot.origin = origin;
    snapshot.clientSize = before;
    snapshot.image = std::move(image);
    return snapshot;
}

RegionCrop cropClientSnapshot(const ClientSnapshot& snapshot, const Rect& rect) {
    RegionCrop crop;
    crop.origin = snapshot.origin;
    crop.clientSize = snapshot.clientSize;
    crop.rect = rect;
    if (!snapshot.success()) {
        crop.error = snapshot.error;
        crop.message = snapshot.message;
        return crop;
    }
    const GeometryCheck rectCheck = checkCropRect(rect, snapshot.clientSize);
    if (!rectCheck.ok) {
        crop.error = mapGeometryError(rectCheck.error);
        crop.message = rectCheck.message;
        return crop;
    }
    const GeometryCheck cropped = cropRegion(snapshot.image, rect, crop.image);
    if (!cropped.ok) {
        crop.error = mapGeometryError(cropped.error);
        crop.message = cropped.message;
        return crop;
    }
    return crop;
}

RegionCrop captureClientRegion(std::uint64_t windowHandle,
                               const Rect& rect,
                               const Size& expectedClientSize) {
    const ClientSnapshot snapshot = captureClientSnapshot(windowHandle);
    if (!snapshot.success()) {
        RegionCrop crop = cropFailure(snapshot.error, snapshot.message);
        crop.origin = snapshot.origin;
        crop.clientSize = snapshot.clientSize;
        crop.rect = rect;
        return crop;
    }
    const GeometryCheck sizeCheck = checkClientSize(expectedClientSize, snapshot.clientSize);
    if (!sizeCheck.ok) {
        RegionCrop crop = cropFailure(ClientCaptureError::ClientSizeMismatch, sizeCheck.message);
        crop.origin = snapshot.origin;
        crop.clientSize = snapshot.clientSize;
        crop.rect = rect;
        return crop;
    }
    return cropClientSnapshot(snapshot, rect);
}

CaptureResult captureVisibleClientArea(std::uint64_t windowHandle) {
    ClientSnapshot snapshot = captureClientSnapshot(windowHandle);
    CaptureResult result;
    if (snapshot.success()) {
        result.success = true;
        result.image = std::move(snapshot.image);
    } else {
        result.error = snapshot.message.empty() ? std::string("Desktop capture failed.")
                                                : snapshot.message;
    }
    return result;
}

ClientCaptureError measureClientSize(std::uint64_t windowHandle,
                                     Size& clientSize,
                                     std::string& message) {
    clientSize = Size {};
    if (!perMonitorV2Aware()) {
        message = toString(ClientCaptureError::NotPerMonitorV2);
        return ClientCaptureError::NotPerMonitorV2;
    }
    const HWND hwnd = reinterpret_cast<HWND>(static_cast<UINT_PTR>(windowHandle));
    if (hwnd == nullptr || !IsWindow(hwnd)) {
        message = toString(ClientCaptureError::WindowInvalid);
        return ClientCaptureError::WindowInvalid;
    }
    if (IsIconic(hwnd)) {
        message = toString(ClientCaptureError::WindowMinimized);
        return ClientCaptureError::WindowMinimized;
    }
    Point origin;
    Size measured;
    if (!measureClient(hwnd, measured, origin)) {
        message = toString(ClientCaptureError::WindowInvalid);
        return ClientCaptureError::WindowInvalid;
    }
    if (measured.width <= 0 || measured.height <= 0) {
        message = toString(ClientCaptureError::ClientAreaEmpty);
        return ClientCaptureError::ClientAreaEmpty;
    }
    clientSize = measured;
    message.clear();
    return ClientCaptureError::None;
}

}  // namespace stock_monitor

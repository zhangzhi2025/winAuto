#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "RegionConfig.h"
#include "StockModel.h"

namespace stock_monitor {

struct WindowEntry {
    std::uint64_t handle = 0;
    std::wstring title;
};

struct CaptureResult {
    bool success = false;
    Image image;
    std::string error;
};

enum class ClientCaptureError {
    None,
    NotPerMonitorV2,
    WindowInvalid,
    WindowMinimized,
    ClientAreaEmpty,
    OffScreen,
    DeviceUnavailable,
    CopyFailed,
    ClientSizeChanged,
    ClientSizeMismatch,
    RegionInvalid,
    RegionOutOfBounds
};

const char* toString(ClientCaptureError error);

// Read-only snapshot of one window client area in physical pixels. The client
// size is measured before and after the screen copy; any change discards the
// capture so callers never receive pixels of an unknown size.
struct ClientSnapshot {
    ClientCaptureError error = ClientCaptureError::None;
    std::string message;
    Point origin;
    Size clientSize;
    Image image;

    bool success() const { return error == ClientCaptureError::None && image.valid(); }
};

// One region cropped out of a single immutable client snapshot.
struct RegionCrop {
    ClientCaptureError error = ClientCaptureError::None;
    std::string message;
    Point origin;
    Size clientSize;
    Rect rect;
    Image image;

    bool success() const { return error == ClientCaptureError::None && image.valid(); }
};

std::vector<WindowEntry> listCapturableWindows();
CaptureResult captureVisibleClientArea(std::uint64_t windowHandle);
ClientSnapshot captureClientSnapshot(std::uint64_t windowHandle);

// Crops one region from a fresh snapshot. The measured client size must match
// `expectedClientSize` exactly; coordinates are never rescaled.
RegionCrop captureClientRegion(std::uint64_t windowHandle,
                               const Rect& rect,
                               const Size& expectedClientSize);

// Crops from an already captured snapshot without touching the screen again.
RegionCrop cropClientSnapshot(const ClientSnapshot& snapshot, const Rect& rect);

// Reads the current client-area size in physical pixels without capturing any
// pixels. Used to confirm that a saved configuration still matches the live
// window before a session starts or a region is tested.
ClientCaptureError measureClientSize(std::uint64_t windowHandle,
                                     Size& clientSize,
                                     std::string& message);

}  // namespace stock_monitor

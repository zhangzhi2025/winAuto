#pragma once

#include <cstdint>
#include <string>
#include <vector>

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

std::vector<WindowEntry> listCapturableWindows();
CaptureResult captureVisibleClientArea(std::uint64_t windowHandle);

}  // namespace stock_monitor

#pragma once

#include <atomic>
#include <cstdint>
#include <string>

#include "RegionCaptureModel.h"
#include "RegionConfig.h"
#include "StockConfig.h"

namespace stock_monitor {

// Everything one region run needs. The request is copied into the worker, so the
// GUI may keep editing its own copy of the configuration.
struct RegionScanRequest {
    std::uint64_t windowHandle = 0;
    std::string sessionId;
    CaptureRegion region;
    // Client-area size stored in the configuration. The run fails instead of
    // rescaling the region rectangle when the measured size differs.
    Size expectedClientSize;
    FontColorConfig fontColorConfig;
    float lowConfidenceThreshold = kDefaultLowConfidenceThreshold;
};

// Captures one region and, unless OCR is unavailable, runs text recognition plus
// table interpretation. Safe to call from a worker thread: it never touches the
// GUI, and the captured image stays in memory only.
RegionCaptureResult scanRegion(const RegionScanRequest& request, const std::atomic_bool& cancelled);

// True when the build links the RapidOcrOnnx C++ adapter. Without it a region is
// still captured but never interpreted, so the UI can explain why.
bool ocrCompiledIn();

// Local wall clock time formatted as ISO-8601 with the UTC offset, for example
// 2024-05-06T09:31:05.123+08:00.
std::string localTimestamp();

}  // namespace stock_monitor

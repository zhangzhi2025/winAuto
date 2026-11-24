#include "RegionScan.h"

#include <chrono>
#include <cstdio>
#include <ctime>
#include <memory>
#include <mutex>
#include <utility>

#include "RegionTableParser.h"
#include "StockWindowCapture.h"
#ifdef STOCK_MONITOR_HAS_RAPIDOCR
#include "RapidOcrAdapter.h"
#endif

namespace stock_monitor {
namespace {

CaptureStatus toCaptureStatus(ClientCaptureError error) {
    switch (error) {
        case ClientCaptureError::None:
            return CaptureStatus::Success;
        case ClientCaptureError::NotPerMonitorV2:
        case ClientCaptureError::WindowInvalid:
        case ClientCaptureError::WindowMinimized:
        case ClientCaptureError::OffScreen:
        case ClientCaptureError::DeviceUnavailable:
            return CaptureStatus::WindowUnavailable;
        case ClientCaptureError::ClientSizeChanged:
        case ClientCaptureError::ClientSizeMismatch:
            return CaptureStatus::ClientSizeMismatch;
        case ClientCaptureError::ClientAreaEmpty:
        case ClientCaptureError::CopyFailed:
        case ClientCaptureError::RegionInvalid:
        case ClientCaptureError::RegionOutOfBounds:
            return CaptureStatus::CaptureFailed;
    }
    return CaptureStatus::CaptureFailed;
}

#ifdef STOCK_MONITOR_HAS_RAPIDOCR
// One OCR engine per process: initialisation is expensive and the adapter is not
// copyable, so every region run shares it and serialises engine calls.
struct OcrRuntime {
    std::mutex mutex;
    std::mutex engineMutex;
    std::shared_ptr<RapidOcrAdapter> provider;
    std::string initializationError;
};

OcrRuntime& ocrRuntime() {
    static OcrRuntime* instance = new OcrRuntime();
    return *instance;
}

std::shared_ptr<RapidOcrAdapter> acquireProvider(std::string& error) {
    OcrRuntime& state = ocrRuntime();
    std::lock_guard<std::mutex> guard(state.mutex);
    if (!state.provider) {
        auto created = std::make_shared<RapidOcrAdapter>();
        if (!created->ready()) {
            state.initializationError = created->initializationError();
            if (state.initializationError.empty()) {
                state.initializationError = "OCR 引擎初始化失败，未提供原因。";
            }
        } else {
            state.provider = std::move(created);
        }
    }
    if (!state.provider) {
        error = state.initializationError;
        return nullptr;
    }
    return state.provider;
}

void interpret(RegionCaptureResult& result, const RegionScanRequest& request) {
    TableParseOptions options;
    options.lowConfidenceThreshold = request.lowConfidenceThreshold;
    options.image = &result.image;
    options.fontColorConfig = &request.fontColorConfig;
    if (request.region.mode == CaptureRegionMode::Table) {
        result.rows = parseTableRows(request.region.table, result.imageSize, result.blocks, options);
    }
    // Plain OCR regions keep their blocks unchanged; font colors are reported per
    // table field only, because a free-form block has no declared type.
}

}  // namespace
#else
}  // namespace
#endif

bool ocrCompiledIn() {
#ifdef STOCK_MONITOR_HAS_RAPIDOCR
    return true;
#else
    return false;
#endif
}

std::string localTimestamp() {
    const auto now = std::chrono::system_clock::now();
    const auto millis =
        std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();
    const std::time_t seconds = static_cast<std::time_t>(millis / 1000);
    std::tm local {};
#ifdef _WIN32
    localtime_s(&local, &seconds);
    const std::time_t localAsUtc = _mkgmtime(&local);
#else
    localtime_r(&seconds, &local);
    const std::time_t localAsUtc = timegm(&local);
#endif
    long long offsetMinutes = 0;
    if (localAsUtc != static_cast<std::time_t>(-1)) {
        offsetMinutes = (static_cast<long long>(localAsUtc) - static_cast<long long>(seconds)) / 60;
    }
    const char sign = offsetMinutes < 0 ? '-' : '+';
    const long long absoluteOffset = offsetMinutes < 0 ? -offsetMinutes : offsetMinutes;

    char buffer[40] = {};
    std::snprintf(buffer, sizeof(buffer), "%04d-%02d-%02dT%02d:%02d:%02d.%03d%c%02lld:%02lld",
                  local.tm_year + 1900, local.tm_mon + 1, local.tm_mday, local.tm_hour, local.tm_min,
                  local.tm_sec, static_cast<int>(millis % 1000), sign, absoluteOffset / 60,
                  absoluteOffset % 60);
    return std::string(buffer);
}

RegionCaptureResult scanRegion(const RegionScanRequest& request, const std::atomic_bool& cancelled) {
    RegionCaptureResult result;
    result.sessionId = request.sessionId;
    result.regionId = request.region.id;
    result.capturedAt = localTimestamp();
    result.clientSize = request.expectedClientSize;
    result.ocrStatus = RegionOcrStatus::NotRun;

    if (cancelled.load()) {
        result.captureStatus = CaptureStatus::Cancelled;
        result.message = "会话已取消，未抓取该区域。";
        return result;
    }

    const RegionCrop crop =
        captureClientRegion(request.windowHandle, request.region.rect, request.expectedClientSize);
    if (!crop.success()) {
        result.captureStatus = toCaptureStatus(crop.error);
        result.message = crop.message.empty() ? toString(crop.error) : crop.message;
        return result;
    }

    result.captureStatus = CaptureStatus::Success;
    result.clientSize = crop.clientSize;
    result.imageSize = Size {crop.image.width, crop.image.height};
    result.hasImage = true;
    result.image = crop.image;

    if (cancelled.load()) {
        result.ocrStatus = RegionOcrStatus::Cancelled;
        result.message = "会话已取消，已放弃该区域的识别。";
        return result;
    }

#ifndef STOCK_MONITOR_HAS_RAPIDOCR
    result.message = "当前构建没有逐文字框 OCR 运行库，只完成了区域抓取。";
    return result;
#else
    std::string initializationError;
    const std::shared_ptr<RapidOcrAdapter> provider = acquireProvider(initializationError);
    if (!provider) {
        result.ocrStatus = RegionOcrStatus::Failed;
        result.message = "OCR 不可用：" + initializationError;
        return result;
    }

    OcrProviderResult recognized;
    {
        OcrRuntime& state = ocrRuntime();
        std::lock_guard<std::mutex> guard(state.engineMutex);
        recognized = provider->recognize(result.image, cancelled);
    }
    if (recognized.error == AnalysisError::Cancelled) {
        result.ocrStatus = RegionOcrStatus::Cancelled;
        result.message = "识别已取消。";
        return result;
    }
    if (recognized.error != AnalysisError::None) {
        result.ocrStatus = RegionOcrStatus::Failed;
        result.message = recognized.message.empty() ? toString(recognized.error) : recognized.message;
        return result;
    }

    result.ocrStatus = RegionOcrStatus::Success;
    result.blocks = std::move(recognized.blocks);
    interpret(result, request);
    if (result.blocks.empty()) {
        result.message = "识别成功，区域内没有文字。";
    }
    return result;
#endif
}

}  // namespace stock_monitor

#pragma once

#include <memory>
#include <mutex>
#include <string>

#include "core/Interfaces.h"
#include "platform_win/OcrRuntime.h"

namespace wa::platform {

class Win32OcrProvider : public IOcrProvider {
public:
    explicit Win32OcrProvider(std::shared_ptr<OcrRuntime> runtime) : runtime_(std::move(runtime)) {}
    explicit Win32OcrProvider(OcrModelPaths paths) : paths_(std::move(paths)) {}

    Result<OcrRecognition> recognize(const OcrRequest& request, const CancelToken& token) override;

private:
    std::shared_ptr<OcrRuntime> runtime_;
    OcrModelPaths paths_;
    std::string initializationError_;
    std::mutex initializationMutex_;
};

}  // namespace wa::platform

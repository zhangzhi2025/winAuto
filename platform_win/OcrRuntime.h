#pragma once

#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "core/Interfaces.h"
#include "platform_win/ScreenCapture.h"

namespace wa::platform {

struct OcrModelPaths {
    std::string detector;
    std::string classifier;
    std::string recognizer;
    std::string dictionary;
    std::string verificationImage;
    int threads = 2;
};

class OcrRuntime {
public:
    static Result<std::shared_ptr<OcrRuntime>> create(const OcrModelPaths& paths);
    ~OcrRuntime();
    Result<std::string> recognize(const Bitmap& image);

private:
    explicit OcrRuntime(void* handle);
    void* handle_ = nullptr;
    std::mutex mutex_;
};

OcrModelPaths defaultOcrModelPaths();

}  // namespace wa::platform

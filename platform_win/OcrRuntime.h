#pragma once

#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "core/Interfaces.h"

namespace cv {
class Mat;
}

class OcrLite;

namespace wa::platform {

struct OcrRuntimeText {
    std::string text;
    Rect bounds;
    double recognitionConfidence = 0.0;
    double detectionConfidence = 0.0;
};

struct OcrModelPaths {
    std::string detector;
    std::string classifier;
    std::string recognizer;
    std::string dictionary;
    int threads = 2;
};

class OcrRuntime {
public:
    static Result<std::shared_ptr<OcrRuntime>> create(const OcrModelPaths& paths);
    Result<std::vector<OcrRuntimeText>> recognize(const cv::Mat& image);

private:
    explicit OcrRuntime(std::unique_ptr<OcrLite> model);
    std::unique_ptr<OcrLite> model_;
    std::mutex mutex_;
};

OcrModelPaths defaultOcrModelPaths();

}  // namespace wa::platform

#include "OcrRuntime.h"

#include <algorithm>
#include <filesystem>
#include <stdexcept>

#include "OcrLite.h"
#include "OcrStruct.h"
#include "opencv2/core.hpp"
#include <windows.h>

namespace wa::platform {
namespace {

Status checkFile(const std::string& path, const char* label) {
    std::error_code error;
    if (path.empty() || !std::filesystem::is_regular_file(std::filesystem::u8path(path), error)) {
        return Status::failure(ErrCode::Unsupported,
                               std::string("OCR ") + label + " file is missing: " + path);
    }
    return okStatus();
}

}  // namespace

OcrRuntime::OcrRuntime(std::unique_ptr<OcrLite> model) : model_(std::move(model)) {}

Result<std::shared_ptr<OcrRuntime>> OcrRuntime::create(const OcrModelPaths& paths) {
    for (const auto& file : {std::pair<const std::string*, const char*>{&paths.detector, "detector"},
                             {&paths.classifier, "classifier"}, {&paths.recognizer, "recognizer"},
                             {&paths.dictionary, "dictionary"}}) {
        const Status status = checkFile(*file.first, file.second);
        if (!status.ok()) return Result<std::shared_ptr<OcrRuntime>>::failure(status.code, status.detail);
    }
    if (paths.threads < 1 || paths.threads > 32) {
        return Result<std::shared_ptr<OcrRuntime>>::failure(ErrCode::Unsupported,
                                                            "OCR thread count must be between 1 and 32");
    }

    try {
        auto model = std::make_unique<OcrLite>();
        model->setNumThread(paths.threads);
        if (!model->initModels(paths.detector, paths.classifier, paths.recognizer, paths.dictionary)) {
            return Result<std::shared_ptr<OcrRuntime>>::failure(ErrCode::Unsupported,
                                                                "RapidOcrOnnx model initialization failed");
        }
        auto runtime = std::shared_ptr<OcrRuntime>(new OcrRuntime(std::move(model)));
        const cv::Mat verificationImage(64, 256, CV_8UC3, cv::Scalar(255, 255, 255));
        Result<std::vector<OcrRuntimeText>> verified = runtime->recognize(verificationImage);
        if (!verified.ok()) {
            return Result<std::shared_ptr<OcrRuntime>>::failure(
                verified.code, "RapidOcrOnnx verification inference failed: " + verified.detail);
        }
        return Result<std::shared_ptr<OcrRuntime>>::success(std::move(runtime));
    } catch (const std::exception& e) {
        return Result<std::shared_ptr<OcrRuntime>>::failure(
            ErrCode::Unsupported, std::string("RapidOcrOnnx initialization failed: ") + e.what());
    }
}

Result<std::vector<OcrRuntimeText>> OcrRuntime::recognize(const cv::Mat& image) {
    if (image.empty()) {
        return Result<std::vector<OcrRuntimeText>>::failure(ErrCode::Unsupported, "OCR image is empty");
    }
    try {
        std::lock_guard<std::mutex> lock(mutex_);
        OcrResult result = model_->detect(image, 0, 960, 0.6f, 0.3f, 2.0f, true, true);
        std::vector<OcrRuntimeText> texts;
        texts.reserve(result.textBlocks.size());
        for (const TextBlock& block : result.textBlocks) {
            if (block.boxPoint.empty()) continue;
            int left = block.boxPoint.front().x;
            int top = block.boxPoint.front().y;
            int right = left;
            int bottom = top;
            for (const cv::Point& point : block.boxPoint) {
                left = (std::min)(left, point.x);
                top = (std::min)(top, point.y);
                right = (std::max)(right, point.x);
                bottom = (std::max)(bottom, point.y);
            }
            double confidence = block.boxScore;
            if (!block.charScores.empty()) {
                double sum = 0;
                for (const float score : block.charScores) sum += score;
                confidence = sum / static_cast<double>(block.charScores.size());
            }
            texts.push_back(OcrRuntimeText{block.text, Rect{left, top, right - left, bottom - top},
                                           confidence, block.boxScore});
        }
        return Result<std::vector<OcrRuntimeText>>::success(std::move(texts));
    } catch (const std::exception& e) {
        return Result<std::vector<OcrRuntimeText>>::failure(
            ErrCode::Unsupported, std::string("RapidOcrOnnx inference failed: ") + e.what());
    }
}

OcrModelPaths defaultOcrModelPaths() {
    wchar_t executablePath[32768]{};
    const DWORD pathLength = GetModuleFileNameW(nullptr, executablePath, static_cast<DWORD>(std::size(executablePath)));
    std::filesystem::path modelDirectory;
    if (pathLength > 0 && pathLength < std::size(executablePath)) {
        modelDirectory = std::filesystem::path(executablePath).parent_path() / L"models";
    }
    std::error_code error;
    if (modelDirectory.empty() || !std::filesystem::is_directory(modelDirectory, error)) {
#ifdef WINAUTO_OCR_MODEL_DIR
        modelDirectory = std::filesystem::u8path(WINAUTO_OCR_MODEL_DIR);
#else
        modelDirectory = std::filesystem::u8path("models");
#endif
    }
    const std::string directory = modelDirectory.u8string();
    OcrModelPaths paths;
    paths.detector = (std::filesystem::u8path(directory) / "ch_PP-OCRv3_det_infer.onnx").u8string();
    paths.classifier = (std::filesystem::u8path(directory) / "ch_ppocr_mobile_v2.0_cls_train.onnx").u8string();
    paths.recognizer = (std::filesystem::u8path(directory) / "ch_PP-OCRv3_rec_infer.onnx").u8string();
    paths.dictionary = (std::filesystem::u8path(directory) / "ppocr_keys_v1.txt").u8string();
    return paths;
}

}  // namespace wa::platform

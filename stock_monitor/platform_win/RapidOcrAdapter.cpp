#include "RapidOcrAdapter.h"

#include <algorithm>
#include <cmath>
#include <exception>
#include <filesystem>
#include <string>
#include <vector>

#include <windows.h>
#include <opencv2/core.hpp>

#include "OcrLite.h"

namespace stock_monitor {
namespace {

std::filesystem::path executableDirectory(std::string& error) {
    std::vector<wchar_t> path(512);
    for (;;) {
        SetLastError(ERROR_SUCCESS);
        const DWORD length = GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
        if (length == 0) {
            error = "Could not locate the running executable (Win32 error " +
                    std::to_string(GetLastError()) + ").";
            return {};
        }
        if (length < path.size() - 1U) {
            return std::filesystem::path(std::wstring(path.data(), length)).parent_path();
        }
        if (path.size() >= 32768U) {
            error = "The running executable path exceeds the supported Windows path length.";
            return {};
        }
        path.resize(std::min(path.size() * 2U, 32768U));
    }
}

bool hasRequiredModels(const std::filesystem::path& directory, std::string& missing) {
    const char* const filenames[] = {
        "ch_PP-OCRv3_det_infer.onnx",
        "ch_ppocr_mobile_v2.0_cls_train.onnx",
        "ch_PP-OCRv3_rec_infer.onnx",
        "ppocr_keys_v1.txt"
    };
    for (const char* filename : filenames) {
        const auto path = directory / filename;
        std::error_code error;
        if (!std::filesystem::is_regular_file(path, error)) {
            if (!missing.empty()) missing += ", ";
            missing += path.string();
        }
    }
    return missing.empty();
}

float averageRecognitionConfidence(const std::vector<float>& scores) {
    if (scores.empty()) return 0.0F;
    double sum = 0.0;
    for (const float score : scores) {
        if (!std::isfinite(score) || score < 0.0F || score > 1.0F) return -1.0F;
        sum += score;
    }
    return static_cast<float>(sum / scores.size());
}

}  // namespace

struct RapidOcrAdapter::Impl {
    OcrLite ocr;
    bool initialized = false;
    std::string error;
};

RapidOcrAdapter::RapidOcrAdapter() : impl_(std::make_unique<Impl>()) {
    const auto executableDir = executableDirectory(impl_->error);
    if (executableDir.empty()) return;
    const std::filesystem::path modelDir = executableDir / "models";
    std::string missing;
    if (!hasRequiredModels(modelDir, missing)) {
        impl_->error = "RapidOcrOnnx model files are missing beside the running executable. "
                       "Expected directory: " + modelDir.string() +
                       ". Missing or unreadable files: " + missing;
        return;
    }
    const auto det = modelDir / "ch_PP-OCRv3_det_infer.onnx";
    const auto cls = modelDir / "ch_ppocr_mobile_v2.0_cls_train.onnx";
    const auto rec = modelDir / "ch_PP-OCRv3_rec_infer.onnx";
    const auto keys = modelDir / "ppocr_keys_v1.txt";
    try {
        impl_->ocr.setNumThread(2);
        impl_->ocr.initLogger(false, false, false);
        impl_->initialized = impl_->ocr.initModels(det.string(), cls.string(), rec.string(), keys.string());
        if (!impl_->initialized) impl_->error = "RapidOcrOnnx did not initialize its models.";
    } catch (const std::exception& error) {
        impl_->error = std::string("RapidOcrOnnx model initialization failed: ") + error.what();
    } catch (...) {
        impl_->error = "RapidOcrOnnx model initialization failed with an unknown runtime error.";
    }
}

RapidOcrAdapter::~RapidOcrAdapter() = default;

bool RapidOcrAdapter::ready() const {
    return impl_ && impl_->initialized;
}

const std::string& RapidOcrAdapter::initializationError() const {
    static const std::string noImplError = "OCR adapter is not initialized.";
    return impl_ ? impl_->error : noImplError;
}

OcrProviderResult RapidOcrAdapter::recognize(const Image& image, const std::atomic_bool& cancelled) {
    if (cancelled.load()) return {AnalysisError::Cancelled, "analysis was cancelled", {}};
    if (!image.valid()) return {AnalysisError::InvalidImage, "input image buffer is invalid", {}};
    if (!ready()) return {AnalysisError::OcrUnavailable, initializationError(), {}};

    cv::Mat bgr(image.height, image.width, CV_8UC3);
    for (int y = 0; y < image.height; ++y) {
        const auto* source = image.rgba.data() + static_cast<std::size_t>(y) * image.stride;
        auto* destination = bgr.ptr<cv::Vec3b>(y);
        for (int x = 0; x < image.width; ++x) {
            const auto offset = static_cast<std::size_t>(x) * 4U;
            destination[x] = cv::Vec3b(source[offset + 2U], source[offset + 1U], source[offset]);
        }
    }

    try {
        OcrResult detected = impl_->ocr.detect(bgr, 50, 1024, 0.6F, 0.3F, 2.0F, true, true);
        OcrProviderResult result;
        result.blocks.reserve(detected.textBlocks.size());
        std::size_t order = 0;
        for (const auto& textBlock : detected.textBlocks) {
            if (textBlock.boxPoint.size() != 4 || !std::isfinite(textBlock.boxScore) ||
                textBlock.boxScore < 0.0F || textBlock.boxScore > 1.0F) {
                return {AnalysisError::InvalidOcrResult, "RapidOcrOnnx returned an invalid quadrilateral or box score.", {}};
            }
            int left = image.width - 1;
            int right = 0;
            int top = image.height - 1;
            int bottom = 0;
            std::array<Point, 4> quad{};
            for (std::size_t i = 0; i < textBlock.boxPoint.size(); ++i) {
                const auto& point = textBlock.boxPoint[i];
                const int x = std::clamp(point.x, 0, image.width - 1);
                const int y = std::clamp(point.y, 0, image.height - 1);
                quad[i] = {x, y};
                left = std::min(left, x);
                right = std::max(right, x);
                top = std::min(top, y);
                bottom = std::max(bottom, y);
            }
            const float recognition = averageRecognitionConfidence(textBlock.charScores);
            if (recognition < 0.0F) {
                return {AnalysisError::InvalidOcrResult, "RapidOcrOnnx returned invalid character confidence scores.", {}};
            }
            OcrTextBlock block;
            block.text = textBlock.text;
            block.bounds = {left, top, right - left + 1, bottom - top + 1};
            block.quad = quad;
            block.detectionConfidence = textBlock.boxScore;
            block.recognitionConfidence = recognition;
            block.order = order++;
            result.blocks.push_back(std::move(block));
        }
        return result;
    } catch (const std::exception& error) {
        return {AnalysisError::InferenceFailed,
                std::string("RapidOcrOnnx inference failed: ") + error.what(), {}};
    } catch (...) {
        return {AnalysisError::InferenceFailed, "RapidOcrOnnx inference failed with an unknown runtime error.", {}};
    }
}

}  // namespace stock_monitor

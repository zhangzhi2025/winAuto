#include "StockMonitorService.h"

#include <algorithm>
#include <cmath>

namespace stock_monitor {
namespace {

bool validBlock(const OcrTextBlock& block, const Image& image) {
    const long long right = static_cast<long long>(block.bounds.x) + block.bounds.width;
    const long long bottom = static_cast<long long>(block.bounds.y) + block.bounds.height;
    if (block.bounds.width <= 0 || block.bounds.height <= 0 || block.bounds.x < 0 || block.bounds.y < 0 ||
        right > image.width || bottom > image.height) {
        return false;
    }
    for (const auto& point : block.quad) {
        if (point.x < block.bounds.x || point.y < block.bounds.y ||
            point.x >= right || point.y >= bottom) {
            return false;
        }
    }
    return
           std::isfinite(block.detectionConfidence) && block.detectionConfidence >= 0.0F &&
           block.detectionConfidence <= 1.0F &&
           std::isfinite(block.recognitionConfidence) && block.recognitionConfidence >= 0.0F &&
           block.recognitionConfidence <= 1.0F;
}

}  // namespace

AnalysisResult analyze(const Image& image,
                       const std::vector<OcrTextBlock>& blocks,
                       const StockMonitorConfig& config) {
    AnalysisResult result;
    if (!image.valid()) {
        result.error = AnalysisError::InvalidImage;
        result.message = "image dimensions, stride, or pixel buffer are invalid";
        return result;
    }
    const auto configStatus = validate(config);
    if (!configStatus.valid) {
        result.error = AnalysisError::InvalidConfiguration;
        result.message = configStatus.path + ": " + configStatus.message;
        return result;
    }
    if (!std::all_of(blocks.begin(), blocks.end(), [&](const OcrTextBlock& block) {
            return validBlock(block, image);
        })) {
        result.error = AnalysisError::InvalidOcrResult;
        result.message = "OCR provider returned invalid bounds or confidence";
        return result;
    }
    result.blocks = blocks;
    auto assembly = assembleRows(blocks, image.width, config.tableLayout);
    result.rows = std::move(assembly.rows);
    result.unassignedBlocks = std::move(assembly.unassignedBlocks);
    for (auto& row : result.rows) {
        if (row.status == RowStatus::Ambiguous) {
            row.classification.status = RowStatus::Ambiguous;
            continue;
        }
        if (row.nameBlocks.size() == 1) {
            row.nameColor = analyzeFontColor(image, row.nameBlocks.front().bounds, config.fontColor);
        }
        if (row.codeBlocks.size() == 1) {
            row.codeColor = analyzeFontColor(image, row.codeBlocks.front().bounds, config.fontColor);
        }
        const bool reliableCodeColor = row.codeColor.valid &&
            row.codeColor.color != StockFontColor::Unknown &&
            row.codeColor.confidence >= config.fontColor.minConfidence;
        row.classification = classifyColors(row.nameColor, row.codeColor, reliableCodeColor);
        if (row.classification.status == RowStatus::ColorConflict) {
            row.status = RowStatus::ColorConflict;
        } else if (row.status == RowStatus::Ready &&
                   row.classification.status == RowStatus::NeedsReview) {
            row.status = RowStatus::NeedsReview;
        }
    }
    return result;
}

AnalysisResult analyzeWithProvider(const Image& image,
                                  const StockMonitorConfig& config,
                                  OcrProvider& provider,
                                  const std::atomic_bool& cancelled) {
    if (cancelled.load()) {
        AnalysisResult result;
        result.error = AnalysisError::Cancelled;
        result.message = "analysis was cancelled";
        return result;
    }
    if (!image.valid()) {
        AnalysisResult result;
        result.error = AnalysisError::InvalidImage;
        result.message = "image dimensions, stride, or pixel buffer are invalid";
        return result;
    }
    OcrProviderResult ocr = provider.recognize(image, cancelled);
    if (cancelled.load() || ocr.error == AnalysisError::Cancelled) {
        AnalysisResult result;
        result.error = AnalysisError::Cancelled;
        result.message = ocr.message.empty() ? "analysis was cancelled" : std::move(ocr.message);
        return result;
    }
    if (ocr.error != AnalysisError::None) {
        AnalysisResult result;
        result.error = ocr.error;
        result.message = std::move(ocr.message);
        return result;
    }
    return analyze(image, ocr.blocks, config);
}

}  // namespace stock_monitor

#include "testing.h"

#include <atomic>

#include "StockMonitorService.h"
#include "StockMonitorTestUtil.h"

using namespace stock_monitor;

namespace {
class FakeOcr final : public OcrProvider {
public:
    OcrProviderResult result;
    int calls = 0;
    OcrProviderResult recognize(const Image&, const std::atomic_bool&) override {
        ++calls;
        return result;
    }
};

OcrTextBlock block(const char* text, Rect bounds, float confidence = 0.95F) {
    OcrTextBlock result;
    result.text = text;
    result.bounds = bounds;
    result.quad = {{{bounds.x, bounds.y},
                    {bounds.x + bounds.width - 1, bounds.y},
                    {bounds.x + bounds.width - 1, bounds.y + bounds.height - 1},
                    {bounds.x, bounds.y + bounds.height - 1}}};
    result.detectionConfidence = confidence;
    result.recognitionConfidence = confidence;
    return result;
}
}  // namespace

TEST(stock_monitor_service_distinguishes_empty_ocr_from_failure) {
    Image image = makeStockImage(20, 20);
    FakeOcr provider;
    std::atomic_bool cancelled{false};
    auto result = analyzeWithProvider(image, StockMonitorConfig{}, provider, cancelled);
    CHECK_EQ(result.error, AnalysisError::None);
    CHECK(result.rows.empty());
    CHECK_EQ(provider.calls, 1);

    provider.result.error = AnalysisError::OcrUnavailable;
    provider.result.message = "native OCR dependencies unavailable";
    result = analyzeWithProvider(image, StockMonitorConfig{}, provider, cancelled);
    CHECK_EQ(result.error, AnalysisError::OcrUnavailable);
    CHECK_CONTAINS(result.message, "native OCR");
}

TEST(stock_monitor_service_rejects_invalid_image_and_ocr_geometry) {
    Image invalid;
    FakeOcr provider;
    std::atomic_bool cancelled{false};
    CHECK_EQ(analyzeWithProvider(invalid, StockMonitorConfig{}, provider, cancelled).error,
             AnalysisError::InvalidImage);
    CHECK_EQ(provider.calls, 0);

    Image image = makeStockImage(20, 20);
    provider.result.blocks = {block("bad", {19, 19, 2, 2})};
    CHECK_EQ(analyzeWithProvider(image, StockMonitorConfig{}, provider, cancelled).error,
             AnalysisError::InvalidOcrResult);
}

TEST(stock_monitor_service_honors_cancel_before_inference) {
    Image image = makeStockImage(20, 20);
    FakeOcr provider;
    std::atomic_bool cancelled{true};
    const auto result = analyzeWithProvider(image, StockMonitorConfig{}, provider, cancelled);
    CHECK_EQ(result.error, AnalysisError::Cancelled);
    CHECK_EQ(provider.calls, 0);
}

TEST(stock_monitor_service_uses_name_pixels_and_reports_code_name_conflict) {
    Image image = makeStockImage(100, 30);
    paintStockRect(image, {30, 10, 20, 10}, 255, 0, 0);
    paintStockRect(image, {80, 10, 12, 10}, 255, 0, 0);
    const std::vector<OcrTextBlock> blocks = {
        block("600001", {5, 10, 15, 10}),
        block("名称", {30, 10, 20, 10}),
        block("涨幅", {80, 10, 12, 10}),
    };
    auto result = analyze(image, blocks, StockMonitorConfig{});
    CHECK_EQ(result.error, AnalysisError::None);
    CHECK_EQ(result.rows.size(), static_cast<std::size_t>(1));
    CHECK_EQ(result.rows[0].nameColor.color, StockFontColor::Red);
    CHECK_EQ(result.rows[0].classification.boardStage, BoardStage::ThirdBoardPlus);
    CHECK_EQ(result.rows[0].classification.followStatus, FollowStatus::Unknown);
    CHECK_EQ(result.rows[0].status, RowStatus::Ready);

    paintStockRect(image, {5, 10, 15, 10}, 240, 120, 0);
    result = analyze(image, blocks, StockMonitorConfig{});
    CHECK_EQ(result.rows[0].status, RowStatus::ColorConflict);
    CHECK_EQ(result.rows[0].nameColor.color, StockFontColor::Red);
    CHECK_EQ(result.rows[0].codeColor.color, StockFontColor::Orange);
}

#include "testing.h"

#include "FontColorAnalyzer.h"
#include "StockMonitorTestUtil.h"

using namespace stock_monitor;

TEST(font_color_analyzer_recognizes_five_palette_colors) {
    struct Sample {
        std::uint8_t r, g, b;
        StockFontColor expected;
    };
    const Sample samples[] = {
        {255, 0, 0, StockFontColor::Red},
        {240, 120, 0, StockFontColor::Orange},
        {255, 255, 0, StockFontColor::Yellow},
        {255, 0, 255, StockFontColor::Magenta},
        {235, 235, 235, StockFontColor::White},
    };
    for (const auto& sample : samples) {
        Image image = makeStockImage(8, 8, sample.r, sample.g, sample.b);
        const auto result = analyzeFontColor(image, {0, 0, 8, 8}, FontColorConfig{});
        CHECK(result.valid);
        CHECK_EQ(result.color, sample.expected);
        CHECK_EQ(result.foregroundPixelCount, static_cast<std::size_t>(64));
        CHECK(result.confidence >= 0.99F);
    }
}

TEST(font_color_analyzer_rejects_gray_small_and_mixed_regions) {
    const FontColorConfig config;
    Image gray = makeStockImage(8, 8, 100, 100, 100);
    CHECK_EQ(analyzeFontColor(gray, {0, 0, 8, 8}, config).color, StockFontColor::Unknown);

    Image small = makeStockImage(2, 2, 255, 0, 0);
    const auto smallResult = analyzeFontColor(small, {0, 0, 2, 2}, config);
    CHECK(smallResult.valid);
    CHECK_EQ(smallResult.color, StockFontColor::Unknown);

    Image mixed = makeStockImage(8, 8, 255, 0, 0);
    paintStockRect(mixed, {4, 0, 4, 8}, 240, 120, 0);
    const auto mixedResult = analyzeFontColor(mixed, {0, 0, 8, 8}, config);
    CHECK(mixedResult.valid);
    CHECK(mixedResult.dominantRatio < config.minDominantRatio);
    CHECK_EQ(mixedResult.color, StockFontColor::Unknown);
}

TEST(font_color_analyzer_clips_bounds_and_reports_invalid_regions) {
    Image image = makeStockImage(8, 8, 255, 0, 0);
    const auto clipped = analyzeFontColor(image, {-2, 0, 8, 8}, FontColorConfig{});
    CHECK(clipped.valid);
    CHECK(clipped.clipped);
    CHECK_EQ(clipped.foregroundPixelCount, static_cast<std::size_t>(48));
    CHECK(!analyzeFontColor(image, {10, 10, 2, 2}, FontColorConfig{}).valid);
}

TEST(font_color_analyzer_processes_regions_as_a_batch) {
    Image image = makeStockImage(16, 8);
    paintStockRect(image, {0, 0, 8, 8}, 255, 0, 0);
    paintStockRect(image, {8, 0, 8, 8}, 255, 255, 0);
    const auto results = analyzeFontColors(image, {{0, 0, 8, 8}, {8, 0, 8, 8}}, FontColorConfig{});
    CHECK_EQ(results.size(), static_cast<std::size_t>(2));
    CHECK_EQ(results[0].color, StockFontColor::Red);
    CHECK_EQ(results[1].color, StockFontColor::Yellow);
}

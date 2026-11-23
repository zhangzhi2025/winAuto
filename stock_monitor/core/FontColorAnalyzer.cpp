#include "FontColorAnalyzer.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

namespace stock_monitor {
namespace {

struct Hsv {
    float h;
    float s;
    float v;
};

Hsv toHsv(int r, int g, int b) {
    const float rf = r / 255.0F;
    const float gf = g / 255.0F;
    const float bf = b / 255.0F;
    const float maxValue = std::max({rf, gf, bf});
    const float minValue = std::min({rf, gf, bf});
    const float delta = maxValue - minValue;
    float hue = 0.0F;
    if (delta > 0.0F) {
        if (maxValue == rf) {
            hue = 60.0F * std::fmod((gf - bf) / delta, 6.0F);
        } else if (maxValue == gf) {
            hue = 60.0F * (((bf - rf) / delta) + 2.0F);
        } else {
            hue = 60.0F * (((rf - gf) / delta) + 4.0F);
        }
    }
    if (hue < 0.0F) hue += 360.0F;
    return {hue, maxValue == 0.0F ? 0.0F : delta / maxValue * 255.0F, maxValue * 255.0F};
}

bool inRule(const Hsv& hsv, const ColorRule& rule) {
    if (hsv.s < rule.minSaturation || hsv.v < rule.minValue) return false;
    return std::any_of(rule.hueRanges.begin(), rule.hueRanges.end(), [&](const HueRange& range) {
        return hsv.h >= range.min && hsv.h < range.max;
    });
}

StockFontColor colorAt(std::size_t index) {
    switch (index) {
        case 0: return StockFontColor::Red;
        case 1: return StockFontColor::White;
        case 2: return StockFontColor::Orange;
        case 3: return StockFontColor::Yellow;
        case 4: return StockFontColor::Magenta;
        default: return StockFontColor::Unknown;
    }
}

}  // namespace

bool Image::valid() const {
    if (width <= 0 || height <= 0) return false;
    if (static_cast<std::size_t>(width) > static_cast<std::size_t>(-1) / 4U) return false;
    const std::size_t rowBytes = static_cast<std::size_t>(width) * 4U;
    if (stride < rowBytes || static_cast<std::size_t>(height) >
                                 (static_cast<std::size_t>(-1) / stride)) {
        return false;
    }
    return rgba.size() >= stride * static_cast<std::size_t>(height);
}

FontColorResult analyzeFontColor(const Image& image, const Rect& bounds, const FontColorConfig& config) {
    FontColorResult result;
    result.sourceBounds = bounds;
    if (!image.valid() || bounds.empty() || !config.enabled || !validate(config).valid) return result;

    const long long requestedRight = static_cast<long long>(bounds.x) + bounds.width;
    const long long requestedBottom = static_cast<long long>(bounds.y) + bounds.height;
    const int left = static_cast<int>(std::clamp<long long>(bounds.x, 0, image.width));
    const int top = static_cast<int>(std::clamp<long long>(bounds.y, 0, image.height));
    const int right = static_cast<int>(std::clamp<long long>(requestedRight, 0, image.width));
    const int bottom = static_cast<int>(std::clamp<long long>(requestedBottom, 0, image.height));
    if (right <= left || bottom <= top) return result;
    result.clipped = left != bounds.x || top != bounds.y ||
                     right != requestedRight || bottom != requestedBottom;

    struct Accumulator {
        std::size_t count = 0;
        std::uint64_t r = 0;
        std::uint64_t g = 0;
        std::uint64_t b = 0;
        double h = 0;
        double s = 0;
        double v = 0;
    };
    std::array<Accumulator, 5> colors{};

    for (int y = top; y < bottom; ++y) {
        const auto rowOffset = static_cast<std::size_t>(y) * image.stride;
        for (int x = left; x < right; ++x) {
            const auto offset = rowOffset + static_cast<std::size_t>(x) * 4U;
            const int r = image.rgba[offset];
            const int g = image.rgba[offset + 1U];
            const int b = image.rgba[offset + 2U];
            const Hsv hsv = toHsv(r, g, b);
            std::size_t index = 0;
            if (hsv.s <= config.whiteMaxSaturation && hsv.v >= config.whiteMinBrightness) {
                index = 1;
            } else if (hsv.v >= config.minBrightness) {
                if (inRule(hsv, config.red)) index = 0;
                else if (inRule(hsv, config.orange)) index = 2;
                else if (inRule(hsv, config.yellow)) index = 3;
                else if (inRule(hsv, config.magenta)) index = 4;
                else continue;
            } else {
                continue;
            }
            auto& count = colors[index];
            ++count.count;
            count.r += static_cast<std::uint64_t>(r);
            count.g += static_cast<std::uint64_t>(g);
            count.b += static_cast<std::uint64_t>(b);
            count.h += hsv.h;
            count.s += hsv.s;
            count.v += hsv.v;
        }
    }

    const auto dominant = std::max_element(colors.begin(), colors.end(),
        [](const Accumulator& a, const Accumulator& b) { return a.count < b.count; });
    result.foregroundPixelCount = 0;
    for (const auto& color : colors) result.foregroundPixelCount += color.count;
    result.dominantPixelCount = dominant->count;
    if (result.foregroundPixelCount == 0 || dominant->count == 0) return result;

    result.valid = true;
    result.dominantRatio = static_cast<float>(dominant->count) /
                           static_cast<float>(result.foregroundPixelCount);
    const float sampleFactor = std::min(1.0F, static_cast<float>(result.foregroundPixelCount) /
                                                static_cast<float>(config.minForegroundPixels));
    result.confidence = result.dominantRatio * sampleFactor;
    result.dominantRgb = {
        static_cast<int>(dominant->r / dominant->count),
        static_cast<int>(dominant->g / dominant->count),
        static_cast<int>(dominant->b / dominant->count)};
    result.averageHue = static_cast<float>(dominant->h / dominant->count);
    result.averageSaturation = static_cast<float>(dominant->s / dominant->count);
    result.averageValue = static_cast<float>(dominant->v / dominant->count);
    if (result.foregroundPixelCount < static_cast<std::size_t>(config.minForegroundPixels) ||
        result.dominantRatio < config.minDominantRatio || result.confidence < config.minConfidence) {
        return result;
    }
    result.color = colorAt(static_cast<std::size_t>(std::distance(colors.begin(), dominant)));
    return result;
}

std::vector<FontColorResult> analyzeFontColors(const Image& image,
                                              const std::vector<Rect>& bounds,
                                              const FontColorConfig& config) {
    std::vector<FontColorResult> results;
    results.reserve(bounds.size());
    for (const auto& region : bounds) results.push_back(analyzeFontColor(image, region, config));
    return results;
}

const char* toString(StockFontColor color) {
    switch (color) {
        case StockFontColor::Red: return "red";
        case StockFontColor::White: return "white";
        case StockFontColor::Orange: return "orange";
        case StockFontColor::Yellow: return "yellow";
        case StockFontColor::Magenta: return "magenta";
        default: return "unknown";
    }
}

const char* toString(BoardStage stage) {
    switch (stage) {
        case BoardStage::FirstBoard: return "BOARD_1";
        case BoardStage::SecondBoard: return "BOARD_2";
        case BoardStage::ThirdBoardPlus: return "BOARD_3_PLUS";
        default: return "UNKNOWN";
    }
}

const char* toString(FollowStatus status) {
    switch (status) {
        case FollowStatus::Watchlist: return "WATCHLIST";
        case FollowStatus::NotFollowed: return "NOT_FOLLOWED";
        default: return "UNKNOWN";
    }
}

const char* toString(RowStatus status) {
    switch (status) {
        case RowStatus::Ready: return "ready";
        case RowStatus::Incomplete: return "incomplete";
        case RowStatus::Ambiguous: return "ambiguous";
        case RowStatus::ColorConflict: return "color_conflict";
        default: return "needs_review";
    }
}

const char* toString(AnalysisError error) {
    switch (error) {
        case AnalysisError::InvalidImage: return "invalid_image";
        case AnalysisError::InvalidConfiguration: return "invalid_configuration";
        case AnalysisError::InvalidOcrResult: return "invalid_ocr_result";
        case AnalysisError::OcrUnavailable: return "ocr_unavailable";
        case AnalysisError::Cancelled: return "cancelled";
        case AnalysisError::InferenceFailed: return "inference_failed";
        default: return "none";
    }
}

}  // namespace stock_monitor

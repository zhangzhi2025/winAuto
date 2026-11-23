#include "StockConfig.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace stock_monitor {
namespace {

ConfigValidation failure(std::string path, std::string message) {
    return {false, std::move(path), std::move(message)};
}

bool validRule(const ColorRule& rule) {
    if (rule.hueRanges.empty() || rule.minSaturation < 0 || rule.minSaturation > 255 ||
        rule.minValue < 0 || rule.minValue > 255) {
        return false;
    }
    for (const auto& range : rule.hueRanges) {
        if (!std::isfinite(range.min) || !std::isfinite(range.max) ||
            range.min < 0.0F || range.max > 360.0F || range.min >= range.max) {
            return false;
        }
    }
    return true;
}

bool overlaps(const HueRange& a, const HueRange& b) {
    return a.min < b.max && b.min < a.max;
}

}  // namespace

ConfigValidation validate(const FontColorConfig& config) {
    if (config.version != "1") return failure("fontColor.version", "unsupported version");
    if (config.minForegroundPixels < 1) return failure("fontColor.minForegroundPixels", "must be positive");
    if (!std::isfinite(config.minDominantRatio) || config.minDominantRatio < 0.0F ||
        config.minDominantRatio > 1.0F) {
        return failure("fontColor.minDominantRatio", "must be between 0 and 1");
    }
    if (!std::isfinite(config.minConfidence) || config.minConfidence < 0.0F ||
        config.minConfidence > 1.0F) {
        return failure("fontColor.minConfidence", "must be between 0 and 1");
    }
    if (config.minBrightness < 0 || config.minBrightness > 255 ||
        config.whiteMinBrightness < 0 || config.whiteMinBrightness > 255 ||
        config.whiteMaxSaturation < 0 || config.whiteMaxSaturation > 255) {
        return failure("fontColor.brightness", "brightness and saturation values must be between 0 and 255");
    }
    const ColorRule* rules[] = {&config.red, &config.orange, &config.yellow, &config.magenta};
    const char* names[] = {"red", "orange", "yellow", "magenta"};
    for (std::size_t i = 0; i < 4; ++i) {
        if (!validRule(*rules[i])) return failure(std::string("fontColor.") + names[i], "invalid HSV rule");
        for (std::size_t first = 0; first < rules[i]->hueRanges.size(); ++first) {
            for (std::size_t second = first + 1; second < rules[i]->hueRanges.size(); ++second) {
                if (overlaps(rules[i]->hueRanges[first], rules[i]->hueRanges[second])) {
                    return failure(std::string("fontColor.") + names[i], "hue ranges overlap");
                }
            }
        }
    }
    for (std::size_t i = 0; i < 4; ++i) {
        for (std::size_t j = i + 1; j < 4; ++j) {
            for (const auto& a : rules[i]->hueRanges) {
                for (const auto& b : rules[j]->hueRanges) {
                    if (overlaps(a, b)) {
                        return failure(std::string("fontColor.") + names[j], "hue range overlaps another color rule");
                    }
                }
            }
        }
    }
    return {};
}

ConfigValidation validate(const TableLayoutConfig& config) {
    if (config.version != "1") return failure("tableLayout.version", "unsupported version");
    const auto validRange = [](const ColumnRange& range) {
        return std::isfinite(range.minX) && std::isfinite(range.maxX) &&
               range.minX >= 0.0F && range.maxX <= 1.0F && range.minX < range.maxX;
    };
    if (!validRange(config.codeColumn)) return failure("tableLayout.codeColumn", "invalid normalized column range");
    if (!validRange(config.nameColumn)) return failure("tableLayout.nameColumn", "invalid normalized column range");
    if (config.codeColumn.maxX > config.nameColumn.minX ||
        config.nameColumn.maxX <= config.codeColumn.minX) {
        return failure("tableLayout.columns", "code and name columns must be ordered and non-overlapping");
    }
    if (!std::isfinite(config.minVerticalOverlap) ||
        config.minVerticalOverlap < 0.0F || config.minVerticalOverlap > 1.0F) {
        return failure("tableLayout.minVerticalOverlap", "must be between 0 and 1");
    }
    if (config.maxCenterDistancePixels < 0 || config.maxCenterDistancePixels > 1000) {
        return failure("tableLayout.maxCenterDistancePixels", "must be between 0 and 1000");
    }
    return {};
}

ConfigValidation validate(const StockMonitorConfig& config) {
    if (config.schemaVersion != "1") return failure("schemaVersion", "unsupported version");
    const auto color = validate(config.fontColor);
    if (!color.valid) return color;
    return validate(config.tableLayout);
}

}  // namespace stock_monitor

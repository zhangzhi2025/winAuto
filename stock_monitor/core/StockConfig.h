#pragma once

#include <string>
#include <vector>

namespace stock_monitor {

struct HueRange {
    float min = 0.0F;
    float max = 0.0F;
};

struct ColorRule {
    std::vector<HueRange> hueRanges;
    int minSaturation = 0;
    int minValue = 0;
};

struct FontColorConfig {
    std::string version = "1";
    bool enabled = true;
    int minForegroundPixels = 8;
    float minDominantRatio = 0.65F;
    float minConfidence = 0.80F;
    int minBrightness = 48;
    int whiteMinBrightness = 180;
    int whiteMaxSaturation = 45;
    ColorRule red{{{345.0F, 360.0F}, {0.0F, 15.0F}}, 70, 65};
    ColorRule orange{{{15.0F, 45.0F}}, 70, 65};
    ColorRule yellow{{{45.0F, 75.0F}}, 65, 70};
    ColorRule magenta{{{285.0F, 345.0F}}, 60, 55};
};

struct ColumnRange {
    float minX = 0.0F;
    float maxX = 0.0F;
};

struct TableLayoutConfig {
    std::string version = "1";
    ColumnRange codeColumn{0.0F, 0.20F};
    ColumnRange nameColumn{0.20F, 0.50F};
    float minVerticalOverlap = 0.35F;
    int maxCenterDistancePixels = 10;
};

struct StockMonitorConfig {
    std::string schemaVersion = "1";
    FontColorConfig fontColor;
    TableLayoutConfig tableLayout;
};

struct ConfigValidation {
    bool valid = true;
    std::string path;
    std::string message;
};

ConfigValidation validate(const FontColorConfig& config);
ConfigValidation validate(const TableLayoutConfig& config);
ConfigValidation validate(const StockMonitorConfig& config);

}  // namespace stock_monitor

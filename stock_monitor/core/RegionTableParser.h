#pragma once

#include <string>
#include <vector>

#include "FontColorAnalyzer.h"
#include "RegionCaptureModel.h"
#include "RegionConfig.h"
#include "StockModel.h"

namespace stock_monitor {

struct TypedValue {
    bool hasText = false;
    std::string text;
    bool hasInteger = false;
    long long integer = 0;
    bool hasNumber = false;
    double number = 0.0;
};

// Strict, locale independent conversions. Only surrounding whitespace is
// trimmed; anything else that does not match the declared type fails.
FieldStatus convertFieldText(const std::string& rawText,
                             FieldValueType type,
                             TypedValue& value,
                             std::string& message);

struct TableParseOptions {
    float lowConfidenceThreshold = kDefaultLowConfidenceThreshold;
    // When set, font colors are measured on the region image in region-local
    // coordinates. The image is never modified.
    const Image* image = nullptr;
    const FontColorConfig* fontColorConfig = nullptr;
    int maxRows = 500;
};

// Assigns OCR blocks to configured table cells. A cell receives at most one
// block; more than one is reported as ambiguous instead of being merged.
std::vector<TableRowResult> parseTableRows(const TableTemplate& table,
                                           const Size& regionSize,
                                           const std::vector<OcrTextBlock>& blocks,
                                           const TableParseOptions& options);

// Applies color analysis to every block of a plain OCR region. Blocks outside the
// image are still reported, with an empty analysis result.
std::vector<FontColorResult> analyzeBlockColors(const Image& image,
                                                const std::vector<OcrTextBlock>& blocks,
                                                const FontColorConfig& config);

}  // namespace stock_monitor

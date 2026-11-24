#include "RegionTableParser.h"

#include <algorithm>
#include <cmath>
#include <cctype>
#include <limits>

namespace stock_monitor {
namespace {

bool isDigit(char c) {
    return std::isdigit(static_cast<unsigned char>(c)) != 0;
}

std::string trim(const std::string& text) {
    std::size_t begin = 0;
    std::size_t end = text.size();
    while(begin < end && std::isspace(static_cast<unsigned char>(text[begin])) != 0) {
        ++begin;
    }
    while(end > begin && std::isspace(static_cast<unsigned char>(text[end - 1])) != 0) {
        --end;
    }
    return text.substr(begin, end - begin);
}

// Decimal digits only: no separators, no exponent, no locale dependent parsing.
bool parseIntegerText(const std::string& text, long long& out) {
    if(text.empty()) {
        return false;
    }
    std::size_t index = 0;
    bool negative = false;
    if(text[index] == '+' || text[index] == '-') {
        negative = text[index] == '-';
        ++index;
    }
    if(index >= text.size()) {
        return false;
    }
    // Accumulate in the negative range so LLONG_MIN stays representable.
    long long value = 0;
    for(; index < text.size(); ++index) {
        if(!isDigit(text[index])) {
            return false;
        }
        const int digit = text[index] - '0';
        if(value < (std::numeric_limits<long long>::min() + digit) / 10) {
            return false;
        }
        value = value * 10 - digit;
    }
    if(!negative) {
        if(value == std::numeric_limits<long long>::min()) {
            return false;
        }
        value = -value;
    }
    out = value;
    return true;
}

bool parseDecimalText(const std::string& text, double& out) {
    if(text.empty()) {
        return false;
    }
    std::size_t index = 0;
    bool negative = false;
    if(text[index] == '+' || text[index] == '-') {
        negative = text[index] == '-';
        ++index;
    }
    long double value = 0.0L;
    std::size_t digits = 0;
    while(index < text.size() && isDigit(text[index])) {
        value = value * 10.0L + static_cast<long double>(text[index] - '0');
        ++digits;
        ++index;
        if(value > 1.0e18L) {
            return false;
        }
    }
    if(index < text.size() && text[index] == '.') {
        ++index;
        long double scale = 1.0L;
        while(index < text.size() && isDigit(text[index])) {
            value = value * 10.0L + static_cast<long double>(text[index] - '0');
            scale *= 10.0L;
            ++digits;
            ++index;
            if(value > 1.0e18L) {
                return false;
            }
        }
        value /= scale;
    }
    if(digits == 0 || index != text.size()) {
        return false;
    }
    const double converted = static_cast<double>(negative ? -value : value);
    if(!std::isfinite(converted)) {
        return false;
    }
    out = converted;
    return true;
}

bool horizontalCenterIn(const OcrTextBlock& block, const FieldDefinition& field) {
    const long long center = static_cast<long long>(block.bounds.x) * 2 + block.bounds.width;
    const long long left = static_cast<long long>(field.x) * 2;
    const long long right = static_cast<long long>(field.x + field.width) * 2;
    return center >= left && center < right;
}

bool verticalCenterIn(const OcrTextBlock& block, int top, int height) {
    const long long center = static_cast<long long>(block.bounds.y) * 2 + block.bounds.height;
    const long long bandTop = static_cast<long long>(top) * 2;
    const long long bandBottom = static_cast<long long>(top + height) * 2;
    return center >= bandTop && center < bandBottom;
}

std::vector<const OcrTextBlock*> sortedBlocks(const std::vector<OcrTextBlock>& blocks) {
    std::vector<const OcrTextBlock*> ordered;
    ordered.reserve(blocks.size());
    for(const OcrTextBlock& block : blocks) {
        ordered.push_back(&block);
    }
    std::stable_sort(ordered.begin(), ordered.end(), [](const OcrTextBlock* left, const OcrTextBlock* right) {
        if(left->order != right->order) {
            return left->order < right->order;
        }
        if(left->bounds.y != right->bounds.y) {
            return left->bounds.y < right->bounds.y;
        }
        return left->bounds.x < right->bounds.x;
    });
    return ordered;
}

TableRowStatus combineRowStatus(const std::vector<TypedFieldValue>& fields) {
    bool ambiguous = false;
    bool incomplete = false;
    for(const TypedFieldValue& field : fields) {
        switch(field.status) {
            case FieldStatus::Parsed:
                break;
            case FieldStatus::Ambiguous:
                ambiguous = true;
                break;
            case FieldStatus::Missing:
            case FieldStatus::InvalidFormat:
            case FieldStatus::LowConfidence:
                incomplete = true;
                break;
        }
    }
    if(ambiguous) {
        return TableRowStatus::Ambiguous;
    }
    return incomplete ? TableRowStatus::Incomplete : TableRowStatus::Complete;
}

TypedFieldValue buildFieldValue(const FieldDefinition& field,
                                const TypedValue& typed,
                                bool hasRawText,
                                const std::string& rawText,
                                float confidence) {
    TypedFieldValue value;
    value.fieldId = field.id;
    value.valueType = field.valueType;
    value.hasRawText = hasRawText;
    value.rawText = rawText;
    value.hasText = typed.hasText;
    value.textValue = typed.text;
    value.hasInteger = typed.hasInteger;
    value.integerValue = typed.integer;
    value.hasNumber = typed.hasNumber;
    value.numberValue = typed.number;
    value.hasConfidence = true;
    value.ocrConfidence = confidence;
    return value;
}

}  // namespace

FieldStatus convertFieldText(const std::string& rawText,
                             FieldValueType type,
                             TypedValue& value,
                             std::string& message) {
    value = TypedValue{};
    message.clear();
    const std::string text = trim(rawText);
    if(text.empty()) {
        message = "the recognized text is empty";
        return FieldStatus::Missing;
    }
    if(type == FieldValueType::Text) {
        value.hasText = true;
        value.text = text;
        return FieldStatus::Parsed;
    }
    if(type == FieldValueType::Integer) {
        long long parsed = 0;
        if(!parseIntegerText(text, parsed)) {
            message = "value is not a plain decimal integer";
            return FieldStatus::InvalidFormat;
        }
        value.hasInteger = true;
        value.integer = parsed;
        value.hasNumber = true;
        value.number = static_cast<double>(parsed);
        return FieldStatus::Parsed;
    }
    if(type == FieldValueType::Percent) {
        if(text.size() < 2 || text.back() != '%') {
            message = "value must end with a percent sign";
            return FieldStatus::InvalidFormat;
        }
        double parsed = 0.0;
        if(!parseDecimalText(trim(text.substr(0, text.size() - 1)), parsed)) {
            message = "value before the percent sign is not a decimal number";
            return FieldStatus::InvalidFormat;
        }
        // 12.50% is reported as 12.5 and is never divided by 100.
        value.hasNumber = true;
        value.number = parsed;
        message.clear();
        return FieldStatus::Parsed;
    }
    double parsed = 0.0;
    if(!parseDecimalText(text, parsed)) {
        message = "value is not a plain decimal number";
        return FieldStatus::InvalidFormat;
    }
    value.hasNumber = true;
    value.number = parsed;
    return FieldStatus::Parsed;
}

std::vector<TableRowResult> parseTableRows(const TableTemplate& table,
                                           const Size& regionSize,
                                           const std::vector<OcrTextBlock>& blocks,
                                           const TableParseOptions& options) {
    std::vector<TableRowResult> rows;
    if(table.rowPitch <= 0 || table.rowHeight <= 0 || table.fields.empty() ||
       regionSize.width <= 0 || regionSize.height <= 0) {
        return rows;
    }
    const int availableRows = (regionSize.height - table.firstRowY - table.rowHeight) / table.rowPitch + 1;
    if(availableRows <= 0) {
        return rows;
    }
    const int rowCount = std::min(availableRows, std::max(options.maxRows, 0));
    const std::vector<const OcrTextBlock*> ordered = sortedBlocks(blocks);

    for(int rowIndex = 0; rowIndex < rowCount; ++rowIndex) {
        const int bandTop = table.firstRowY + rowIndex * table.rowPitch;
        std::vector<std::vector<const OcrTextBlock*>> cellBlocks(table.fields.size());
        for(const OcrTextBlock* block : ordered) {
            if(!verticalCenterIn(*block, bandTop, table.rowHeight)) {
                continue;
            }
            for(std::size_t fieldIndex = 0; fieldIndex < table.fields.size(); ++fieldIndex) {
                if(horizontalCenterIn(*block, table.fields[fieldIndex])) {
                    cellBlocks[fieldIndex].push_back(block);
                    break;
                }
            }
        }

        bool anyBlock = false;
        for(const auto& cell : cellBlocks) {
            if(!cell.empty()) {
                anyBlock = true;
                break;
            }
        }
        if(!anyBlock) {
            continue;
        }

        TableRowResult row;
        row.rowIndex = rowIndex;
        row.bounds.x = table.fields.front().x;
        row.bounds.y = bandTop;
        row.bounds.width = 0;
        for(const FieldDefinition& field : table.fields) {
            row.bounds.width = std::max(row.bounds.width, field.x + field.width - row.bounds.x);
        }
        row.bounds.width = std::min(row.bounds.width, regionSize.width - row.bounds.x);
        row.bounds.height = std::min(table.rowHeight, regionSize.height - bandTop);

        for(std::size_t fieldIndex = 0; fieldIndex < table.fields.size(); ++fieldIndex) {
            const FieldDefinition& field = table.fields[fieldIndex];
            const std::vector<const OcrTextBlock*>& cell = cellBlocks[fieldIndex];
            if(cell.empty()) {
                TypedFieldValue missing;
                missing.fieldId = field.id;
                missing.valueType = field.valueType;
                missing.status = FieldStatus::Missing;
                missing.message = "no recognized text inside this cell";
                row.fields.push_back(missing);
                continue;
            }
            if(cell.size() > 1) {
                std::string joined;
                for(const OcrTextBlock* block : cell) {
                    if(!joined.empty()) {
                        joined += " | ";
                    }
                    joined += block->text;
                }
                TypedFieldValue ambiguous;
                ambiguous.fieldId = field.id;
                ambiguous.valueType = field.valueType;
                ambiguous.hasRawText = true;
                ambiguous.rawText = joined;
                ambiguous.status = FieldStatus::Ambiguous;
                ambiguous.message = "more than one recognized text block fell into this cell";
                row.fields.push_back(ambiguous);
                continue;
            }
            const OcrTextBlock& block = *cell.front();
            TypedValue typed;
            std::string message;
            FieldStatus status = convertFieldText(block.text, field.valueType, typed, message);
            if(status == FieldStatus::Parsed &&
               block.recognitionConfidence < options.lowConfidenceThreshold) {
                status = FieldStatus::LowConfidence;
                message = "recognized text confidence is below the accepted threshold";
            }
            TypedFieldValue value = buildFieldValue(field, typed, true, block.text, block.recognitionConfidence);
            value.hasSourceBounds = true;
            value.sourceBounds = block.bounds;
            value.status = status;
            value.message = message;
            if(field.analyzeFontColor && options.image != nullptr && options.fontColorConfig != nullptr) {
                FontColorConfig scoped = *options.fontColorConfig;
                scoped.enabled = true;
                value.fontColor = analyzeFontColor(*options.image, block.bounds, scoped);
                value.fontColorAnalyzed = true;
            }
            row.fields.push_back(value);
        }
        row.status = combineRowStatus(row.fields);
        rows.push_back(std::move(row));
    }
    return rows;
}

std::vector<FontColorResult> analyzeBlockColors(const Image& image,
                                                const std::vector<OcrTextBlock>& blocks,
                                                const FontColorConfig& config) {
    std::vector<Rect> bounds;
    bounds.reserve(blocks.size());
    for(const OcrTextBlock& block : blocks) {
        bounds.push_back(block.bounds);
    }
    return analyzeFontColors(image, bounds, config);
}

}  // namespace stock_monitor

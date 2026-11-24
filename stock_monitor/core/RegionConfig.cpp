#include "RegionConfig.h"

#include <algorithm>
#include <cctype>
#include <string>

namespace stock_monitor {
namespace {

ConfigValidation failure(std::string path, std::string message) {
    return ConfigValidation{false, std::move(path), std::move(message)};
}

std::string indexed(const std::string& base, std::size_t index) {
    return base + "[" + std::to_string(index) + "]";
}

bool allDigits(const std::string& text, std::size_t offset, std::size_t length) {
    if(offset + length > text.size()) {
        return false;
    }
    for(std::size_t i = offset; i < offset + length; ++i) {
        if(!std::isdigit(static_cast<unsigned char>(text[i]))) {
            return false;
        }
    }
    return true;
}

int twoDigits(const std::string& text, std::size_t offset) {
    return (text[offset] - '0') * 10 + (text[offset + 1] - '0');
}

bool overlaps(int beginA, int endA, int beginB, int endB) {
    return beginA < endB && beginB < endA;
}

}  // namespace

bool clientSizeMatches(const Size& reference, const Size& actual) {
    return reference.width == actual.width && reference.height == actual.height;
}

bool parseDailyTime(const std::string& text, int& secondsOfDay) {
    // Strict 24-hour HH:mm:ss only; a locale dependent parser would be ambiguous.
    if(text.size() != 8 || text[2] != ':' || text[5] != ':') {
        return false;
    }
    if(!allDigits(text, 0, 2) || !allDigits(text, 3, 2) || !allDigits(text, 6, 2)) {
        return false;
    }
    const int hour = twoDigits(text, 0);
    const int minute = twoDigits(text, 3);
    const int second = twoDigits(text, 6);
    if(hour > 23 || minute > 59 || second > 59) {
        return false;
    }
    secondsOfDay = hour * 3600 + minute * 60 + second;
    return true;
}

bool parseCaptureRegionMode(const std::string& text, CaptureRegionMode& mode) {
    if(text == "ocr") {
        mode = CaptureRegionMode::Ocr;
        return true;
    }
    if(text == "table") {
        mode = CaptureRegionMode::Table;
        return true;
    }
    return false;
}

bool parseFieldValueType(const std::string& text, FieldValueType& type) {
    if(text == "text") {
        type = FieldValueType::Text;
        return true;
    }
    if(text == "integer") {
        type = FieldValueType::Integer;
        return true;
    }
    if(text == "decimal") {
        type = FieldValueType::Decimal;
        return true;
    }
    if(text == "percent") {
        type = FieldValueType::Percent;
        return true;
    }
    return false;
}

const char* toString(CaptureRegionMode mode) {
    switch(mode) {
        case CaptureRegionMode::Ocr:
            return "ocr";
        case CaptureRegionMode::Table:
            return "table";
    }
    return "ocr";
}

const char* toString(FieldValueType type) {
    switch(type) {
        case FieldValueType::Text:
            return "text";
        case FieldValueType::Integer:
            return "integer";
        case FieldValueType::Decimal:
            return "decimal";
        case FieldValueType::Percent:
            return "percent";
    }
    return "text";
}

ConfigValidation validateDailyWindow(const DailyWindow& window, std::string path) {
    int startSeconds = 0;
    if(!parseDailyTime(window.start, startSeconds)) {
        return failure(std::move(path) + ".start", "start must be a 24-hour HH:mm:ss time");
    }
    int endSeconds = 0;
    if(!parseDailyTime(window.end, endSeconds)) {
        return failure(std::move(path) + ".end", "end must be a 24-hour HH:mm:ss time");
    }
    if(startSeconds >= endSeconds) {
        return failure(std::move(path), "start must be earlier than end; midnight crossing is not supported");
    }
    return {};
}

ConfigValidation validateFieldDefinition(const FieldDefinition& field,
                                         const Rect& regionRect,
                                         std::string path) {
    if(field.id.empty()) {
        return failure(path + ".id", "field id must not be empty");
    }
    if(field.name.empty()) {
        return failure(path + ".name", "field name must not be empty");
    }
    if(field.x < 0) {
        return failure(path + ".x", "field column offset must not be negative");
    }
    if(field.width <= 0) {
        return failure(path + ".width", "field column width must be positive");
    }
    if(field.x + field.width > regionRect.width) {
        return failure(path + ".width", "field column exceeds the region width");
    }
    return {};
}

ConfigValidation validateTableTemplate(const TableTemplate& table,
                                       const Rect& regionRect,
                                       std::string path) {
    if(table.firstRowY < 0) {
        return failure(path + ".firstRowY", "firstRowY must not be negative");
    }
    if(table.rowHeight <= 0) {
        return failure(path + ".rowHeight", "rowHeight must be positive");
    }
    if(table.rowPitch <= 0) {
        return failure(path + ".rowPitch", "rowPitch must be positive");
    }
    if(table.rowPitch < table.rowHeight) {
        return failure(path + ".rowPitch", "rowPitch must not be smaller than rowHeight");
    }
    if(table.fields.empty()) {
        return failure(path + ".fields", "table template requires at least one field");
    }
    if(table.firstRowY + table.rowHeight > regionRect.height) {
        return failure(path + ".firstRowY", "first data row band does not fit inside the region height");
    }
    std::vector<std::string> ids;
    std::vector<std::string> names;
    for(std::size_t i = 0; i < table.fields.size(); ++i) {
        const std::string fieldPath = indexed(path + ".fields", i);
        const ConfigValidation fieldValidation =
            validateFieldDefinition(table.fields[i], regionRect, fieldPath);
        if(!fieldValidation.valid) {
            return fieldValidation;
        }
        if(std::find(ids.begin(), ids.end(), table.fields[i].id) != ids.end()) {
            return failure(fieldPath + ".id", "field id must be unique inside the template");
        }
        if(std::find(names.begin(), names.end(), table.fields[i].name) != names.end()) {
            return failure(fieldPath + ".name", "field name must be unique inside the template");
        }
        ids.push_back(table.fields[i].id);
        names.push_back(table.fields[i].name);
    }
    for(std::size_t i = 1; i < table.fields.size(); ++i) {
        const FieldDefinition& previous = table.fields[i - 1];
        const FieldDefinition& current = table.fields[i];
        if(current.x < previous.x) {
            return failure(indexed(path + ".fields", i) + ".x",
                           "fields must be ordered by ascending column offset");
        }
        if(overlaps(previous.x, previous.x + previous.width, current.x, current.x + current.width)) {
            return failure(indexed(path + ".fields", i) + ".x", "field column ranges must not overlap");
        }
    }
    return {};
}

ConfigValidation validateCaptureRegion(const CaptureRegion& region,
                                        const Size& clientSize,
                                        std::string path) {
    if(region.id.empty()) {
        return failure(path + ".id", "region id must not be empty");
    }
    if(region.name.empty()) {
        return failure(path + ".name", "region name must not be empty");
    }
    if(region.rect.empty()) {
        return failure(path + ".rect", "region rectangle must have a positive size");
    }
    if(region.rect.x < 0 || region.rect.y < 0) {
        return failure(path + ".rect", "region rectangle must stay inside the client area");
    }
    if(region.rect.right() > clientSize.width || region.rect.bottom() > clientSize.height) {
        return failure(path + ".rect", "region rectangle exceeds the reference client size");
    }
    if(region.intervalMs < kMinRegionIntervalMs || region.intervalMs > kMaxRegionIntervalMs) {
        return failure(path + ".intervalMs",
                       "intervalMs must be between " + std::to_string(kMinRegionIntervalMs) +
                           " and " + std::to_string(kMaxRegionIntervalMs));
    }
    if(region.mode == CaptureRegionMode::Table) {
        const ConfigValidation tableValidation =
            validateTableTemplate(region.table, region.rect, path + ".table");
        if(!tableValidation.valid) {
            return tableValidation;
        }
    }
    return {};
}

ConfigValidation validate(const RegionCaptureConfig& config) {
    if(config.schemaVersion != kRegionCaptureSchemaVersion) {
        return failure("schemaVersion", "unsupported schemaVersion; expected " +
                                            std::string(kRegionCaptureSchemaVersion));
    }
    if(config.configId.empty()) {
        return failure("configId", "configId must not be empty");
    }
    if(config.name.empty()) {
        return failure("name", "configuration name must not be empty");
    }
    if(config.referenceClientSize.width <= 0 || config.referenceClientSize.height <= 0) {
        return failure("referenceClientSize", "reference client width and height must be positive");
    }
    if(config.referenceDpi < 0) {
        return failure("referenceDpi", "referenceDpi must not be negative");
    }
    const ConfigValidation dailyValidation = validateDailyWindow(config.dailyWindow, "dailyWindow");
    if(!dailyValidation.valid) {
        return dailyValidation;
    }
    if(config.regions.empty()) {
        return failure("regions", "configuration requires at least one region");
    }
    std::vector<std::string> ids;
    for(std::size_t i = 0; i < config.regions.size(); ++i) {
        const std::string regionPath = indexed("regions", i);
        const ConfigValidation regionValidation =
            validateCaptureRegion(config.regions[i], config.referenceClientSize, regionPath);
        if(!regionValidation.valid) {
            return regionValidation;
        }
        if(std::find(ids.begin(), ids.end(), config.regions[i].id) != ids.end()) {
            return failure(regionPath + ".id", "region id must be unique inside the configuration");
        }
        ids.push_back(config.regions[i].id);
    }
    return {};
}

}  // namespace stock_monitor

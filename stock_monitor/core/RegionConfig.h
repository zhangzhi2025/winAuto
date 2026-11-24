#pragma once

#include <string>
#include <vector>

#include "StockConfig.h"
#include "StockModel.h"

namespace stock_monitor {

constexpr const char* kRegionCaptureSchemaVersion = "1";
constexpr int kMinRegionIntervalMs = 200;
constexpr int kMaxRegionIntervalMs = 3600000;
constexpr float kDefaultLowConfidenceThreshold = 0.5F;

struct Size {
    int width = 0;
    int height = 0;
};

// Persisted identity hint only. A native window handle is never part of a saved
// configuration because handles are session local and become stale.
struct WindowIdentityHint {
    std::string title;
    std::string processName;

    bool empty() const { return title.empty() && processName.empty(); }
};

enum class CaptureRegionMode { Ocr, Table };
enum class FieldValueType { Text, Integer, Decimal, Percent };

struct FieldDefinition {
    std::string id;
    std::string name;
    // Pixel offset of the field column from the region left edge.
    int x = 0;
    // Pixel width of the field column.
    int width = 0;
    FieldValueType valueType = FieldValueType::Text;
    bool analyzeFontColor = false;
};

struct TableTemplate {
    // Pixel offset of the first data row band from the region top edge. The band
    // covers [firstRowY + rowIndex * rowPitch, + rowHeight).
    int firstRowY = 0;
    int rowHeight = 0;
    int rowPitch = 0;
    std::vector<FieldDefinition> fields;
};

struct CaptureRegion {
    std::string id;
    std::string name;
    // Rectangle relative to the client-area top-left corner in physical pixels.
    Rect rect;
    int intervalMs = 0;
    bool enabled = true;
    CaptureRegionMode mode = CaptureRegionMode::Ocr;
    TableTemplate table;
    bool analyzeFontColor = false;
};

struct DailyWindow {
    std::string start = "09:30:00";
    std::string end = "15:00:00";
};

struct RegionCaptureConfig {
    std::string schemaVersion = kRegionCaptureSchemaVersion;
    std::string configId;
    std::string name;
    WindowIdentityHint targetWindow;
    // Client-area physical pixels recorded when the regions were calibrated.
    Size referenceClientSize;
    // Diagnostic only; never used to scale coordinates. 0 means unspecified.
    int referenceDpi = 0;
    DailyWindow dailyWindow;
    std::vector<CaptureRegion> regions;
};

bool clientSizeMatches(const Size& reference, const Size& actual);
bool parseDailyTime(const std::string& text, int& secondsOfDay);
bool parseCaptureRegionMode(const std::string& text, CaptureRegionMode& mode);
bool parseFieldValueType(const std::string& text, FieldValueType& type);

const char* toString(CaptureRegionMode mode);
const char* toString(FieldValueType type);

ConfigValidation validate(const RegionCaptureConfig& config);
ConfigValidation validateDailyWindow(const DailyWindow& window, std::string path);
ConfigValidation validateCaptureRegion(const CaptureRegion& region,
                                       const Size& clientSize,
                                       std::string path);
ConfigValidation validateTableTemplate(const TableTemplate& table,
                                       const Rect& regionRect,
                                       std::string path);
ConfigValidation validateFieldDefinition(const FieldDefinition& field,
                                         const Rect& regionRect,
                                         std::string path);

}  // namespace stock_monitor

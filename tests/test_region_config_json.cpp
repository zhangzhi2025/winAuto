#include "testing.h"

#include <filesystem>
#include <string>

#include "RegionConfig.h"
#include "RegionConfigJson.h"

using namespace stock_monitor;

namespace {

RegionCaptureConfig makeConfig() {
    RegionCaptureConfig config;
    config.schemaVersion = kRegionCaptureSchemaVersion;
    config.configId = "cfg-watch";
    config.name = "盘口监控";
    config.targetWindow.title = "Trade";
    config.targetWindow.processName = "trade.exe";
    config.referenceClientSize = Size{1024, 768};
    config.referenceDpi = 144;
    config.dailyWindow.start = "09:30:00";
    config.dailyWindow.end = "15:00:00";

    CaptureRegion table;
    table.id = "r1";
    table.name = "盘口表格";
    table.rect = Rect{10, 20, 300, 200};
    table.intervalMs = 1000;
    table.enabled = true;
    table.mode = CaptureRegionMode::Table;
    table.analyzeFontColor = true;
    table.table.firstRowY = 5;
    table.table.rowHeight = 20;
    table.table.rowPitch = 30;
    table.table.fields.push_back(FieldDefinition{"code", "代码", 0, 100, FieldValueType::Text, false});
    table.table.fields.push_back(FieldDefinition{"price", "涨幅", 100, 100, FieldValueType::Percent, true});
    config.regions.push_back(table);

    CaptureRegion plain;
    plain.id = "r2";
    plain.name = "标题";
    plain.rect = Rect{0, 0, 100, 50};
    plain.intervalMs = 500;
    plain.enabled = false;
    plain.mode = CaptureRegionMode::Ocr;
    config.regions.push_back(plain);
    return config;
}

std::string fullDocument() {
    return R"({
  "schemaVersion": "1",
  "configId": "cfg-watch",
  "name": "盘口监控",
  "targetWindow": { "title": "Trade", "processName": "trade.exe" },
  "referenceClientSize": { "width": 1024, "height": 768 },
  "referenceDpi": 144,
  "dailyWindow": { "start": "09:30:00", "end": "15:00:00" },
  "regions": [
    {
      "id": "r1",
      "name": "盘口表格",
      "rect": { "x": 10, "y": 20, "width": 300, "height": 200 },
      "intervalMs": 1000,
      "enabled": true,
      "mode": "table",
      "analyzeFontColor": true,
      "table": {
        "firstRowY": 5,
        "rowHeight": 20,
        "rowPitch": 30,
        "fields": [
          { "id": "code", "name": "代码", "x": 0, "width": 100, "valueType": "text", "analyzeFontColor": false },
          { "id": "price", "name": "涨幅", "x": 100, "width": 100, "valueType": "percent", "analyzeFontColor": true }
        ]
      }
    },
    {
      "id": "r2",
      "name": "标题",
      "rect": { "x": 0, "y": 0, "width": 100, "height": 50 },
      "intervalMs": 500,
      "enabled": false,
      "mode": "ocr"
    }
  ]
}
)";
}

std::string wrapDocument(const std::string& regions) {
    return std::string(R"({ "schemaVersion": "1", "configId": "cfg", "name": "watch",
  "targetWindow": { "title": "Trade", "processName": "trade.exe" },
  "referenceClientSize": { "width": 1024, "height": 768 },
  "dailyWindow": { "start": "09:30:00", "end": "15:00:00" },
  "regions": [ )") +
           regions + "] }";
}

std::string plainRegion(const std::string& extraFields) {
    return std::string(R"({ "id": "r1", "name": "r1",
      "rect": { "x": 0, "y": 0, "width": 100, "height": 50 },
      "intervalMs": 1000, "enabled": true, "mode": "ocr")") + extraFields + " }";
}

std::size_t countOccurrences(const std::string& text, const std::string& needle) {
    std::size_t count = 0;
    std::size_t position = text.find(needle);
    while(position != std::string::npos) {
        ++count;
        position = text.find(needle, position + needle.size());
    }
    return count;
}

}  // namespace

TEST(region_config_json_round_trip_keeps_every_value) {
    const RegionConfigParseResult parsed = readRegionConfigJson(fullDocument());
    CHECK(parsed.ok);
    CHECK(parsed.validation.valid);
    CHECK(parsed.validation.message.empty());

    const RegionCaptureConfig& config = parsed.config;
    CHECK_EQ(config.schemaVersion, std::string("1"));
    CHECK_EQ(config.configId, std::string("cfg-watch"));
    CHECK_EQ(config.targetWindow.processName, std::string("trade.exe"));
    CHECK_EQ(config.referenceClientSize.width, 1024);
    CHECK_EQ(config.referenceClientSize.height, 768);
    CHECK_EQ(config.referenceDpi, 144);
    CHECK_EQ(config.dailyWindow.start, std::string("09:30:00"));
    CHECK_EQ(config.regions.size(), static_cast<std::size_t>(2));

    const CaptureRegion& table = config.regions[0];
    CHECK_EQ(table.mode, CaptureRegionMode::Table);
    CHECK(table.enabled);
    CHECK(table.analyzeFontColor);
    CHECK_EQ(table.rect.x, 10);
    CHECK_EQ(table.intervalMs, 1000);
    CHECK_EQ(table.table.rowPitch, 30);
    CHECK_EQ(table.table.fields.size(), static_cast<std::size_t>(2));
    CHECK_EQ(table.table.fields[1].valueType, FieldValueType::Percent);
    CHECK(table.table.fields[1].analyzeFontColor);

    const CaptureRegion& plain = config.regions[1];
    CHECK_EQ(plain.mode, CaptureRegionMode::Ocr);
    CHECK(!plain.enabled);
    CHECK(!plain.analyzeFontColor);
    CHECK(plain.table.fields.empty());
}

TEST(region_config_json_writer_is_deterministic_and_stable) {
    const RegionConfigParseResult parsed = readRegionConfigJson(fullDocument());
    CHECK(parsed.ok);
    const std::string first = writeRegionConfigJson(parsed.config);
    const std::string second = writeRegionConfigJson(parsed.config);
    CHECK_EQ(first, second);
    CHECK(first.size() > 2);
    CHECK_EQ(first.back(), '\n');
    CHECK_EQ(first.compare(first.size() - 2, 2, "}\n"), 0);

    const RegionConfigParseResult reparsed = readRegionConfigJson(first);
    CHECK(reparsed.ok);
    CHECK_EQ(writeRegionConfigJson(reparsed.config), first);
}

TEST(region_config_json_writer_orders_keys_like_the_schema) {
    const std::string text = writeRegionConfigJson(makeConfig());
    CHECK_EQ(text.compare(0, 21, "{\n  \"schemaVersion\": "), 0);
    CHECK_CONTAINS(text, std::string("\n  \"configId\": "));
    CHECK_CONTAINS(text, std::string("\n  \"referenceDpi\": "));
    CHECK_CONTAINS(text, std::string("\n  \"regions\": ["));

    const char* rootOrder[] = {"\"schemaVersion\"",
                               "\"configId\"",
                               "\"name\"",
                               "\"targetWindow\"",
                               "\"referenceClientSize\"",
                               "\"referenceDpi\"",
                               "\"dailyWindow\"",
                               "\"regions\""};
    std::size_t previous = 0;
    for(const char* key : rootOrder) {
        const std::size_t position = text.find(key);
        CHECK(position != std::string::npos);
        CHECK(position > previous);
        previous = position;
    }

    const std::size_t regionStart = text.find("\"r1\"");
    CHECK(regionStart != std::string::npos);
    const char* regionOrder[] = {"\"name\"",
                                 "\"rect\"",
                                 "\"x\"",
                                 "\"width\"",
                                 "\"intervalMs\"",
                                 "\"enabled\"",
                                 "\"mode\"",
                                 "\"analyzeFontColor\"",
                                 "\"table\"",
                                 "\"firstRowY\"",
                                 "\"rowPitch\"",
                                 "\"fields\"",
                                 "\"valueType\""};
    previous = regionStart;
    for(const char* key : regionOrder) {
        const std::size_t position = text.find(key, previous);
        CHECK(position != std::string::npos);
        CHECK(position > previous);
        previous = position;
    }
}

TEST(region_config_json_writer_omits_table_for_ocr_regions) {
    const std::string text = writeRegionConfigJson(makeConfig());
    // "table" also appears as the mode value, so the key form is counted.
    CHECK_EQ(countOccurrences(text, "\"table\": {"), static_cast<std::size_t>(1));
    CHECK_EQ(countOccurrences(text, "\"mode\": \"ocr\""), static_cast<std::size_t>(1));
    CHECK_EQ(countOccurrences(text, "\"fields\""), static_cast<std::size_t>(1));
}

TEST(region_config_json_writer_never_persists_runtime_state) {
    const std::string text = writeRegionConfigJson(makeConfig());
    CHECK_NOT_CONTAINS(text, std::string("hwnd"));
    CHECK_NOT_CONTAINS(text, std::string("HWND"));
    CHECK_NOT_CONTAINS(text, std::string("sessionId"));
    CHECK_NOT_CONTAINS(text, std::string("screenshot"));
    CHECK_NOT_CONTAINS(text, std::string("data:image"));
}

TEST(region_config_json_reader_applies_optional_defaults) {
    const std::string document = wrapDocument(plainRegion(""));
    const RegionConfigParseResult parsed = readRegionConfigJson(document);
    CHECK(parsed.ok);
    CHECK_EQ(parsed.config.referenceDpi, 0);
    CHECK(!parsed.config.regions[0].analyzeFontColor);
    CHECK(parsed.config.regions[0].table.fields.empty());
}

TEST(region_config_json_reader_ignores_unknown_keys) {
    const std::string document =
        wrapDocument(plainRegion(R"(, "note": "ignored", "sessionId": "abc", "hwnd": 12345)"));
    const RegionConfigParseResult parsed = readRegionConfigJson(document);
    CHECK(parsed.ok);
    CHECK_EQ(parsed.config.regions.size(), static_cast<std::size_t>(1));
    CHECK_EQ(parsed.config.regions[0].id, std::string("r1"));
}

TEST(region_config_json_reader_reports_wrong_types_with_json_paths) {
    const RegionConfigParseResult rootType = readRegionConfigJson(R"({ "schemaVersion": 1 })");
    CHECK(!rootType.ok);
    CHECK_EQ(rootType.validation.path, std::string("schemaVersion"));
    CHECK_CONTAINS(rootType.validation.message, std::string("必须是字符串"));

    const RegionConfigParseResult missingRect =
        readRegionConfigJson(wrapDocument(R"({ "id": "r1", "name": "r1", "intervalMs": 1000, "enabled": true, "mode": "ocr" })"));
    CHECK(!missingRect.ok);
    CHECK_EQ(missingRect.validation.path, std::string("regions[0].rect"));

    std::string wrongMode = plainRegion("");
    wrongMode.replace(wrongMode.find("\"ocr\""), 5, "\"grid\"");
    const RegionConfigParseResult badModeValue = readRegionConfigJson(wrapDocument(wrongMode));
    CHECK(!badModeValue.ok);
    CHECK_EQ(badModeValue.validation.path, std::string("regions[0].mode"));

    std::string wrongInterval = plainRegion("");
    wrongInterval.replace(wrongInterval.find("1000"), 4, "\"1000\"");
    const RegionConfigParseResult badInterval = readRegionConfigJson(wrapDocument(wrongInterval));
    CHECK(!badInterval.ok);
    CHECK_EQ(badInterval.validation.path, std::string("regions[0].intervalMs"));
    CHECK_CONTAINS(badInterval.validation.message, std::string("必须是整数"));
}

TEST(region_config_json_reader_reports_bad_table_fields_with_paths) {
    std::string region = plainRegion(R"(, "table": { "firstRowY": 0, "rowHeight": 20, "rowPitch": 30,
      "fields": [ { "id": "a", "name": "a", "x": 0, "width": 10, "valueType": "money" } ] })");
    region.replace(region.find("\"mode\": \"ocr\""), 13, "\"mode\": \"table\"");
    const RegionConfigParseResult parsed = readRegionConfigJson(wrapDocument(region));
    CHECK(!parsed.ok);
    CHECK_EQ(parsed.validation.path, std::string("regions[0].table.fields[0].valueType"));

    const RegionConfigParseResult badFields = readRegionConfigJson(wrapDocument(
        R"({ "id": "r1", "name": "r1", "rect": { "x": 0, "y": 0, "width": 100, "height": 50 },
      "intervalMs": 1000, "enabled": true, "mode": "table",
      "table": { "firstRowY": 0, "rowHeight": 20, "rowPitch": 30, "fields": 5 } })"));
    CHECK(!badFields.ok);
    CHECK_EQ(badFields.validation.path, std::string("regions[0].table.fields"));
    CHECK_CONTAINS(badFields.validation.message, std::string("必须是数组"));
}

TEST(region_config_json_reader_still_applies_semantic_validation) {
    std::string tooFast = plainRegion("");
    tooFast.replace(tooFast.find("1000"), 4, "100");
    const RegionConfigParseResult fast = readRegionConfigJson(wrapDocument(tooFast));
    CHECK(!fast.ok);
    CHECK_EQ(fast.validation.path, std::string("regions[0].intervalMs"));

    const std::string overlapping =
        R"({ "id": "r1", "name": "r1", "rect": { "x": 0, "y": 0, "width": 100, "height": 50 },
      "intervalMs": 1000, "enabled": true, "mode": "table",
      "table": { "firstRowY": 0, "rowHeight": 20, "rowPitch": 30,
      "fields": [ { "id": "a", "name": "a", "x": 0, "width": 60, "valueType": "text" },
                  { "id": "b", "name": "b", "x": 50, "width": 40, "valueType": "text" } ] } })";
    const RegionConfigParseResult overlap = readRegionConfigJson(wrapDocument(overlapping));
    CHECK(!overlap.ok);
    CHECK_EQ(overlap.validation.path, std::string("regions[0].table.fields[1].x"));

    std::string lateStart = wrapDocument(plainRegion(""));
    lateStart.replace(lateStart.find("09:30:00"), 8, "25:00:00");
    const RegionConfigParseResult daily = readRegionConfigJson(lateStart);
    CHECK(!daily.ok);
    CHECK_EQ(daily.validation.path, std::string("dailyWindow.start"));

    const RegionConfigParseResult noRegions = readRegionConfigJson(wrapDocument(""));
    CHECK(!noRegions.ok);
    CHECK_EQ(noRegions.validation.path, std::string("regions"));
}

TEST(region_config_json_reader_rejects_malformed_documents) {
    const RegionConfigParseResult broken = readRegionConfigJson("{ not json ");
    CHECK(!broken.ok);
    CHECK(broken.validation.path.empty());
    CHECK(!broken.validation.message.empty());

    const RegionConfigParseResult arrayRoot = readRegionConfigJson("[]");
    CHECK(!arrayRoot.ok);
    CHECK(arrayRoot.validation.path.empty());

    const RegionConfigParseResult empty = readRegionConfigJson("");
    CHECK(!empty.ok);
}

TEST(region_config_json_file_wrappers_round_trip_and_report_missing_files) {
    const std::filesystem::path target =
        std::filesystem::temp_directory_path() / "winauto_region_config_test.json";
    std::error_code ignored;
    std::filesystem::remove(target, ignored);

    const RegionCaptureConfig config = makeConfig();
    std::string error;
    CHECK(writeRegionConfigFile(target.string(), config, error));
    CHECK(error.empty());

    const RegionConfigParseResult loaded = readRegionConfigFile(target.string());
    CHECK(loaded.ok);
    CHECK_EQ(loaded.config.configId, config.configId);
    CHECK_EQ(loaded.config.regions.size(), config.regions.size());
    CHECK_EQ(writeRegionConfigJson(loaded.config), writeRegionConfigJson(config));

    RegionCaptureConfig invalid = config;
    invalid.regions.clear();
    std::string invalidError;
    CHECK(!writeRegionConfigFile((target.string() + ".invalid"), invalid, invalidError));
    CHECK_CONTAINS(invalidError, std::string("regions"));

    const std::string missing = (std::filesystem::temp_directory_path() / "winauto_missing_region.json").string();
    const RegionConfigParseResult absent = readRegionConfigFile(missing);
    CHECK(!absent.ok);
    CHECK(absent.validation.path.empty());
    CHECK_CONTAINS(absent.validation.message, missing);

    std::filesystem::remove(target, ignored);
    std::filesystem::remove(target.string() + ".invalid", ignored);
}

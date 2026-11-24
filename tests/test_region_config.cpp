#include "testing.h"

#include <utility>

#include "RegionConfig.h"

using namespace stock_monitor;

namespace {

CaptureRegion makeOcrRegion() {
    CaptureRegion region;
    region.id = "r1";
    region.name = "行情表";
    region.rect = Rect{10, 20, 300, 200};
    region.intervalMs = 1000;
    region.enabled = true;
    region.mode = CaptureRegionMode::Ocr;
    return region;
}

CaptureRegion makeTableRegion() {
    CaptureRegion region;
    region.id = "table";
    region.name = "自选表";
    region.rect = Rect{0, 0, 300, 200};
    region.intervalMs = 2000;
    region.enabled = true;
    region.mode = CaptureRegionMode::Table;
    region.table.firstRowY = 10;
    region.table.rowHeight = 20;
    region.table.rowPitch = 30;
    FieldDefinition code;
    code.id = "code";
    code.name = "代码";
    code.x = 0;
    code.width = 100;
    code.valueType = FieldValueType::Text;
    FieldDefinition price;
    price.id = "price";
    price.name = "价格";
    price.x = 100;
    price.width = 120;
    price.valueType = FieldValueType::Decimal;
    region.table.fields.push_back(code);
    region.table.fields.push_back(price);
    return region;
}

RegionCaptureConfig makeConfig() {
    RegionCaptureConfig config;
    config.configId = "cfg-1";
    config.name = "看盘配置";
    config.targetWindow.title = "客户端";
    config.targetWindow.processName = "client.exe";
    config.referenceClientSize = Size{1024, 768};
    config.dailyWindow.start = "09:30:00";
    config.dailyWindow.end = "15:00:00";
    config.regions.push_back(makeOcrRegion());
    return config;
}

}  // namespace

TEST(region_config_default_configuration_is_invalid) {
    const ConfigValidation validation = validate(RegionCaptureConfig{});
    CHECK(!validation.valid);
    CHECK_EQ(validation.path, std::string("configId"));
}

TEST(region_config_accepts_valid_configuration) {
    const RegionCaptureConfig config = makeConfig();
    const ConfigValidation validation = validate(config);
    CHECK(validation.valid);
    CHECK_EQ(validation.path, std::string());
}

TEST(region_config_rejects_unsupported_schema_version) {
    RegionCaptureConfig config = makeConfig();
    config.schemaVersion = "2";
    CHECK_EQ(validate(config).path, std::string("schemaVersion"));
}

TEST(region_config_requires_positive_reference_client_size) {
    RegionCaptureConfig config = makeConfig();
    config.referenceClientSize = Size{0, 768};
    CHECK_EQ(validate(config).path, std::string("referenceClientSize"));

    config = makeConfig();
    config.referenceClientSize = Size{1024, -1};
    CHECK_EQ(validate(config).path, std::string("referenceClientSize"));

    config = makeConfig();
    config.referenceDpi = -1;
    CHECK_EQ(validate(config).path, std::string("referenceDpi"));
}

TEST(region_config_requires_at_least_one_region) {
    RegionCaptureConfig config = makeConfig();
    config.regions.clear();
    CHECK_EQ(validate(config).path, std::string("regions"));
}

TEST(region_config_daily_window_requires_strict_hh_mm_ss) {
    RegionCaptureConfig config = makeConfig();
    config.dailyWindow.start = "9:30:00";
    CHECK_EQ(validate(config).path, std::string("dailyWindow.start"));

    config = makeConfig();
    config.dailyWindow.start = "24:00:00";
    CHECK_EQ(validate(config).path, std::string("dailyWindow.start"));

    config = makeConfig();
    config.dailyWindow.start = "09:60:00";
    CHECK_EQ(validate(config).path, std::string("dailyWindow.start"));

    config = makeConfig();
    config.dailyWindow.end = "15:00";
    CHECK_EQ(validate(config).path, std::string("dailyWindow.end"));

    config = makeConfig();
    config.dailyWindow.end = "15:00:60";
    CHECK_EQ(validate(config).path, std::string("dailyWindow.end"));
}

TEST(region_config_daily_window_must_not_cross_midnight) {
    RegionCaptureConfig config = makeConfig();
    config.dailyWindow.start = "15:00:00";
    config.dailyWindow.end = "09:30:00";
    const ConfigValidation validation = validate(config);
    CHECK(!validation.valid);
    CHECK_EQ(validation.path, std::string("dailyWindow"));
    CHECK_CONTAINS(validation.message, std::string("midnight"));

    config = makeConfig();
    config.dailyWindow.start = "09:30:00";
    config.dailyWindow.end = "09:30:00";
    CHECK_EQ(validate(config).path, std::string("dailyWindow"));
}

TEST(region_config_client_size_match_is_exact) {
    CHECK(clientSizeMatches(Size{1024, 768}, Size{1024, 768}));
    CHECK(!clientSizeMatches(Size{1024, 768}, Size{1025, 768}));
    CHECK(!clientSizeMatches(Size{1024, 768}, Size{1024, 769}));
}

TEST(region_config_region_must_fit_reference_client_size) {
    RegionCaptureConfig config = makeConfig();
    config.regions[0].rect = Rect{-1, 0, 100, 100};
    CHECK_EQ(validate(config).path, std::string("regions[0].rect"));

    config = makeConfig();
    config.regions[0].rect = Rect{1000, 0, 100, 100};
    CHECK_EQ(validate(config).path, std::string("regions[0].rect"));

    config = makeConfig();
    config.regions[0].rect = Rect{0, 700, 100, 100};
    CHECK_EQ(validate(config).path, std::string("regions[0].rect"));

    config = makeConfig();
    config.regions[0].rect = Rect{0, 0, 0, 100};
    CHECK_EQ(validate(config).path, std::string("regions[0].rect"));

    config = makeConfig();
    config.regions[0].rect = Rect{924, 668, 100, 100};
    CHECK(validate(config).valid);
}

TEST(region_config_region_id_must_be_unique) {
    RegionCaptureConfig config = makeConfig();
    config.regions.push_back(makeTableRegion());
    config.regions[1].id = config.regions[0].id;
    config.regions[1].name = "另一个区域";
    CHECK_EQ(validate(config).path, std::string("regions[1].id"));

    config = makeConfig();
    config.regions[0].id.clear();
    CHECK_EQ(validate(config).path, std::string("regions[0].id"));
}

TEST(region_config_interval_bounds) {
    RegionCaptureConfig config = makeConfig();
    config.regions[0].intervalMs = kMinRegionIntervalMs - 1;
    CHECK_EQ(validate(config).path, std::string("regions[0].intervalMs"));

    config = makeConfig();
    config.regions[0].intervalMs = kMaxRegionIntervalMs + 1;
    CHECK_EQ(validate(config).path, std::string("regions[0].intervalMs"));

    config = makeConfig();
    config.regions[0].intervalMs = kMinRegionIntervalMs;
    CHECK(validate(config).valid);

    config = makeConfig();
    config.regions[0].intervalMs = kMaxRegionIntervalMs;
    CHECK(validate(config).valid);
}

TEST(region_config_table_template_requires_fields) {
    RegionCaptureConfig config = makeConfig();
    CaptureRegion region = makeTableRegion();
    region.table.fields.clear();
    config.regions[0] = region;
    CHECK_EQ(validate(config).path, std::string("regions[0].table.fields"));
}

TEST(region_config_table_template_geometry_rules) {
    RegionCaptureConfig config = makeConfig();

    CaptureRegion region = makeTableRegion();
    region.table.rowHeight = 0;
    config.regions[0] = region;
    CHECK_EQ(validate(config).path, std::string("regions[0].table.rowHeight"));

    region = makeTableRegion();
    region.table.rowPitch = region.table.rowHeight - 1;
    config.regions[0] = region;
    CHECK_EQ(validate(config).path, std::string("regions[0].table.rowPitch"));

    region = makeTableRegion();
    region.table.firstRowY = region.rect.height - region.table.rowHeight + 1;
    config.regions[0] = region;
    CHECK_EQ(validate(config).path, std::string("regions[0].table.firstRowY"));

    region = makeTableRegion();
    region.table.firstRowY = region.rect.height - region.table.rowHeight;
    config.regions[0] = region;
    CHECK(validate(config).valid);
}

TEST(region_config_field_columns_stay_inside_region) {
    RegionCaptureConfig config = makeConfig();
    CaptureRegion region = makeTableRegion();
    region.table.fields[1].x = 200;
    region.table.fields[1].width = 101;
    config.regions[0] = region;
    CHECK_EQ(validate(config).path, std::string("regions[0].table.fields[1].width"));

    region = makeTableRegion();
    region.table.fields[1].x = 200;
    region.table.fields[1].width = 100;
    config.regions[0] = region;
    CHECK(validate(config).valid);
}

TEST(region_config_field_columns_must_be_ordered_and_disjoint) {
    RegionCaptureConfig config = makeConfig();
    CaptureRegion region = makeTableRegion();
    std::swap(region.table.fields[0].x, region.table.fields[1].x);
    config.regions[0] = region;
    CHECK_EQ(validate(config).path, std::string("regions[0].table.fields[1].x"));
    CHECK_CONTAINS(validate(config).message, std::string("ascending"));

    region = makeTableRegion();
    region.table.fields[1].x = 50;
    config.regions[0] = region;
    CHECK_EQ(validate(config).path, std::string("regions[0].table.fields[1].x"));
    CHECK_CONTAINS(validate(config).message, std::string("overlap"));

    region = makeTableRegion();
    region.table.fields[1].x = 100;
    config.regions[0] = region;
    CHECK(validate(config).valid);
}

TEST(region_config_field_ids_and_names_must_be_unique) {
    RegionCaptureConfig config = makeConfig();
    CaptureRegion region = makeTableRegion();
    region.table.fields[1].id = region.table.fields[0].id;
    config.regions[0] = region;
    CHECK_EQ(validate(config).path, std::string("regions[0].table.fields[1].id"));

    region = makeTableRegion();
    region.table.fields[1].name = region.table.fields[0].name;
    config.regions[0] = region;
    CHECK_EQ(validate(config).path, std::string("regions[0].table.fields[1].name"));

    region = makeTableRegion();
    region.table.fields[0].name.clear();
    config.regions[0] = region;
    CHECK_EQ(validate(config).path, std::string("regions[0].table.fields[0].name"));
}

TEST(region_config_parses_daily_time) {
    int seconds = -1;
    CHECK(parseDailyTime("00:00:00", seconds));
    CHECK_EQ(seconds, 0);
    CHECK(parseDailyTime("09:30:00", seconds));
    CHECK_EQ(seconds, 9 * 3600 + 30 * 60);
    CHECK(parseDailyTime("23:59:59", seconds));
    CHECK_EQ(seconds, 86399);

    CHECK(!parseDailyTime("", seconds));
    CHECK(!parseDailyTime("09:30", seconds));
    CHECK(!parseDailyTime("09:30:00 ", seconds));
    CHECK(!parseDailyTime("09:30:0a", seconds));
    CHECK(!parseDailyTime("9:30:00", seconds));
    CHECK(!parseDailyTime("24:00:00", seconds));
    CHECK(!parseDailyTime("00:60:00", seconds));
    CHECK(!parseDailyTime("00:00:60", seconds));
    CHECK(!parseDailyTime("-1:00:00", seconds));
}

TEST(region_config_mode_and_type_round_trip) {
    CaptureRegionMode mode = CaptureRegionMode::Ocr;
    CHECK(parseCaptureRegionMode("table", mode));
    CHECK(mode == CaptureRegionMode::Table);
    CHECK_EQ(toString(mode), std::string("table"));
    CHECK(parseCaptureRegionMode("ocr", mode));
    CHECK(mode == CaptureRegionMode::Ocr);
    CHECK_EQ(toString(mode), std::string("ocr"));
    CHECK(!parseCaptureRegionMode("Table", mode));
    CHECK(!parseCaptureRegionMode("", mode));

    const FieldValueType types[] = {FieldValueType::Text, FieldValueType::Integer,
                                    FieldValueType::Decimal, FieldValueType::Percent};
    for(const FieldValueType type : types) {
        FieldValueType parsed = FieldValueType::Text;
        CHECK(parseFieldValueType(toString(type), parsed));
        CHECK(parsed == type);
    }
    FieldValueType parsed = FieldValueType::Text;
    CHECK(!parseFieldValueType("Float", parsed));
    CHECK(!parseFieldValueType("", parsed));
}

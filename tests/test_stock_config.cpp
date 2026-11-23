#include "testing.h"

#include "StockConfig.h"

using namespace stock_monitor;

TEST(stock_monitor_default_configuration_is_valid) {
    CHECK(validate(StockMonitorConfig{}).valid);
}

TEST(stock_monitor_configuration_rejects_bad_thresholds_and_overlap) {
    FontColorConfig invalid;
    invalid.minDominantRatio = 1.1F;
    CHECK_EQ(validate(invalid).path, std::string("fontColor.minDominantRatio"));

    invalid = FontColorConfig{};
    invalid.orange.hueRanges = {{10.0F, 30.0F}};
    CHECK(!validate(invalid).valid);

    invalid = FontColorConfig{};
    invalid.red.hueRanges = {{345.0F, 355.0F}, {350.0F, 360.0F}, {0.0F, 15.0F}};
    CHECK(!validate(invalid).valid);

    TableLayoutConfig layout;
    layout.nameColumn.minX = 0.1F;
    CHECK_EQ(validate(layout).path, std::string("tableLayout.columns"));
}

TEST(stock_monitor_configuration_rejects_unknown_versions) {
    StockMonitorConfig config;
    config.schemaVersion = "2";
    CHECK_EQ(validate(config).path, std::string("schemaVersion"));
}

#include "testing.h"

#include <string>
#include <vector>

#include "RegionTableParser.h"
#include "StockModel.h"

using namespace stock_monitor;

namespace {

TableTemplate makeTable() {
    TableTemplate table;
    table.firstRowY = 10;
    table.rowHeight = 20;
    table.rowPitch = 30;
    table.fields.push_back(FieldDefinition{"code", "代码", 0, 100, FieldValueType::Text, false});
    table.fields.push_back(FieldDefinition{"price", "价格", 100, 100, FieldValueType::Decimal, false});
    return table;
}

OcrTextBlock makeBlock(const std::string& text, int x, int y, int width, int height, float confidence, int order) {
    OcrTextBlock block;
    block.text = text;
    block.bounds.x = x;
    block.bounds.y = y;
    block.bounds.width = width;
    block.bounds.height = height;
    block.detectionConfidence = confidence;
    block.recognitionConfidence = confidence;
    block.order = order;
    return block;
}

}  // namespace

TEST(region_table_parser_text_type_trims_surrounding_space) {
    TypedValue value;
    std::string message;
    CHECK(convertFieldText("  hello world ", FieldValueType::Text, value, message) == FieldStatus::Parsed);
    CHECK(value.hasText);
    CHECK_EQ(value.text, std::string("hello world"));
    CHECK(!value.hasInteger);
    CHECK(!value.hasNumber);
    CHECK(message.empty());
}

TEST(region_table_parser_empty_text_is_missing) {
    TypedValue value;
    std::string message;
    CHECK(convertFieldText("", FieldValueType::Text, value, message) == FieldStatus::Missing);
    CHECK_CONTAINS(message, std::string("empty"));
    CHECK(!value.hasText);

    TypedValue spaced;
    std::string spacedMessage;
    CHECK(convertFieldText("   \t ", FieldValueType::Decimal, spaced, spacedMessage) == FieldStatus::Missing);
    CHECK_CONTAINS(spacedMessage, std::string("empty"));
    CHECK(!spaced.hasNumber);
}

TEST(region_table_parser_integer_type_is_strict) {
    TypedValue value;
    std::string message;
    CHECK(convertFieldText("42", FieldValueType::Integer, value, message) == FieldStatus::Parsed);
    CHECK(value.hasInteger);
    CHECK_EQ(value.integer, 42);
    CHECK(value.hasNumber);
    CHECK_EQ(value.number, 42.0);
    CHECK(!value.hasText);

    CHECK(convertFieldText("-17", FieldValueType::Integer, value, message) == FieldStatus::Parsed);
    CHECK_EQ(value.integer, -17);

    CHECK(convertFieldText("  +8  ", FieldValueType::Integer, value, message) == FieldStatus::Parsed);
    CHECK_EQ(value.integer, 8);

    const char* rejected[] = {"1,234", "1e3", "12.0", "abc", "-", "+", "12 34", "0x10"};
    for(const char* text : rejected) {
        TypedValue rejectedValue;
        std::string rejectedMessage;
        CHECK(convertFieldText(text, FieldValueType::Integer, rejectedValue, rejectedMessage) ==
              FieldStatus::InvalidFormat);
        CHECK_CONTAINS(rejectedMessage, std::string("plain decimal integer"));
        CHECK(!rejectedValue.hasInteger);
        CHECK(!rejectedValue.hasNumber);
    }
}

TEST(region_table_parser_decimal_type_is_strict) {
    TypedValue value;
    std::string message;
    CHECK(convertFieldText("3.5", FieldValueType::Decimal, value, message) == FieldStatus::Parsed);
    CHECK(value.hasNumber);
    CHECK_EQ(value.number, 3.5);
    CHECK(!value.hasInteger);
    CHECK(!value.hasText);

    CHECK(convertFieldText("-0.25", FieldValueType::Decimal, value, message) == FieldStatus::Parsed);
    CHECK_EQ(value.number, -0.25);

    CHECK(convertFieldText("12", FieldValueType::Decimal, value, message) == FieldStatus::Parsed);
    CHECK_EQ(value.number, 12.0);

    const char* rejected[] = {"1.2.3", "1,5", "1e3", ".", "-", "abc", "1 2"};
    for(const char* text : rejected) {
        TypedValue rejectedValue;
        std::string rejectedMessage;
        CHECK(convertFieldText(text, FieldValueType::Decimal, rejectedValue, rejectedMessage) ==
              FieldStatus::InvalidFormat);
        CHECK_CONTAINS(rejectedMessage, std::string("plain decimal number"));
        CHECK(!rejectedValue.hasNumber);
    }
}

TEST(region_table_parser_percent_requires_sign_and_keeps_scale) {
    TypedValue value;
    std::string message;
    CHECK(convertFieldText("12.50%", FieldValueType::Percent, value, message) == FieldStatus::Parsed);
    CHECK(value.hasNumber);
    CHECK_EQ(value.number, 12.5);
    CHECK(!value.hasInteger);
    CHECK(!value.hasText);

    CHECK(convertFieldText(" 5% ", FieldValueType::Percent, value, message) == FieldStatus::Parsed);
    CHECK_EQ(value.number, 5.0);

    CHECK(convertFieldText("-3%", FieldValueType::Percent, value, message) == FieldStatus::Parsed);
    CHECK_EQ(value.number, -3.0);

    TypedValue missingSign;
    std::string missingSignMessage;
    CHECK(convertFieldText("12.5", FieldValueType::Percent, missingSign, missingSignMessage) ==
          FieldStatus::InvalidFormat);
    CHECK_CONTAINS(missingSignMessage, std::string("percent sign"));
    CHECK(!missingSign.hasNumber);

    TypedValue onlySign;
    std::string onlySignMessage;
    CHECK(convertFieldText("%", FieldValueType::Percent, onlySign, onlySignMessage) == FieldStatus::InvalidFormat);
    CHECK_CONTAINS(onlySignMessage, std::string("percent sign"));

    TypedValue badNumber;
    std::string badNumberMessage;
    CHECK(convertFieldText("abc%", FieldValueType::Percent, badNumber, badNumberMessage) ==
          FieldStatus::InvalidFormat);
    CHECK_CONTAINS(badNumberMessage, std::string("not a decimal number"));
    CHECK(!badNumber.hasNumber);
}

TEST(region_table_parser_assigns_blocks_to_configured_cells) {
    const TableTemplate table = makeTable();
    std::vector<OcrTextBlock> blocks;
    blocks.push_back(makeBlock("600000", 10, 15, 40, 10, 0.9F, 0));
    blocks.push_back(makeBlock("12.5", 110, 15, 40, 10, 0.9F, 1));
    blocks.push_back(makeBlock("600001", 10, 45, 40, 10, 0.8F, 2));
    blocks.push_back(makeBlock("13", 110, 45, 40, 10, 0.8F, 3));

    TableParseOptions options;
    const std::vector<TableRowResult> rows = parseTableRows(table, Size{300, 200}, blocks, options);
    CHECK_EQ(rows.size(), static_cast<std::size_t>(2));
    CHECK_EQ(rows[0].rowIndex, 0);
    CHECK_EQ(rows[0].status, TableRowStatus::Complete);
    CHECK_EQ(rows[0].bounds.x, 0);
    CHECK_EQ(rows[0].bounds.y, 10);
    CHECK_EQ(rows[0].bounds.width, 200);
    CHECK_EQ(rows[0].bounds.height, 20);
    CHECK_EQ(rows[0].fields.size(), static_cast<std::size_t>(2));
    CHECK_EQ(rows[0].fields[0].fieldId, std::string("code"));
    CHECK_EQ(rows[0].fields[0].status, FieldStatus::Parsed);
    CHECK_EQ(rows[0].fields[0].textValue, std::string("600000"));
    CHECK_EQ(rows[0].fields[0].rawText, std::string("600000"));
    CHECK(rows[0].fields[0].hasSourceBounds);
    CHECK(rows[0].fields[0].hasConfidence);
    CHECK_EQ(rows[0].fields[0].ocrConfidence, 0.9F);
    CHECK_EQ(rows[0].fields[1].fieldId, std::string("price"));
    CHECK_EQ(rows[0].fields[1].numberValue, 12.5);

    CHECK_EQ(rows[1].rowIndex, 1);
    CHECK_EQ(rows[1].bounds.y, 40);
    CHECK_EQ(rows[1].fields[0].textValue, std::string("600001"));
    CHECK_EQ(rows[1].fields[1].numberValue, 13.0);
}

TEST(region_table_parser_skips_rows_without_any_block) {
    const TableTemplate table = makeTable();
    std::vector<OcrTextBlock> blocks;
    blocks.push_back(makeBlock("600000", 10, 75, 40, 10, 0.9F, 0));

    const std::vector<TableRowResult> rows = parseTableRows(table, Size{300, 200}, blocks, TableParseOptions{});
    CHECK_EQ(rows.size(), static_cast<std::size_t>(1));
    CHECK_EQ(rows[0].rowIndex, 2);
    CHECK_EQ(rows[0].bounds.y, 70);
}

TEST(region_table_parser_reports_ambiguous_cells_without_merging) {
    const TableTemplate table = makeTable();
    std::vector<OcrTextBlock> blocks;
    blocks.push_back(makeBlock("600000", 10, 15, 40, 10, 0.9F, 0));
    blocks.push_back(makeBlock("600001", 60, 15, 40, 10, 0.9F, 1));
    blocks.push_back(makeBlock("12.5", 110, 15, 40, 10, 0.9F, 2));

    const std::vector<TableRowResult> rows = parseTableRows(table, Size{300, 200}, blocks, TableParseOptions{});
    CHECK_EQ(rows.size(), static_cast<std::size_t>(1));
    CHECK_EQ(rows[0].fields[0].status, FieldStatus::Ambiguous);
    CHECK_CONTAINS(rows[0].fields[0].message, std::string("more than one"));
    CHECK_CONTAINS(rows[0].fields[0].rawText, std::string(" | "));
    CHECK(!rows[0].fields[0].hasText);
    CHECK(!rows[0].fields[0].hasNumber);
    CHECK_EQ(rows[0].status, TableRowStatus::Ambiguous);
}

TEST(region_table_parser_marks_missing_cells) {
    const TableTemplate table = makeTable();
    std::vector<OcrTextBlock> blocks;
    blocks.push_back(makeBlock("600000", 10, 15, 40, 10, 0.9F, 0));

    const std::vector<TableRowResult> rows = parseTableRows(table, Size{300, 200}, blocks, TableParseOptions{});
    CHECK_EQ(rows.size(), static_cast<std::size_t>(1));
    CHECK_EQ(rows[0].fields[0].status, FieldStatus::Parsed);
    CHECK_EQ(rows[0].fields[1].status, FieldStatus::Missing);
    CHECK_CONTAINS(rows[0].fields[1].message, std::string("no recognized text"));
    CHECK(!rows[0].fields[1].hasNumber);
    CHECK(!rows[0].fields[1].hasRawText);
    CHECK_EQ(rows[0].status, TableRowStatus::Incomplete);
}

TEST(region_table_parser_flags_low_confidence_but_keeps_raw_text) {
    const TableTemplate table = makeTable();
    std::vector<OcrTextBlock> blocks;
    blocks.push_back(makeBlock("600000", 10, 15, 40, 10, 0.9F, 0));
    blocks.push_back(makeBlock("12.5", 110, 15, 40, 10, 0.49F, 1));

    const std::vector<TableRowResult> rows = parseTableRows(table, Size{300, 200}, blocks, TableParseOptions{});
    CHECK_EQ(rows.size(), static_cast<std::size_t>(1));
    CHECK_EQ(rows[0].fields[1].status, FieldStatus::LowConfidence);
    CHECK_CONTAINS(rows[0].fields[1].message, std::string("confidence"));
    CHECK(rows[0].fields[1].hasRawText);
    CHECK_EQ(rows[0].fields[1].rawText, std::string("12.5"));
    CHECK_EQ(rows[0].status, TableRowStatus::Incomplete);

    TableParseOptions relaxed;
    relaxed.lowConfidenceThreshold = 0.4F;
    const std::vector<TableRowResult> relaxedRows = parseTableRows(table, Size{300, 200}, blocks, relaxed);
    CHECK_EQ(relaxedRows[0].fields[1].status, FieldStatus::Parsed);
}

TEST(region_table_parser_reports_invalid_format_for_declared_type) {
    const TableTemplate table = makeTable();
    std::vector<OcrTextBlock> blocks;
    blocks.push_back(makeBlock("600000", 10, 15, 40, 10, 0.9F, 0));
    blocks.push_back(makeBlock("abc", 110, 15, 40, 10, 0.9F, 1));

    const std::vector<TableRowResult> rows = parseTableRows(table, Size{300, 200}, blocks, TableParseOptions{});
    CHECK_EQ(rows[0].fields[1].status, FieldStatus::InvalidFormat);
    CHECK_CONTAINS(rows[0].fields[1].message, std::string("plain decimal number"));
    CHECK(rows[0].fields[1].hasRawText);
    CHECK(!rows[0].fields[1].hasNumber);
    CHECK_EQ(rows[0].status, TableRowStatus::Incomplete);
}

TEST(region_table_parser_rejects_invalid_template_or_region) {
    TableTemplate table = makeTable();
    std::vector<OcrTextBlock> blocks;
    blocks.push_back(makeBlock("600000", 10, 15, 40, 10, 0.9F, 0));

    TableTemplate noPitch = table;
    noPitch.rowPitch = 0;
    CHECK(parseTableRows(noPitch, Size{300, 200}, blocks, TableParseOptions{}).empty());

    TableTemplate noHeight = table;
    noHeight.rowHeight = 0;
    CHECK(parseTableRows(noHeight, Size{300, 200}, blocks, TableParseOptions{}).empty());

    TableTemplate noFields = table;
    noFields.fields.clear();
    CHECK(parseTableRows(noFields, Size{300, 200}, blocks, TableParseOptions{}).empty());

    CHECK(parseTableRows(table, Size{0, 200}, blocks, TableParseOptions{}).empty());
    CHECK(parseTableRows(table, Size{300, 0}, blocks, TableParseOptions{}).empty());

    TableTemplate belowRegion = table;
    belowRegion.firstRowY = 300;
    CHECK(parseTableRows(belowRegion, Size{300, 200}, blocks, TableParseOptions{}).empty());

    TableParseOptions limited;
    limited.maxRows = 1;
    std::vector<OcrTextBlock> twoRows = blocks;
    twoRows.push_back(makeBlock("600001", 10, 45, 40, 10, 0.9F, 1));
    const std::vector<TableRowResult> capped = parseTableRows(table, Size{300, 200}, twoRows, limited);
    CHECK_EQ(capped.size(), static_cast<std::size_t>(1));
}

TEST(region_table_parser_analyze_block_colors_keeps_block_count) {
    Image invalid;
    std::vector<OcrTextBlock> blocks;
    blocks.push_back(makeBlock("600000", 0, 0, 10, 10, 0.9F, 0));
    blocks.push_back(makeBlock("600001", 20, 0, 10, 10, 0.9F, 1));
    FontColorConfig config;
    const std::vector<FontColorResult> colors = analyzeBlockColors(invalid, blocks, config);
    CHECK_EQ(colors.size(), static_cast<std::size_t>(2));
}

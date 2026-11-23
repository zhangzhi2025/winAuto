#include "testing.h"

#include "StockRowAssembler.h"

using namespace stock_monitor;

namespace {
OcrTextBlock block(const char* text, Rect bounds, std::size_t order) {
    OcrTextBlock result;
    result.text = text;
    result.bounds = bounds;
    result.order = order;
    result.detectionConfidence = 0.95F;
    result.recognitionConfidence = 0.92F;
    return result;
}
}  // namespace

TEST(stock_row_assembler_groups_by_vertical_geometry_and_columns) {
    const std::vector<OcrTextBlock> blocks = {
        block("名称A", {30, 11, 25, 12}, 1),
        block("600001", {5, 10, 20, 12}, 0),
        block("名称B", {30, 40, 25, 12}, 3),
        block("600002", {5, 41, 20, 12}, 2),
        block("名称C", {30, 68, 25, 14}, 6),
        block("600003", {5, 70, 20, 8}, 5),
        block("涨幅", {80, 10, 12, 12}, 4),
    };
    const auto result = assembleRows(blocks, 100, TableLayoutConfig{});
    CHECK_EQ(result.rows.size(), static_cast<std::size_t>(3));
    CHECK_EQ(result.unassignedBlocks.size(), static_cast<std::size_t>(0));
    CHECK_EQ(result.rows[0].status, RowStatus::Ready);
    CHECK_EQ(result.rows[0].codeBlocks.size(), static_cast<std::size_t>(1));
    CHECK_EQ(result.rows[0].nameBlocks.size(), static_cast<std::size_t>(1));
    CHECK_EQ(result.rows[0].otherBlocks.size(), static_cast<std::size_t>(1));
    CHECK_EQ(result.rows[1].status, RowStatus::Ready);
    CHECK_EQ(result.rows[2].status, RowStatus::Ready);
}

TEST(stock_row_assembler_preserves_ambiguous_and_incomplete_rows) {
    const std::vector<OcrTextBlock> blocks = {
        block("600001", {5, 10, 15, 10}, 0),
        block("600002", {5, 11, 15, 10}, 1),
        block("名称", {30, 10, 20, 10}, 2),
        block("孤立", {30, 50, 20, 10}, 3),
        block("越界", {95, 70, 10, 10}, 4),
    };
    const auto result = assembleRows(blocks, 100, TableLayoutConfig{});
    CHECK_EQ(result.rows.size(), static_cast<std::size_t>(2));
    CHECK_EQ(result.rows[0].status, RowStatus::Ambiguous);
    CHECK_EQ(result.rows[1].status, RowStatus::Incomplete);
    CHECK_EQ(result.unassignedBlocks.size(), static_cast<std::size_t>(1));
}

TEST(stock_row_assembler_rejects_invalid_layout_without_guessing) {
    TableLayoutConfig invalid;
    invalid.nameColumn.minX = 0.1F;
    const auto result = assembleRows({block("123", {1, 1, 4, 4}, 0)}, 100, invalid);
    CHECK_EQ(result.rows.size(), static_cast<std::size_t>(0));
    CHECK_EQ(result.unassignedBlocks.size(), static_cast<std::size_t>(1));
}

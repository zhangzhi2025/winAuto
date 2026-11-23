#include "testing.h"

#include "StockClassifier.h"

using namespace stock_monitor;

namespace {
FontColorResult known(StockFontColor color) {
    FontColorResult result;
    result.valid = true;
    result.color = color;
    result.confidence = 0.95F;
    return result;
}
}  // namespace

TEST(stock_classifier_keeps_board_and_follow_dimensions_independent) {
    auto result = classifyColors(known(StockFontColor::Red), {}, false);
    CHECK_EQ(result.boardStage, BoardStage::ThirdBoardPlus);
    CHECK_EQ(result.followStatus, FollowStatus::Unknown);
    CHECK_EQ(result.tags.size(), static_cast<std::size_t>(1));
    CHECK_EQ(result.tags[0], std::string("BOARD_3_PLUS"));

    result = classifyColors(known(StockFontColor::White), {}, false);
    CHECK_EQ(result.boardStage, BoardStage::Unknown);
    CHECK_EQ(result.followStatus, FollowStatus::Watchlist);
    CHECK_EQ(result.tags[0], std::string("WATCHLIST"));

    result = classifyColors(known(StockFontColor::Yellow), {}, false);
    CHECK_EQ(result.boardStage, BoardStage::Unknown);
    CHECK_EQ(result.followStatus, FollowStatus::NotFollowed);
}

TEST(stock_classifier_maps_remaining_colors_and_detects_conflicts) {
    CHECK_EQ(classifyColors(known(StockFontColor::Orange), {}, false).boardStage, BoardStage::FirstBoard);
    CHECK_EQ(classifyColors(known(StockFontColor::Magenta), {}, false).boardStage, BoardStage::SecondBoard);
    CHECK_EQ(classifyColors({}, {}, false).status, RowStatus::NeedsReview);
    CHECK_EQ(classifyColors(known(StockFontColor::Red), known(StockFontColor::Orange), true).status,
             RowStatus::ColorConflict);
}

#include "StockClassifier.h"

namespace stock_monitor {

StockClassification classifyColors(const FontColorResult& nameColor,
                                   const FontColorResult& codeColor,
                                   bool hasReliableCodeColor) {
    StockClassification result;
    result.colorConfidence = nameColor.confidence;
    result.colorRecognized = nameColor.valid && nameColor.color != StockFontColor::Unknown;
    if (result.colorRecognized) {
        switch (nameColor.color) {
            case StockFontColor::Red:
                result.boardStage = BoardStage::ThirdBoardPlus;
                result.tags.emplace_back("BOARD_3_PLUS");
                break;
            case StockFontColor::Orange:
                result.boardStage = BoardStage::FirstBoard;
                result.tags.emplace_back("BOARD_1");
                break;
            case StockFontColor::Magenta:
                result.boardStage = BoardStage::SecondBoard;
                result.tags.emplace_back("BOARD_2");
                break;
            case StockFontColor::White:
                result.followStatus = FollowStatus::Watchlist;
                result.tags.emplace_back("WATCHLIST");
                break;
            case StockFontColor::Yellow:
                result.followStatus = FollowStatus::NotFollowed;
                result.tags.emplace_back("NOT_FOLLOWED");
                break;
            default:
                break;
        }
    }
    if (hasReliableCodeColor && codeColor.valid && codeColor.color != StockFontColor::Unknown &&
        result.colorRecognized && codeColor.color != nameColor.color) {
        result.status = RowStatus::ColorConflict;
        return result;
    }
    result.status = result.colorRecognized ? RowStatus::Ready : RowStatus::NeedsReview;
    return result;
}

}  // namespace stock_monitor

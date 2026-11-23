#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace stock_monitor {

struct Point {
    int x = 0;
    int y = 0;
};

struct Rect {
    int x = 0;
    int y = 0;
    int width = 0;
    int height = 0;

    int right() const { return x + width; }
    int bottom() const { return y + height; }
    int centerX() const { return x + width / 2; }
    int centerY() const { return y + height / 2; }
    bool empty() const { return width <= 0 || height <= 0; }
};

struct Image {
    int width = 0;
    int height = 0;
    std::size_t stride = 0;
    std::vector<std::uint8_t> rgba;

    bool valid() const;
};

enum class StockFontColor { Unknown, Red, White, Orange, Yellow, Magenta };
enum class BoardStage { Unknown, FirstBoard, SecondBoard, ThirdBoardPlus };
enum class FollowStatus { Unknown, Watchlist, NotFollowed };
enum class RowStatus { Ready, Incomplete, Ambiguous, ColorConflict, NeedsReview };
enum class AnalysisError {
    None,
    InvalidImage,
    InvalidConfiguration,
    InvalidOcrResult,
    OcrUnavailable,
    Cancelled,
    InferenceFailed
};

struct OcrTextBlock {
    std::string text;
    Rect bounds;
    std::array<Point, 4> quad{};
    float detectionConfidence = 0.0F;
    float recognitionConfidence = 0.0F;
    std::size_t order = 0;
};

struct FontColorResult {
    StockFontColor color = StockFontColor::Unknown;
    std::array<int, 3> dominantRgb{};
    float averageHue = 0.0F;
    float averageSaturation = 0.0F;
    float averageValue = 0.0F;
    Rect sourceBounds;
    std::size_t foregroundPixelCount = 0;
    std::size_t dominantPixelCount = 0;
    float dominantRatio = 0.0F;
    float confidence = 0.0F;
    bool valid = false;
    bool clipped = false;
};

struct StockClassification {
    BoardStage boardStage = BoardStage::Unknown;
    FollowStatus followStatus = FollowStatus::Unknown;
    std::vector<std::string> tags;
    bool colorRecognized = false;
    float colorConfidence = 0.0F;
    RowStatus status = RowStatus::NeedsReview;
};

struct StockRowCandidate {
    Rect rowBounds;
    std::vector<OcrTextBlock> codeBlocks;
    std::vector<OcrTextBlock> nameBlocks;
    std::vector<OcrTextBlock> otherBlocks;
    FontColorResult nameColor;
    FontColorResult codeColor;
    StockClassification classification;
    RowStatus status = RowStatus::Incomplete;
};

struct AnalysisResult {
    AnalysisError error = AnalysisError::None;
    std::string message;
    std::vector<OcrTextBlock> blocks;
    std::vector<OcrTextBlock> unassignedBlocks;
    std::vector<StockRowCandidate> rows;
};

const char* toString(StockFontColor color);
const char* toString(BoardStage stage);
const char* toString(FollowStatus status);
const char* toString(RowStatus status);
const char* toString(AnalysisError error);

}  // namespace stock_monitor

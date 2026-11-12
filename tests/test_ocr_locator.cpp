#include "core/OcrMatch.h"
#include "testing.h"

using namespace wa;

namespace {

OcrTextCandidate candidate(std::string text, int x, int y, double confidence = 0.9) {
    OcrTextCandidate result;
    result.text = std::move(text);
    result.screenBounds = Rect{x, y, 0, 0};
    result.recognitionConfidence = confidence;
    return result;
}

}  // namespace

TEST(ocr_matching_applies_confidence_keyword_and_text_filters) {
    OcrLocator locator;
    locator.text = "Submit";
    locator.match = Match::Contains;
    locator.keyword = "Sub";
    locator.minConfidence = 0.8;
    CHECK(matchesOcrText(locator, candidate("Submit now", 1, 1, 0.9)));
    CHECK(!matchesOcrText(locator, candidate("Cancel", 1, 1, 0.99)));
    CHECK(!matchesOcrText(locator, candidate("Submit", 1, 1, 0.79)));
    CHECK(!matchesOcrText(locator, candidate("Submarine", 1, 1, 0.99)));
    locator.match = Match::Exact;
    CHECK(!matchesOcrText(locator, candidate("Submit now", 1, 1, 0.99)));
    CHECK(matchesOcrText(locator, candidate("Submit", 1, 1, 0.99)));
}

TEST(ocr_direction_classifies_all_eight_sectors_and_center) {
    const Rect client{0, 0, 100, 100};
    const struct {
        OcrDirection direction;
        int x;
        int y;
    } cases[] = {
        {OcrDirection::Left, 10, 50},       {OcrDirection::LeftFront, 20, 20},
        {OcrDirection::Front, 50, 10},      {OcrDirection::RightFront, 80, 20},
        {OcrDirection::Right, 90, 50},      {OcrDirection::RightBack, 80, 80},
        {OcrDirection::Back, 50, 90},       {OcrDirection::LeftBack, 20, 80}};
    for (const auto& test : cases) {
        CHECK(matchesOcrDirection(test.direction, client, Rect{test.x, test.y, 0, 0}));
    }
    CHECK(!matchesOcrDirection(OcrDirection::Right, client, Rect{50, 50, 0, 0}));
    CHECK(matchesOcrDirection(OcrDirection::RightBack, client, Rect{62, 55, 0, 0}));
    CHECK(!matchesOcrDirection(OcrDirection::Right, client, Rect{62, 55, 0, 0}));
    const Rect offCenterClient{1000, 2000, 100, 100};
    CHECK(matchesOcrDirection(OcrDirection::Right, offCenterClient, Rect{1090, 2050, 0, 0}));
    const Rect boundaryClient{0, 0, 5000, 5000};
    CHECK(matchesOcrDirection(OcrDirection::RightBack, boundaryClient, Rect{4914, 3500, 0, 0}));
    CHECK(!matchesOcrDirection(OcrDirection::Right, boundaryClient, Rect{4914, 3500, 0, 0}));
    CHECK(matchesOcrDirection(OcrDirection::Right, boundaryClient, Rect{4915, 3500, 0, 0}));
    CHECK(!matchesOcrDirection(OcrDirection::RightBack, boundaryClient, Rect{4915, 3500, 0, 0}));
}

TEST(ocr_filter_preserves_multiple_candidates_in_deterministic_order) {
    OcrRecognition recognition;
    recognition.clientBounds = Rect{100, 200, 400, 200};
    recognition.candidates = {candidate("target", 400, 300), candidate("target", 350, 300)};
    OcrLocator locator;
    locator.text = "target";
    locator.direction = OcrDirection::Right;
    const auto matches = filterOcrCandidates(locator, recognition);
    CHECK_EQ(matches.size(), size_t(2));
    CHECK_EQ(matches[0].screenBounds.x, 350);
    CHECK_EQ(matches[1].screenBounds.x, 400);
}

#include "core/OcrMatch.h"

#include <algorithm>
#include <cmath>

namespace wa {
namespace {

OcrDirection directionFor(const Rect& clientBounds, const Rect& textBounds) {
    const Point origin = clientBounds.center();
    const Point target = textBounds.center();
    double degrees = std::atan2(static_cast<double>(target.y - origin.y),
                                static_cast<double>(target.x - origin.x)) *
                     180.0 / 3.14159265358979323846;
    if (degrees < 0.0) degrees += 360.0;
    const int sector = static_cast<int>(std::floor((degrees + 22.5) / 45.0)) % 8;
    static const OcrDirection directions[] = {
        OcrDirection::Right,      OcrDirection::RightBack, OcrDirection::Back,     OcrDirection::LeftBack,
        OcrDirection::Left,       OcrDirection::LeftFront, OcrDirection::Front,    OcrDirection::RightFront};
    return directions[sector];
}

}  // namespace

bool matchesOcrText(const OcrLocator& locator, const OcrTextCandidate& candidate) {
    if (candidate.recognitionConfidence < locator.minConfidence) return false;
    if (!locator.keyword.empty() &&
        (candidate.text.size() < locator.keyword.size() ||
         candidate.text.compare(0, locator.keyword.size(), locator.keyword) != 0)) {
        return false;
    }
    if (locator.text.empty()) return true;
    if (locator.match == Match::Exact) return candidate.text == locator.text;
    return candidate.text.find(locator.text) != std::string::npos;
}

bool matchesOcrDirection(OcrDirection direction, const Rect& clientBounds, const Rect& textBounds) {
    const Point clientCenter = clientBounds.center();
    const Point textCenter = textBounds.center();
    if (clientCenter.x == textCenter.x && clientCenter.y == textCenter.y) return false;
    return directionFor(clientBounds, textBounds) == direction;
}

std::vector<OcrTextCandidate> filterOcrCandidates(const OcrLocator& locator,
                                                  const OcrRecognition& recognition) {
    std::vector<OcrTextCandidate> matches;
    for (const auto& candidate : recognition.candidates) {
        if (matchesOcrText(locator, candidate) &&
            (!locator.direction ||
             matchesOcrDirection(*locator.direction, recognition.clientBounds, candidate.screenBounds))) {
            matches.push_back(candidate);
        }
    }
    std::stable_sort(matches.begin(), matches.end(), [](const OcrTextCandidate& a, const OcrTextCandidate& b) {
        if (a.screenBounds.y != b.screenBounds.y) return a.screenBounds.y < b.screenBounds.y;
        if (a.screenBounds.x != b.screenBounds.x) return a.screenBounds.x < b.screenBounds.x;
        return a.order < b.order;
    });
    return matches;
}

}  // namespace wa

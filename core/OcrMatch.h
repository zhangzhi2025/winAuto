#pragma once

#include "core/Interfaces.h"
#include "core/Model.h"

namespace wa {

bool matchesOcrText(const OcrLocator& locator, const OcrTextCandidate& candidate);
bool matchesOcrDirection(OcrDirection direction, const Rect& clientBounds, const Rect& textBounds);
std::vector<OcrTextCandidate> filterOcrCandidates(const OcrLocator& locator,
                                                  const OcrRecognition& recognition);

}  // namespace wa

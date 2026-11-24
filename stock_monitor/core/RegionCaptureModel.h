#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "RegionConfig.h"
#include "StockModel.h"

namespace stock_monitor {

enum class CaptureStatus { Success, WindowUnavailable, ClientSizeMismatch, CaptureFailed, Cancelled };
enum class RegionOcrStatus { NotRun, Success, Failed, Cancelled };
enum class FieldStatus { Parsed, Missing, InvalidFormat, Ambiguous, LowConfidence };
enum class TableRowStatus { Complete, Incomplete, Ambiguous };
enum class SessionState { Stopped, WaitingForDailyStart, Running, Ended, Failed };
// What a result panel may show for one region. A failed cycle is always shown as
// a failure and never as an empty success.
enum class ResultAvailability { None, CurrentSuccess, CurrentFailure, FailureWithStaleSuccess };

// One configured field of one table row. A failed parse keeps rawText and leaves
// the typed value empty so that a zero is never invented.
struct TypedFieldValue {
    std::string fieldId;
    FieldValueType valueType = FieldValueType::Text;
    bool hasRawText = false;
    std::string rawText;
    bool hasText = false;
    std::string textValue;
    bool hasInteger = false;
    long long integerValue = 0;
    bool hasNumber = false;
    double numberValue = 0.0;
    bool hasConfidence = false;
    float ocrConfidence = 0.0F;
    bool hasSourceBounds = false;
    Rect sourceBounds;
    FieldStatus status = FieldStatus::Missing;
    std::string message;
    bool fontColorAnalyzed = false;
    FontColorResult fontColor;
};

struct TableRowResult {
    int rowIndex = 0;
    Rect bounds;
    std::vector<TypedFieldValue> fields;
    TableRowStatus status = TableRowStatus::Incomplete;
};

// Result of one region capture plus optional OCR/table interpretation. The image
// is kept in memory only and is never serialized.
struct RegionCaptureResult {
    std::string sessionId;
    std::string regionId;
    std::string capturedAt;
    Size clientSize;
    Size imageSize;
    CaptureStatus captureStatus = CaptureStatus::CaptureFailed;
    RegionOcrStatus ocrStatus = RegionOcrStatus::NotRun;
    std::vector<OcrTextBlock> blocks;
    std::vector<TableRowResult> rows;
    std::uint64_t skippedTicks = 0;
    std::string message;
    bool hasImage = false;
    Image image;

    bool succeeded() const {
        return captureStatus == CaptureStatus::Success && ocrStatus != RegionOcrStatus::Failed &&
               ocrStatus != RegionOcrStatus::Cancelled;
    }
};

// Per-region runtime status for the session table. Timestamps are scheduler
// milliseconds; the UI decides how to render them. Never persisted.
struct RegionRuntimeStatus {
    std::string regionId;
    bool hasAttempt = false;
    std::int64_t lastAttemptMs = 0;
    bool hasSuccess = false;
    std::int64_t lastSuccessMs = 0;
    std::int64_t nextDueMs = 0;
    bool inFlight = false;
    std::uint64_t skippedTicks = 0;
    std::uint64_t completedRuns = 0;
    std::uint64_t failedRuns = 0;
    std::string lastError;
};

// Keeps the newest attempt and the newest success of one region so that a failed
// cycle can replace the visible status while the earlier success stays visible
// with its own timestamp and a stale marker. Images stay in memory only.
class RegionResultTracker {
public:
    void record(RegionCaptureResult result);
    void clear();

    bool hasAttempt() const { return hasAttempt_; }
    bool hasSuccess() const { return hasSuccess_; }
    const RegionCaptureResult& lastAttempt() const { return lastAttempt_; }
    const RegionCaptureResult& lastSuccess() const { return lastSuccess_; }
    // True only when the retained success predates the newest failed attempt.
    bool staleSuccess() const;
    ResultAvailability availability() const;

private:
    bool hasAttempt_ = false;
    RegionCaptureResult lastAttempt_;
    bool hasSuccess_ = false;
    RegionCaptureResult lastSuccess_;
};

const char* toString(CaptureStatus status);
const char* toString(RegionOcrStatus status);
const char* toString(FieldStatus status);
const char* toString(TableRowStatus status);
const char* toString(SessionState state);
const char* toString(ResultAvailability availability);

}  // namespace stock_monitor

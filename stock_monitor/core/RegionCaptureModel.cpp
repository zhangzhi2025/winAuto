#include "RegionCaptureModel.h"

#include <utility>

namespace stock_monitor {

const char* toString(CaptureStatus status) {
    switch(status) {
        case CaptureStatus::Success:
            return "success";
        case CaptureStatus::WindowUnavailable:
            return "window_unavailable";
        case CaptureStatus::ClientSizeMismatch:
            return "client_size_mismatch";
        case CaptureStatus::CaptureFailed:
            return "capture_failed";
        case CaptureStatus::Cancelled:
            return "cancelled";
    }
    return "capture_failed";
}

const char* toString(RegionOcrStatus status) {
    switch(status) {
        case RegionOcrStatus::NotRun:
            return "not_run";
        case RegionOcrStatus::Success:
            return "success";
        case RegionOcrStatus::Failed:
            return "failed";
        case RegionOcrStatus::Cancelled:
            return "cancelled";
    }
    return "not_run";
}

const char* toString(FieldStatus status) {
    switch(status) {
        case FieldStatus::Parsed:
            return "parsed";
        case FieldStatus::Missing:
            return "missing";
        case FieldStatus::InvalidFormat:
            return "invalid_format";
        case FieldStatus::Ambiguous:
            return "ambiguous";
        case FieldStatus::LowConfidence:
            return "low_confidence";
    }
    return "missing";
}

const char* toString(TableRowStatus status) {
    switch(status) {
        case TableRowStatus::Complete:
            return "complete";
        case TableRowStatus::Incomplete:
            return "incomplete";
        case TableRowStatus::Ambiguous:
            return "ambiguous";
    }
    return "incomplete";
}

const char* toString(SessionState state) {
    switch(state) {
        case SessionState::Stopped:
            return "stopped";
        case SessionState::WaitingForDailyStart:
            return "waiting_for_daily_start";
        case SessionState::Running:
            return "running";
        case SessionState::Ended:
            return "ended";
        case SessionState::Failed:
            return "failed";
    }
    return "stopped";
}

const char* toString(ResultAvailability availability) {
    switch(availability) {
        case ResultAvailability::None:
            return "none";
        case ResultAvailability::CurrentSuccess:
            return "current_success";
        case ResultAvailability::CurrentFailure:
            return "current_failure";
        case ResultAvailability::FailureWithStaleSuccess:
            return "failure_with_stale_success";
    }
    return "none";
}

void RegionResultTracker::record(RegionCaptureResult result) {
    const bool succeeded = result.succeeded();
    lastAttempt_ = result;
    hasAttempt_ = true;
    if(succeeded) {
        lastSuccess_ = std::move(result);
        hasSuccess_ = true;
    }
}

void RegionResultTracker::clear() {
    hasAttempt_ = false;
    hasSuccess_ = false;
    lastAttempt_ = RegionCaptureResult{};
    lastSuccess_ = RegionCaptureResult{};
}

bool RegionResultTracker::staleSuccess() const {
    return hasSuccess_ && hasAttempt_ && !lastAttempt_.succeeded();
}

ResultAvailability RegionResultTracker::availability() const {
    if(!hasAttempt_) {
        return ResultAvailability::None;
    }
    if(lastAttempt_.succeeded()) {
        return ResultAvailability::CurrentSuccess;
    }
    return hasSuccess_ ? ResultAvailability::FailureWithStaleSuccess
                       : ResultAvailability::CurrentFailure;
}

}  // namespace stock_monitor

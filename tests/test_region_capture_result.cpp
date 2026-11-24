#include "testing.h"

#include <string>
#include <utility>

#include "RegionCaptureModel.h"

using namespace stock_monitor;

namespace {

RegionCaptureResult makeResult(const std::string& regionId,
                               CaptureStatus captureStatus,
                               RegionOcrStatus ocrStatus,
                               const std::string& capturedAt) {
    RegionCaptureResult result;
    result.sessionId = "session-1";
    result.regionId = regionId;
    result.capturedAt = capturedAt;
    result.clientSize = Size{1280, 720};
    result.imageSize = Size{100, 50};
    result.captureStatus = captureStatus;
    result.ocrStatus = ocrStatus;
    return result;
}

RegionCaptureResult successResult(const std::string& capturedAt) {
    return makeResult("left", CaptureStatus::Success, RegionOcrStatus::Success, capturedAt);
}

RegionCaptureResult failedCaptureResult(const std::string& capturedAt) {
    return makeResult("left", CaptureStatus::CaptureFailed, RegionOcrStatus::NotRun, capturedAt);
}

RegionCaptureResult emptyOcrResult(const std::string& capturedAt) {
    return makeResult("left", CaptureStatus::Success, RegionOcrStatus::Success, capturedAt);
}

}  // namespace

TEST(region_result_empty_ocr_is_a_success) {
    const RegionCaptureResult result = emptyOcrResult("2024-05-06T09:30:01");
    CHECK(result.blocks.empty());
    CHECK(result.rows.empty());
    CHECK(result.succeeded());
}

TEST(region_result_capture_failure_is_not_success) {
    const RegionCaptureResult result = failedCaptureResult("2024-05-06T09:30:01");
    CHECK(!result.succeeded());
}

TEST(region_result_ocr_failure_is_not_success) {
    const RegionCaptureResult result =
        makeResult("left", CaptureStatus::Success, RegionOcrStatus::Failed, "2024-05-06T09:30:01");
    CHECK(!result.succeeded());
}

TEST(region_result_cancelled_capture_is_not_success) {
    const RegionCaptureResult result =
        makeResult("left", CaptureStatus::Cancelled, RegionOcrStatus::Cancelled, "2024-05-06T09:30:01");
    CHECK(!result.succeeded());
    CHECK(!makeResult("left", CaptureStatus::Success, RegionOcrStatus::Cancelled, "x").succeeded());
}

TEST(region_result_tracker_reports_none_before_any_attempt) {
    RegionResultTracker tracker;
    CHECK(!tracker.hasAttempt());
    CHECK(!tracker.hasSuccess());
    CHECK(!tracker.staleSuccess());
    CHECK_EQ(tracker.availability(), ResultAvailability::None);
}

TEST(region_result_tracker_keeps_current_success) {
    RegionResultTracker tracker;
    tracker.record(successResult("2024-05-06T09:30:01"));
    CHECK(tracker.hasAttempt());
    CHECK(tracker.hasSuccess());
    CHECK(!tracker.staleSuccess());
    CHECK_EQ(tracker.availability(), ResultAvailability::CurrentSuccess);
    CHECK_EQ(tracker.lastAttempt().capturedAt, std::string("2024-05-06T09:30:01"));
    CHECK_EQ(tracker.lastSuccess().capturedAt, std::string("2024-05-06T09:30:01"));
}

TEST(region_result_tracker_reports_first_failure_without_prior_success) {
    RegionResultTracker tracker;
    tracker.record(failedCaptureResult("2024-05-06T09:30:02"));
    CHECK(tracker.hasAttempt());
    CHECK(!tracker.hasSuccess());
    CHECK(!tracker.staleSuccess());
    CHECK_EQ(tracker.availability(), ResultAvailability::CurrentFailure);
}

TEST(region_result_tracker_marks_retained_success_stale_after_failure) {
    RegionResultTracker tracker;
    tracker.record(successResult("2024-05-06T09:30:01"));
    tracker.record(failedCaptureResult("2024-05-06T09:30:05"));

    CHECK(tracker.hasSuccess());
    CHECK(tracker.staleSuccess());
    CHECK_EQ(tracker.availability(), ResultAvailability::FailureWithStaleSuccess);
    // The failed cycle owns the current status and never becomes an empty success.
    CHECK_EQ(tracker.lastAttempt().capturedAt, std::string("2024-05-06T09:30:05"));
    CHECK(!tracker.lastAttempt().succeeded());
    // The retained success keeps its own timestamp so the UI can show the age.
    CHECK_EQ(tracker.lastSuccess().capturedAt, std::string("2024-05-06T09:30:01"));
}

TEST(region_result_tracker_clears_stale_marker_after_recovery) {
    RegionResultTracker tracker;
    tracker.record(successResult("2024-05-06T09:30:01"));
    tracker.record(failedCaptureResult("2024-05-06T09:30:05"));
    tracker.record(successResult("2024-05-06T09:30:09"));

    CHECK(!tracker.staleSuccess());
    CHECK_EQ(tracker.availability(), ResultAvailability::CurrentSuccess);
    CHECK_EQ(tracker.lastSuccess().capturedAt, std::string("2024-05-06T09:30:09"));
}

TEST(region_result_tracker_keeps_empty_ocr_success_apart_from_failure) {
    RegionResultTracker empty;
    empty.record(emptyOcrResult("2024-05-06T09:30:01"));
    CHECK_EQ(empty.availability(), ResultAvailability::CurrentSuccess);

    RegionResultTracker failed;
    failed.record(makeResult("left", CaptureStatus::Success, RegionOcrStatus::Failed, "2024-05-06T09:30:01"));
    CHECK_EQ(failed.availability(), ResultAvailability::CurrentFailure);
    CHECK(!failed.staleSuccess());
}

TEST(region_result_tracker_keeps_only_one_success_and_one_attempt) {
    RegionResultTracker tracker;
    tracker.record(successResult("2024-05-06T09:30:01"));
    tracker.record(successResult("2024-05-06T09:30:03"));
    tracker.record(successResult("2024-05-06T09:30:05"));
    CHECK_EQ(tracker.lastAttempt().capturedAt, std::string("2024-05-06T09:30:05"));
    CHECK_EQ(tracker.lastSuccess().capturedAt, std::string("2024-05-06T09:30:05"));
}

TEST(region_result_tracker_clear_resets_both_slots) {
    RegionResultTracker tracker;
    tracker.record(successResult("2024-05-06T09:30:01"));
    tracker.record(failedCaptureResult("2024-05-06T09:30:05"));
    tracker.clear();
    CHECK(!tracker.hasAttempt());
    CHECK(!tracker.hasSuccess());
    CHECK(!tracker.staleSuccess());
    CHECK_EQ(tracker.availability(), ResultAvailability::None);
    CHECK(tracker.lastSuccess().capturedAt.empty());
}

TEST(region_result_time_formats) {
    CHECK_EQ(std::string(toString(ResultAvailability::None)), std::string("none"));
    CHECK_EQ(std::string(toString(ResultAvailability::CurrentSuccess)), std::string("current_success"));
    CHECK_EQ(std::string(toString(ResultAvailability::CurrentFailure)), std::string("current_failure"));
    CHECK_EQ(std::string(toString(ResultAvailability::FailureWithStaleSuccess)),
             std::string("failure_with_stale_success"));
}

TEST(region_result_status_formats) {
    CHECK_EQ(std::string(toString(CaptureStatus::ClientSizeMismatch)), std::string("client_size_mismatch"));
    CHECK_EQ(std::string(toString(RegionOcrStatus::NotRun)), std::string("not_run"));
    CHECK_EQ(std::string(toString(FieldStatus::LowConfidence)), std::string("low_confidence"));
    CHECK_EQ(std::string(toString(TableRowStatus::Ambiguous)), std::string("ambiguous"));
    CHECK_EQ(std::string(toString(SessionState::WaitingForDailyStart)),
             std::string("waiting_for_daily_start"));
}

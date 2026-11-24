#include "testing.h"

#include <cstdint>
#include <string>
#include <vector>

#include "RegionConfig.h"
#include "RegionScheduler.h"

using namespace stock_monitor;

namespace {

constexpr int kWindowStartSeconds = 9 * 3600 + 30 * 60;  // 09:30:00
constexpr int kWindowEndSeconds = 15 * 3600;             // 15:00:00

CaptureRegion ocrRegion(const std::string& id, int intervalMs, bool enabled) {
    CaptureRegion region;
    region.id = id;
    region.name = id;
    region.rect = Rect{0, 0, 100, 50};
    region.intervalMs = intervalMs;
    region.enabled = enabled;
    region.mode = CaptureRegionMode::Ocr;
    return region;
}

RegionCaptureConfig makeConfig() {
    RegionCaptureConfig config;
    config.schemaVersion = kRegionCaptureSchemaVersion;
    config.configId = "cfg";
    config.name = "watch";
    config.targetWindow.title = "Trade";
    config.targetWindow.processName = "trade.exe";
    config.referenceClientSize = Size{1024, 768};
    config.referenceDpi = 96;
    config.dailyWindow.start = "09:30:00";
    config.dailyWindow.end = "15:00:00";
    config.regions.push_back(ocrRegion("r1", 1000, true));
    config.regions.push_back(ocrRegion("r2", 500, true));
    config.regions.push_back(ocrRegion("r3", 800, false));
    return config;
}

ScheduleInput at(std::int64_t monotonicMs, int secondsOfDay) {
    ScheduleInput input;
    input.monotonicMs = monotonicMs;
    input.localSecondsOfDay = secondsOfDay;
    return input;
}

}  // namespace

TEST(region_scheduler_start_is_immediate_inside_the_window) {
    const RegionCaptureConfig config = makeConfig();
    RegionScheduler scheduler;
    CHECK_EQ(scheduler.state(), SessionState::Stopped);
    scheduler.start(config, at(1000, kWindowStartSeconds + 60));

    CHECK(scheduler.running());
    CHECK_EQ(scheduler.state(), SessionState::Running);
    CHECK_EQ(scheduler.sessionStartMs(), static_cast<std::int64_t>(1000));
    CHECK_EQ(scheduler.config().configId, std::string("cfg"));

    const std::vector<RegionRuntimeStatus> statuses = scheduler.statuses();
    CHECK_EQ(statuses.size(), static_cast<std::size_t>(2));
    CHECK_EQ(statuses[0].regionId, std::string("r1"));
    CHECK_EQ(statuses[1].regionId, std::string("r2"));
    for(const RegionRuntimeStatus& status : statuses) {
        CHECK(!status.hasAttempt);
        CHECK(!status.hasSuccess);
        CHECK(!status.inFlight);
        CHECK_EQ(status.nextDueMs, static_cast<std::int64_t>(1000));
    }
}

TEST(region_scheduler_start_rejects_invalid_configurations) {
    RegionCaptureConfig crossing = makeConfig();
    crossing.dailyWindow.start = "15:00:00";
    crossing.dailyWindow.end = "09:30:00";
    RegionScheduler crossingScheduler;
    crossingScheduler.start(crossing, at(0, kWindowStartSeconds + 60));
    CHECK_EQ(crossingScheduler.state(), SessionState::Failed);
    CHECK(!crossingScheduler.running());
    CHECK(crossingScheduler.statuses().empty());
    CHECK(crossingScheduler.poll(at(0, kWindowStartSeconds + 60)).empty());

    RegionCaptureConfig malformedTime = makeConfig();
    malformedTime.dailyWindow.start = "9:30";
    RegionScheduler malformedScheduler;
    malformedScheduler.start(malformedTime, at(0, kWindowStartSeconds + 60));
    CHECK_EQ(malformedScheduler.state(), SessionState::Failed);

    RegionCaptureConfig allDisabled = makeConfig();
    for(CaptureRegion& region : allDisabled.regions) {
        region.enabled = false;
    }
    RegionScheduler disabledScheduler;
    disabledScheduler.start(allDisabled, at(0, kWindowStartSeconds + 60));
    CHECK_EQ(disabledScheduler.state(), SessionState::Failed);
    CHECK(disabledScheduler.statuses().empty());

    RegionCaptureConfig invalidRegion = makeConfig();
    invalidRegion.regions[0].intervalMs = 100;
    RegionScheduler invalidScheduler;
    invalidScheduler.start(invalidRegion, at(0, kWindowStartSeconds + 60));
    CHECK_EQ(invalidScheduler.state(), SessionState::Failed);
}

TEST(region_scheduler_poll_returns_each_due_region_once) {
    RegionScheduler scheduler;
    scheduler.start(makeConfig(), at(0, kWindowStartSeconds + 60));

    const std::vector<RegionScheduleDue> first = scheduler.poll(at(0, kWindowStartSeconds + 60));
    CHECK_EQ(first.size(), static_cast<std::size_t>(2));
    CHECK_EQ(first[0].regionId, std::string("r1"));
    CHECK_EQ(first[0].dueAtMs, static_cast<std::int64_t>(0));
    CHECK_EQ(first[1].regionId, std::string("r2"));

    const std::vector<RegionRuntimeStatus> statuses = scheduler.statuses();
    CHECK(statuses[0].hasAttempt);
    CHECK(statuses[0].inFlight);
    CHECK_EQ(statuses[0].lastAttemptMs, static_cast<std::int64_t>(0));
    CHECK_EQ(statuses[0].nextDueMs, static_cast<std::int64_t>(1000));
    CHECK_EQ(statuses[1].nextDueMs, static_cast<std::int64_t>(500));

    CHECK(scheduler.poll(at(0, kWindowStartSeconds + 60)).empty());
    CHECK(scheduler.poll(at(400, kWindowStartSeconds + 60)).empty());
    CHECK_EQ(scheduler.skippedTicks(), static_cast<std::uint64_t>(0));
}

TEST(region_scheduler_poll_honors_per_region_intervals) {
    RegionScheduler scheduler;
    scheduler.start(makeConfig(), at(0, kWindowStartSeconds + 60));
    const std::vector<RegionScheduleDue> first = scheduler.poll(at(0, kWindowStartSeconds + 60));
    CHECK_EQ(first.size(), static_cast<std::size_t>(2));
    scheduler.completeAttempt("r1", true, "");
    scheduler.completeAttempt("r2", true, "");

    const std::vector<RegionScheduleDue> second = scheduler.poll(at(500, kWindowStartSeconds + 60));
    CHECK_EQ(second.size(), static_cast<std::size_t>(1));
    CHECK_EQ(second[0].regionId, std::string("r2"));
    CHECK_EQ(second[0].dueAtMs, static_cast<std::int64_t>(500));

    const std::vector<RegionRuntimeStatus> statuses = scheduler.statuses();
    CHECK_EQ(statuses[0].completedRuns, 1);
    CHECK_EQ(statuses[1].completedRuns, 1);
    CHECK_EQ(statuses[0].lastAttemptMs, static_cast<std::int64_t>(0));
}

TEST(region_scheduler_poll_counts_in_flight_ticks_as_skipped) {
    RegionScheduler scheduler;
    scheduler.start(makeConfig(), at(0, kWindowStartSeconds + 60));
    CHECK_EQ(scheduler.poll(at(0, kWindowStartSeconds + 60)).size(), static_cast<std::size_t>(2));

    CHECK(scheduler.poll(at(1000, kWindowStartSeconds + 60)).empty());
    CHECK_EQ(scheduler.skippedTicks(), static_cast<std::uint64_t>(3));
    std::vector<RegionRuntimeStatus> statuses = scheduler.statuses();
    CHECK(statuses[0].inFlight);
    CHECK_EQ(statuses[0].nextDueMs, static_cast<std::int64_t>(2000));
    CHECK_EQ(statuses[0].skippedTicks, static_cast<std::uint64_t>(1));
    CHECK_EQ(statuses[1].nextDueMs, static_cast<std::int64_t>(1500));
    CHECK_EQ(statuses[1].skippedTicks, static_cast<std::uint64_t>(2));

    CHECK(scheduler.poll(at(2000, kWindowStartSeconds + 60)).empty());
    statuses = scheduler.statuses();
    CHECK_EQ(statuses[0].nextDueMs, static_cast<std::int64_t>(3000));
    CHECK_EQ(statuses[0].skippedTicks, static_cast<std::uint64_t>(2));

    scheduler.completeAttempt("r1", true, "");
    const std::vector<RegionScheduleDue> recovered = scheduler.poll(at(3000, kWindowStartSeconds + 60));
    CHECK_EQ(recovered.size(), static_cast<std::size_t>(1));
    CHECK_EQ(recovered[0].regionId, std::string("r1"));
    CHECK_EQ(recovered[0].dueAtMs, static_cast<std::int64_t>(3000));
}

TEST(region_scheduler_complete_attempt_records_success_and_failure) {
    RegionScheduler scheduler;
    scheduler.start(makeConfig(), at(0, kWindowStartSeconds + 60));
    CHECK_EQ(scheduler.poll(at(0, kWindowStartSeconds + 60)).size(), static_cast<std::size_t>(2));

    scheduler.completeAttempt("r1", true, "stale error");
    scheduler.completeAttempt("r2", false, "ocr failed");

    const std::vector<RegionRuntimeStatus> statuses = scheduler.statuses();
    CHECK(!statuses[0].inFlight);
    CHECK_EQ(statuses[0].completedRuns, 1);
    CHECK_EQ(statuses[0].failedRuns, 0);
    CHECK(statuses[0].hasSuccess);
    CHECK_EQ(statuses[0].lastSuccessMs, statuses[0].lastAttemptMs);
    CHECK(statuses[0].lastError.empty());

    CHECK(!statuses[1].inFlight);
    CHECK_EQ(statuses[1].completedRuns, 0);
    CHECK_EQ(statuses[1].failedRuns, 1);
    CHECK(!statuses[1].hasSuccess);
    CHECK_EQ(statuses[1].lastError, std::string("ocr failed"));
}

TEST(region_scheduler_ignores_unknown_and_repeated_completions) {
    RegionScheduler scheduler;
    scheduler.start(makeConfig(), at(0, kWindowStartSeconds + 60));
    scheduler.completeAttempt("r1", true, "");
    CHECK_EQ(scheduler.statuses()[0].completedRuns, 0);
    CHECK_EQ(scheduler.statuses()[0].failedRuns, 0);

    CHECK_EQ(scheduler.poll(at(0, kWindowStartSeconds + 60)).size(), static_cast<std::size_t>(2));
    scheduler.completeAttempt("missing", true, "");
    scheduler.completeAttempt("r1", true, "");
    scheduler.completeAttempt("r1", false, "ignored");
    const std::vector<RegionRuntimeStatus> statuses = scheduler.statuses();
    CHECK_EQ(statuses[0].completedRuns, 1);
    CHECK_EQ(statuses[0].failedRuns, 0);
    CHECK(statuses[0].lastError.empty());
}

TEST(region_scheduler_completion_does_not_shift_the_next_due_time) {
    RegionScheduler scheduler;
    scheduler.start(makeConfig(), at(0, kWindowStartSeconds + 60));
    CHECK_EQ(scheduler.poll(at(0, kWindowStartSeconds + 60)).size(), static_cast<std::size_t>(2));
    scheduler.completeAttempt("r1", true, "");
    scheduler.completeAttempt("r2", true, "");

    // r2 runs late because nothing prevents a late capture once the run is done,
    // but the completion time never moves r1's fixed due time.
    const std::vector<RegionScheduleDue> late = scheduler.poll(at(700, kWindowStartSeconds + 60));
    CHECK_EQ(late.size(), static_cast<std::size_t>(1));
    CHECK_EQ(late[0].regionId, std::string("r2"));
    CHECK_EQ(late[0].dueAtMs, static_cast<std::int64_t>(700));
    scheduler.completeAttempt("r2", true, "");

    CHECK_EQ(scheduler.skippedTicks(), static_cast<std::uint64_t>(0));
    CHECK_EQ(scheduler.statuses()[0].nextDueMs, static_cast<std::int64_t>(1000));

    const std::vector<RegionScheduleDue> next = scheduler.poll(at(1000, kWindowStartSeconds + 60));
    CHECK_EQ(next.size(), static_cast<std::size_t>(1));
    CHECK_EQ(next[0].regionId, std::string("r1"));
    CHECK_EQ(next[0].dueAtMs, static_cast<std::int64_t>(1000));
}

TEST(region_scheduler_waits_for_the_daily_start) {
    RegionScheduler scheduler;
    scheduler.start(makeConfig(), at(0, kWindowStartSeconds - 60));
    CHECK_EQ(scheduler.state(), SessionState::WaitingForDailyStart);
    CHECK(!scheduler.running());

    CHECK(scheduler.poll(at(1000, kWindowStartSeconds - 1)).empty());
    CHECK_EQ(scheduler.state(), SessionState::WaitingForDailyStart);
    CHECK_EQ(scheduler.skippedTicks(), static_cast<std::uint64_t>(0));

    const std::vector<RegionScheduleDue> opened = scheduler.poll(at(5000, kWindowStartSeconds));
    CHECK_EQ(scheduler.state(), SessionState::Running);
    CHECK_EQ(opened.size(), static_cast<std::size_t>(2));
    CHECK_EQ(opened[0].dueAtMs, static_cast<std::int64_t>(5000));
    const std::vector<RegionRuntimeStatus> statuses = scheduler.statuses();
    CHECK_EQ(statuses[0].lastAttemptMs, static_cast<std::int64_t>(5000));
    CHECK_EQ(scheduler.skippedTicks(), static_cast<std::uint64_t>(0));
}

TEST(region_scheduler_treats_the_end_boundary_as_closed) {
    RegionScheduler scheduler;
    scheduler.start(makeConfig(), at(0, kWindowStartSeconds + 60));
    CHECK_EQ(scheduler.poll(at(0, kWindowStartSeconds + 60)).size(), static_cast<std::size_t>(2));

    CHECK(scheduler.poll(at(1000, kWindowEndSeconds)).empty());
    CHECK_EQ(scheduler.state(), SessionState::Ended);
    CHECK(!scheduler.running());

    CHECK(scheduler.poll(at(2000, kWindowEndSeconds - 10)).empty());
    CHECK_EQ(scheduler.state(), SessionState::Ended);
    CHECK_EQ(scheduler.statuses().size(), static_cast<std::size_t>(2));
    CHECK_EQ(scheduler.statuses()[0].lastAttemptMs, static_cast<std::int64_t>(0));
}

TEST(region_scheduler_start_after_the_window_ends_immediately) {
    RegionScheduler scheduler;
    scheduler.start(makeConfig(), at(0, kWindowEndSeconds + 5));
    CHECK_EQ(scheduler.state(), SessionState::Ended);
    CHECK(scheduler.poll(at(100, kWindowEndSeconds + 6)).empty());
    CHECK_EQ(scheduler.state(), SessionState::Ended);
}

TEST(region_scheduler_stop_and_restart_reset_session_state) {
    RegionScheduler scheduler;
    scheduler.start(makeConfig(), at(0, kWindowStartSeconds + 60));
    CHECK_EQ(scheduler.poll(at(0, kWindowStartSeconds + 60)).size(), static_cast<std::size_t>(2));
    CHECK_EQ(scheduler.poll(at(1000, kWindowStartSeconds + 60)).size(), static_cast<std::size_t>(0));
    CHECK_EQ(scheduler.skippedTicks(), static_cast<std::uint64_t>(3));

    scheduler.stop();
    CHECK_EQ(scheduler.state(), SessionState::Stopped);
    CHECK(scheduler.statuses().empty());
    CHECK(scheduler.poll(at(2000, kWindowStartSeconds + 60)).empty());

    scheduler.start(makeConfig(), at(4000, kWindowStartSeconds + 60));
    CHECK_EQ(scheduler.state(), SessionState::Running);
    CHECK_EQ(scheduler.skippedTicks(), static_cast<std::uint64_t>(0));
    CHECK_EQ(scheduler.sessionStartMs(), static_cast<std::int64_t>(4000));
    const std::vector<RegionRuntimeStatus> statuses = scheduler.statuses();
    CHECK_EQ(statuses.size(), static_cast<std::size_t>(2));
    CHECK(!statuses[0].hasAttempt);
    CHECK(!statuses[0].inFlight);
    CHECK_EQ(statuses[0].nextDueMs, static_cast<std::int64_t>(4000));
}

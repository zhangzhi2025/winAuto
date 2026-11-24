#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "RegionCaptureModel.h"
#include "RegionConfig.h"

namespace stock_monitor {

// Injected time source: the scheduler never reads the system clock, so tests can
// drive it deterministically.
struct ScheduleInput {
    std::int64_t monotonicMs = 0;
    int localSecondsOfDay = 0;
};

struct RegionScheduleDue {
    std::string regionId;
    std::int64_t dueAtMs = 0;
};

// Deterministic per-region interval scheduler.
//
// Rules:
//   * The daily window is half open: [start, end). Outside it the session is
//     WaitingForDailyStart (before start) or Ended (at or after end).
//   * A region runs at most once at a time; a due tick while a run is in flight
//     is counted as a skipped tick instead of queueing another capture.
//   * Intervals are measured from the start of the previous attempt, so a slow
//     run never causes a burst of catch-up captures.
//   * No state carries over to the next day: once Ended, start() must be called
//     again.
class RegionScheduler {
   public:
    void start(const RegionCaptureConfig& config, const ScheduleInput& now);
    void stop();

    SessionState state() const { return state_; }
    bool running() const { return state_ == SessionState::Running; }

    // Advances the session state for the injected time and returns the regions
    // that must be captured now. Returned regions are marked in flight.
    std::vector<RegionScheduleDue> poll(const ScheduleInput& now);

    // Reports the outcome of an in flight run. Unknown or already completed
    // regions are ignored.
    void completeAttempt(const std::string& regionId, bool success, const std::string& error);

    std::vector<RegionRuntimeStatus> statuses() const;
    const RegionCaptureConfig& config() const { return config_; }
    std::int64_t sessionStartMs() const { return sessionStartMs_; }
    std::uint64_t skippedTicks() const { return skippedTicks_; }

   private:
    struct Slot {
        RegionRuntimeStatus status;
        int intervalMs = 0;
    };

    Slot* findSlot(const std::string& regionId);
    void advanceDue(Slot& slot, std::int64_t nowMs, bool countSkipped);

    SessionState state_ = SessionState::Stopped;
    RegionCaptureConfig config_;
    std::vector<Slot> slots_;
    int windowStartSeconds_ = 0;
    int windowEndSeconds_ = 0;
    std::int64_t sessionStartMs_ = 0;
    std::uint64_t skippedTicks_ = 0;
};

}  // namespace stock_monitor

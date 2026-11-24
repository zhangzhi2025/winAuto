#include "RegionScheduler.h"

#include <algorithm>

namespace stock_monitor {

void RegionScheduler::start(const RegionCaptureConfig& config, const ScheduleInput& now) {
    stop();
    config_ = config;
    int startSeconds = 0;
    int endSeconds = 0;
    if(!parseDailyTime(config.dailyWindow.start, startSeconds) ||
       !parseDailyTime(config.dailyWindow.end, endSeconds) || startSeconds >= endSeconds ||
       !validate(config).valid) {
        state_ = SessionState::Failed;
        return;
    }
    windowStartSeconds_ = startSeconds;
    windowEndSeconds_ = endSeconds;
    sessionStartMs_ = now.monotonicMs;
    skippedTicks_ = 0;
    slots_.clear();
    for(const CaptureRegion& region : config.regions) {
        if(!region.enabled) {
            continue;
        }
        Slot slot;
        slot.status.regionId = region.id;
        slot.status.nextDueMs = now.monotonicMs;
        slot.intervalMs = region.intervalMs;
        slots_.push_back(std::move(slot));
    }
    if(slots_.empty()) {
        state_ = SessionState::Failed;
        return;
    }
    if(now.localSecondsOfDay < windowStartSeconds_) {
        state_ = SessionState::WaitingForDailyStart;
    } else if(now.localSecondsOfDay >= windowEndSeconds_) {
        state_ = SessionState::Ended;
    } else {
        state_ = SessionState::Running;
        // The session starts immediately: the first capture of every region is
        // due as soon as start() returns.
        for(Slot& slot : slots_) {
            slot.status.nextDueMs = now.monotonicMs;
        }
    }
}

void RegionScheduler::stop() {
    slots_.clear();
    state_ = SessionState::Stopped;
}

RegionScheduler::Slot* RegionScheduler::findSlot(const std::string& regionId) {
    for(Slot& slot : slots_) {
        if(slot.status.regionId == regionId) {
            return &slot;
        }
    }
    return nullptr;
}

void RegionScheduler::advanceDue(Slot& slot, std::int64_t nowMs, bool countSkipped) {
    if(slot.intervalMs <= 0) {
        return;
    }
    if(slot.status.nextDueMs > nowMs) {
        return;
    }
    const std::int64_t interval = slot.intervalMs;
    const std::int64_t missed = (nowMs - slot.status.nextDueMs) / interval + 1;
    slot.status.nextDueMs += missed * interval;
    if(countSkipped) {
        const std::uint64_t skipped = static_cast<std::uint64_t>(missed);
        slot.status.skippedTicks += skipped;
        skippedTicks_ += skipped;
    }
}

std::vector<RegionScheduleDue> RegionScheduler::poll(const ScheduleInput& now) {
    std::vector<RegionScheduleDue> due;
    if(state_ == SessionState::Stopped || state_ == SessionState::Ended ||
       state_ == SessionState::Failed) {
        return due;
    }
    if(now.localSecondsOfDay >= windowEndSeconds_) {
        state_ = SessionState::Ended;
        return due;
    }
    if(now.localSecondsOfDay < windowStartSeconds_) {
        state_ = SessionState::WaitingForDailyStart;
        return due;
    }
    if(state_ == SessionState::WaitingForDailyStart) {
        // The window just opened: every region becomes due right away and no run
        // is carried over from before the window.
        state_ = SessionState::Running;
        for(Slot& slot : slots_) {
            slot.status.nextDueMs = now.monotonicMs;
        }
    }
    state_ = SessionState::Running;

    for(Slot& slot : slots_) {
        if(slot.status.inFlight) {
            if(slot.status.nextDueMs <= now.monotonicMs) {
                advanceDue(slot, now.monotonicMs, true);
            }
            continue;
        }
        if(slot.status.nextDueMs > now.monotonicMs) {
            continue;
        }
        slot.status.inFlight = true;
        slot.status.hasAttempt = true;
        slot.status.lastAttemptMs = now.monotonicMs;
        slot.status.nextDueMs = now.monotonicMs + slot.intervalMs;
        due.push_back(RegionScheduleDue{slot.status.regionId, now.monotonicMs});
    }
    return due;
}

void RegionScheduler::completeAttempt(const std::string& regionId,
                                      bool success,
                                      const std::string& error) {
    Slot* slot = findSlot(regionId);
    if(slot == nullptr || !slot->status.inFlight) {
        return;
    }
    slot->status.inFlight = false;
    if(success) {
        ++slot->status.completedRuns;
        slot->status.hasSuccess = true;
        slot->status.lastSuccessMs = slot->status.lastAttemptMs;
        slot->status.lastError.clear();
    } else {
        ++slot->status.failedRuns;
        slot->status.lastError = error;
    }
}

std::vector<RegionRuntimeStatus> RegionScheduler::statuses() const {
    std::vector<RegionRuntimeStatus> result;
    result.reserve(slots_.size());
    for(const Slot& slot : slots_) {
        result.push_back(slot.status);
    }
    return result;
}

}  // namespace stock_monitor

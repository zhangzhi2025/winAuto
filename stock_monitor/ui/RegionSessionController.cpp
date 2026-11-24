#include "RegionSessionController.h"

#include <algorithm>
#include <chrono>
#include <ctime>
#include <utility>

#include <QRunnable>

namespace stock_monitor {
namespace {

std::int64_t monotonicNowMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

}  // namespace

struct RegionSessionController::RunTask final : QRunnable {
    RunTask(std::shared_ptr<CompletionQueue> queue,
            std::uint64_t generation,
            std::uint64_t token,
            RegionScanRequest request,
            std::shared_ptr<std::atomic_bool> cancel)
        : queue_(std::move(queue)),
          generation_(generation),
          token_(token),
          request_(std::move(request)),
          cancel_(std::move(cancel)) {}

    void run() override {
        CompletedRun completed;
        completed.generation = generation_;
        completed.token = token_;
        completed.result = scanRegion(request_, *cancel_);
        {
            std::lock_guard<std::mutex> guard(queue_->mutex);
            queue_->runs.push_back(std::move(completed));
        }
        queue_->active.fetch_sub(1);
    }

    std::shared_ptr<CompletionQueue> queue_;
    std::uint64_t generation_ = 0;
    std::uint64_t token_ = 0;
    RegionScanRequest request_;
    std::shared_ptr<std::atomic_bool> cancel_;
};

RegionSessionController::RegionSessionController(int maxWorkers)
    : queue_(std::make_shared<CompletionQueue>()),
      sessionCancel_(std::make_shared<std::atomic_bool>(false)),
      singleCancel_(std::make_shared<std::atomic_bool>(false)) {
    pool_.setMaxThreadCount(std::max(1, maxWorkers));
    pool_.setExpiryTimeout(30000);
    timer_.setInterval(tickIntervalMs_);
    QObject::connect(&timer_, &QTimer::timeout, [this] { onTick(); });
}

RegionSessionController::~RegionSessionController() {
    timer_.stop();
    sessionCancel_->store(true);
    singleCancel_->store(true);
    pool_.waitForDone(5000);
}

void RegionSessionController::setResultHandler(ResultHandler handler) {
    resultHandler_ = std::move(handler);
}

void RegionSessionController::setSnapshotHandler(SnapshotHandler handler) {
    snapshotHandler_ = std::move(handler);
}

void RegionSessionController::setLogHandler(LogHandler handler) {
    logHandler_ = std::move(handler);
}

bool RegionSessionController::start(const RegionCaptureConfig& config,
                                    std::uint64_t windowHandle,
                                    const FontColorConfig& fontColorConfig,
                                    float lowConfidenceThreshold,
                                    int tickIntervalMs) {
    if (state() != SessionState::Stopped || stopping_) {
        log("会话已在运行，忽略重复的开始请求。", true);
        return false;
    }
    if (!queue_->runs.empty()) {
        // Drain anything a previous single test left behind before restarting.
        drain();
    }
    config_ = config;
    fontColorConfig_ = fontColorConfig;
    lowConfidenceThreshold_ = lowConfidenceThreshold;
    windowHandle_ = windowHandle;
    tickIntervalMs_ = std::max(50, tickIntervalMs);
    timer_.setInterval(tickIntervalMs_);

    ScheduleInput input;
    input.monotonicMs = monotonicNowMs();
    input.localSecondsOfDay = localSecondsOfDay();

    const std::string previousSession = sessionId_;
    ++generation_;
    sessionId_ = localTimestamp();
    scheduler_.start(config_, input);
    if (scheduler_.state() == SessionState::Failed) {
        sessionId_ = previousSession;
        log("无法开始采集：配置未通过校验或没有启用的区域。", true);
        return false;
    }
    sessionCancel_ = std::make_shared<std::atomic_bool>(false);
    deliverGeneration_ = generation_;
    stopping_ = false;
    droppedRuns_ = 0;
    windowLost_ = false;
    windowLostReason_.clear();
    snapshotDirty_ = true;
    endedLogged_ = false;
    timer_.start();
    emitSnapshot();
    return true;
}

void RegionSessionController::stop() {
    if (stopping_) return;
    if (state() == SessionState::Stopped && queue_->active.load() == 0) return;
    stopping_ = true;
    deliverGeneration_ = 0;
    sessionCancel_->store(true);
    scheduler_.stop();
    log("已请求结束采集，等待进行中的区域结束后收尾。", false);
    snapshotDirty_ = true;
    finishStopIfIdle();
    emitSnapshot();
}

void RegionSessionController::shutdown(int waitMs) {
    stop();
    singleCancel_->store(true);
    timer_.stop();
    if (!pool_.waitForDone(waitMs)) {
        log("仍有区域采集任务未在超时内结束。", true);
    }
    drain();
    singleHandlers_.clear();
}

bool RegionSessionController::running() const { return scheduler_.running(); }

SessionState RegionSessionController::state() const { return scheduler_.state(); }

std::uint64_t RegionSessionController::windowHandle() const { return windowHandle_; }

int RegionSessionController::inFlightCount() const { return queue_->active.load(); }

int RegionSessionController::workerCount() const { return pool_.maxThreadCount(); }

RegionSessionSnapshot RegionSessionController::snapshot() const {
    RegionSessionSnapshot result;
    result.state = scheduler_.state();
    result.sessionId = sessionId_;
    result.skippedTicks = scheduler_.skippedTicks();
    result.inFlight = queue_->active.load();
    result.statuses = scheduler_.statuses();
    return result;
}

void RegionSessionController::runSingle(const RegionScanRequest& request, ResultHandler handler) {
    const std::uint64_t token = nextToken_++;
    singleHandlers_.emplace_back(token, std::move(handler));
    dispatch(0, token, request, singleCancel_);
    if (!timer_.isActive()) {
        timer_.start();
    }
}

void RegionSessionController::dispatch(std::uint64_t generation,
                                       std::uint64_t token,
                                       const RegionScanRequest& request,
                                       const std::shared_ptr<std::atomic_bool>& cancel) {
    queue_->active.fetch_add(1);
    pool_.start(new RunTask(queue_, generation, token, request, cancel));
}

const CaptureRegion* RegionSessionController::findRegion(const std::string& regionId) const {
    for (const CaptureRegion& region : config_.regions) {
        if (region.id == regionId) return &region;
    }
    return nullptr;
}

RegionScanRequest RegionSessionController::makeRequest(const CaptureRegion& region) const {
    RegionScanRequest request;
    request.windowHandle = windowHandle_;
    request.sessionId = sessionId_;
    request.region = region;
    request.expectedClientSize = config_.referenceClientSize;
    request.fontColorConfig = fontColorConfig_;
    request.lowConfidenceThreshold = lowConfidenceThreshold_;
    return request;
}

int RegionSessionController::localSecondsOfDay() const {
    const std::time_t now = std::time(nullptr);
    std::tm local {};
#ifdef _WIN32
    localtime_s(&local, &now);
#else
    localtime_r(&now, &local);
#endif
    return local.tm_hour * 3600 + local.tm_min * 60 + local.tm_sec;
}

void RegionSessionController::log(const std::string& message, bool error) {
    if (logHandler_) logHandler_(message, error);
}

void RegionSessionController::emitSnapshot() {
    if (!snapshotDirty_ && state() == SessionState::Stopped && !stopping_) return;
    snapshotDirty_ = false;
    if (snapshotHandler_) snapshotHandler_(snapshot());
}

bool RegionSessionController::drain() {
    std::vector<CompletedRun> runs;
    {
        std::lock_guard<std::mutex> guard(queue_->mutex);
        runs.swap(queue_->runs);
    }
    if (runs.empty()) return false;
    for (CompletedRun& run : runs) {
        if (run.token != 0) {
            ResultHandler handler;
            for (auto it = singleHandlers_.begin(); it != singleHandlers_.end(); ++it) {
                if (it->first == run.token) {
                    handler = std::move(it->second);
                    singleHandlers_.erase(it);
                    break;
                }
            }
            if (handler) handler(run.result);
            continue;
        }
        if (run.generation == 0 || run.generation != deliverGeneration_) {
            ++droppedRuns_;
            continue;
        }
        scheduler_.completeAttempt(run.result.regionId, run.result.succeeded(), run.result.message);
        snapshotDirty_ = true;
        if ((run.result.captureStatus == CaptureStatus::WindowUnavailable ||
             run.result.captureStatus == CaptureStatus::ClientSizeMismatch) &&
            !windowLost_) {
            windowLost_ = true;
            windowLostReason_ = run.result.message;
            windowLostStatus_ = run.result.captureStatus;
        }
        if (resultHandler_) resultHandler_(run.result);
    }
    return true;
}

void RegionSessionController::onTick() {
    if (stopping_) {
        drain();
        finishStopIfIdle();
        emitSnapshot();
        return;
    }

    drain();

    const SessionState afterDrain = state();
    if (windowLost_) {
        windowLost_ = false;
        if (afterDrain == SessionState::Running || afterDrain == SessionState::WaitingForDailyStart) {
            std::string reason = windowLostReason_.empty()
                                     ? std::string("目标窗口已失效或客户区尺寸与配置不一致。")
                                     : windowLostReason_;
            const std::string prefix =
                windowLostStatus_ == CaptureStatus::ClientSizeMismatch
                    ? "客户区尺寸与配置记录的参考尺寸不一致，正在停止整次采集会话："
                    : "目标窗口已失效，正在停止整次采集会话：";
            log(prefix + reason, true);
            windowLostStatus_ = CaptureStatus::Success;
            stop();
            return;
        }
        windowLostReason_.clear();
    }

    const SessionState current = state();
    if (current == SessionState::Running || current == SessionState::WaitingForDailyStart) {
        ScheduleInput now;
        now.monotonicMs = monotonicNowMs();
        now.localSecondsOfDay = localSecondsOfDay();
        const std::vector<RegionScheduleDue> due = scheduler_.poll(now);
        for (const RegionScheduleDue& item : due) {
            const CaptureRegion* region = findRegion(item.regionId);
            if (region == nullptr) {
                scheduler_.completeAttempt(item.regionId, false, "配置中已找不到该区域。");
                continue;
            }
            dispatch(generation_, 0, makeRequest(*region), sessionCancel_);
        }
        if (!due.empty()) snapshotDirty_ = true;
        if (due.empty() && state() == SessionState::WaitingForDailyStart) {
            snapshotDirty_ = true;
        }
    }

    if (state() == SessionState::Ended && !endedLogged_) {
        endedLogged_ = true;
        deliverGeneration_ = 0;
        log("已到每日结束时间，本次采集结束。", false);
        snapshotDirty_ = true;
    }

    const bool sessionLive =
        state() == SessionState::Running || state() == SessionState::WaitingForDailyStart;
    if (!sessionLive && singleHandlers_.empty() && queue_->active.load() == 0) {
        timer_.stop();
    }
    emitSnapshot();
}

void RegionSessionController::finishStopIfIdle() {
    if (!stopping_) return;
    if (queue_->active.load() > 0) return;
    drain();
    stopping_ = false;
    deliverGeneration_ = 0;
    sessionCancel_ = std::make_shared<std::atomic_bool>(false);
    if (singleHandlers_.empty()) timer_.stop();
    snapshotDirty_ = true;
    log("采集会话已结束。", false);
}

}  // namespace stock_monitor

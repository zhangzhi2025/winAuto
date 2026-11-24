#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include <QThreadPool>
#include <QTimer>

#include "RegionCaptureModel.h"
#include "RegionConfig.h"
#include "RegionScan.h"
#include "RegionScheduler.h"
#include "StockConfig.h"

namespace stock_monitor {

// Snapshot of the scheduler state that the UI needs to refresh its indicators.
struct RegionSessionSnapshot {
    SessionState state = SessionState::Stopped;
    std::string sessionId;
    int skippedTicks = 0;
    int inFlight = 0;
    std::vector<RegionRuntimeStatus> statuses;
};

// Owns the region capture scheduler, the bounded worker pool and the Qt timer
// that drives start/stop/due-time decisions on the GUI thread.
//
// Threading contract: every public method must be called on the GUI thread. Only
// the worker tasks touch other threads, and they communicate exclusively through
// a mutex-protected completion queue drained by the timer tick. No Qt widget is
// ever touched from a worker.
class RegionSessionController final {
public:
    using ResultHandler = std::function<void(const RegionCaptureResult&)>;
    using SnapshotHandler = std::function<void(const RegionSessionSnapshot&)>;
    using LogHandler = std::function<void(const std::string&, bool)>;

    explicit RegionSessionController(int maxWorkers = 4);
    ~RegionSessionController();

    RegionSessionController(const RegionSessionController&) = delete;
    RegionSessionController& operator=(const RegionSessionController&) = delete;

    void setResultHandler(ResultHandler handler);
    void setSnapshotHandler(SnapshotHandler handler);
    void setLogHandler(LogHandler handler);

    // Starts a session. Returns false and leaves the previous state untouched
    // when the configuration is rejected by the scheduler.
    bool start(const RegionCaptureConfig& config,
               std::uint64_t windowHandle,
               const FontColorConfig& fontColorConfig,
               float lowConfidenceThreshold,
               int tickIntervalMs);

    // Requests cancellation. In-flight runs are abandoned (their results are
    // dropped and reported) and the snapshot handler fires once they settle.
    void stop();

    // stop() followed by a blocking wait for the worker pool; used on close so
    // the process never exits with a running capture task.
    void shutdown(int waitMs = 5000);

    bool running() const;
    SessionState state() const;
    std::uint64_t windowHandle() const;
    RegionSessionSnapshot snapshot() const;
    int inFlightCount() const;
    int workerCount() const;

    // Runs one region outside any session state. Used by the operator's
    // "test this region" button; it never advances the scheduler.
    void runSingle(const RegionScanRequest& request, ResultHandler handler);

private:
    struct CompletedRun {
        std::uint64_t generation = 0;
        std::uint64_t token = 0;
        RegionCaptureResult result;
    };

    struct CompletionQueue {
        std::mutex mutex;
        std::vector<CompletedRun> runs;
        std::atomic<int> active {0};
    };

    struct RunTask;

    void dispatch(std::uint64_t generation,
                  std::uint64_t token,
                  const RegionScanRequest& request,
                  const std::shared_ptr<std::atomic_bool>& cancel);
    void onTick();
    bool drain();
    void emitSnapshot();
    void log(const std::string& message, bool error);
    RegionScanRequest makeRequest(const CaptureRegion& region) const;
    const CaptureRegion* findRegion(const std::string& regionId) const;
    int localSecondsOfDay() const;
    void finishStopIfIdle();

    RegionScheduler scheduler_;
    QTimer timer_;
    QThreadPool pool_;
    std::shared_ptr<CompletionQueue> queue_;
    std::shared_ptr<std::atomic_bool> sessionCancel_;
    std::shared_ptr<std::atomic_bool> singleCancel_;

    ResultHandler resultHandler_;
    SnapshotHandler snapshotHandler_;
    LogHandler logHandler_;

    RegionCaptureConfig config_;
    FontColorConfig fontColorConfig_;
    float lowConfidenceThreshold_ = kDefaultLowConfidenceThreshold;
    std::string sessionId_;
    std::uint64_t windowHandle_ = 0;
    std::uint64_t generation_ = 0;
    std::uint64_t deliverGeneration_ = 0;
    std::uint64_t nextToken_ = 1;
    int tickIntervalMs_ = 250;
    bool stopping_ = false;
    bool snapshotDirty_ = false;
    bool endedLogged_ = false;
    bool windowLost_ = false;
    CaptureStatus windowLostStatus_ = CaptureStatus::Success;
    std::string windowLostReason_;
    int droppedRuns_ = 0;
    std::vector<std::pair<std::uint64_t, ResultHandler>> singleHandlers_;
};

}  // namespace stock_monitor

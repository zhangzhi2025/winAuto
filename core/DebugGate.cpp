#include "core/DebugGate.h"

#include <chrono>

namespace wa {

void DebugGate::startPaused(bool paused) {
    std::lock_guard<std::mutex> lock(mutex_);
    paused_ = paused;
    cmd_ = Cmd::None;
}

void DebugGate::pause() {
    std::lock_guard<std::mutex> lock(mutex_);
    paused_ = true;
}

void DebugGate::resume() {
    std::lock_guard<std::mutex> lock(mutex_);
    paused_ = false;  // 没人等待时也生效：之后不再暂停
    if (waiting_ && cmd_ == Cmd::None) cmd_ = Cmd::Resume;
    cv_.notify_all();
}

void DebugGate::step() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (waiting_ && cmd_ == Cmd::None) cmd_ = Cmd::Step;  // 先到先得：已有待处理命令时忽略
    cv_.notify_all();
}

void DebugGate::skip() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (waiting_ && cmd_ == Cmd::None) cmd_ = Cmd::Skip;
    cv_.notify_all();
}

void DebugGate::stop() {
    std::lock_guard<std::mutex> lock(mutex_);
    cmd_ = Cmd::Stop;  // 即使没在等待也记下，下一个闸门立刻停止
    cv_.notify_all();
}

bool DebugGate::isWaiting() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return waiting_;
}

StepGate DebugGate::gate(const Step&, size_t index) {
    std::unique_lock<std::mutex> lock(mutex_);
    if (cmd_ == Cmd::Stop) return StepGate::Stop;
    if (!paused_) return StepGate::Run;

    waiting_ = true;
    cmd_ = Cmd::None;
    lock.unlock();
    if (onWaiting) onWaiting(index, true);
    lock.lock();

    // 取消令牌不会通知本类的条件变量，所以用短间隔轮询它；命令则立即唤醒
    while (cmd_ == Cmd::None && !token_.isCancelled()) {
        cv_.wait_for(lock, std::chrono::milliseconds(20));
    }
    const Cmd cmd = cmd_;
    const bool cancelled = token_.isCancelled();
    cmd_ = Cmd::None;
    waiting_ = false;
    lock.unlock();
    if (onWaiting) onWaiting(index, false);

    if (cancelled || cmd == Cmd::Stop) return StepGate::Stop;
    if (cmd == Cmd::Skip) return StepGate::Skip;
    return StepGate::Run;  // Resume 的 paused_=false 已在 resume() 里设置；Step 保持 paused_=true
}

}  // namespace wa

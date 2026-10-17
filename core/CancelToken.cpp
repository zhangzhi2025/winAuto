#include "core/CancelToken.h"

namespace wa {

void CancelToken::cancel() {
    {
        // 持锁置位，避免与 waitFor 的"检查-睡眠"之间丢失唤醒
        std::lock_guard<std::mutex> lock(mutex_);
        flag_.store(true, std::memory_order_release);
    }
    cv_.notify_all();
}

bool CancelToken::waitFor(std::chrono::milliseconds duration) const {
    if (isCancelled()) return false;
    if (duration.count() <= 0) return true;
    std::unique_lock<std::mutex> lock(mutex_);
    const bool cancelled =
        cv_.wait_for(lock, duration, [this] { return flag_.load(std::memory_order_acquire); });
    return !cancelled;
}

}  // namespace wa

#pragma once
// 线程安全的取消令牌。任意线程可调用 cancel()（例如 UI 线程或全局停止热键），
// 执行线程里的所有等待都通过 waitFor() 进行，因此取消能立即打断，而不是等下一次轮询。
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <mutex>

namespace wa {

class CancelToken {
public:
    CancelToken() = default;
    CancelToken(const CancelToken&) = delete;
    CancelToken& operator=(const CancelToken&) = delete;

    void cancel();
    bool isCancelled() const { return flag_.load(std::memory_order_acquire); }

    // 最长等待 duration。返回 true 表示完整等完；返回 false 表示被取消提前唤醒（或已取消）。
    bool waitFor(std::chrono::milliseconds duration) const;

private:
    std::atomic<bool> flag_{false};
    mutable std::mutex mutex_;
    mutable std::condition_variable cv_;
};

inline bool waitFor(std::chrono::milliseconds duration, const CancelToken& token) {
    return token.waitFor(duration);
}

}  // namespace wa

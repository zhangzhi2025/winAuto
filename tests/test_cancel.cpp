#include <atomic>
#include <chrono>
#include <thread>
#include <vector>

#include "core/CancelToken.h"
#include "core/Clock.h"
#include "tests/TestFramework.h"

using namespace winauto;
using Clock = std::chrono::steady_clock;

static long long msSince(Clock::time_point t) {
    return std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - t).count();
}

TEST(Cancel, waitCompletesWhenNotCancelled) {
    CancelToken t;
    auto start = Clock::now();
    CHECK(waitFor(60, t));
    CHECK(msSince(start) >= 55);
}

TEST(Cancel, alreadyCancelledReturnsImmediately) {
    CancelToken t;
    t.cancel();
    auto start = Clock::now();
    CHECK(!waitFor(5000, t));
    CHECK(msSince(start) < 100);
    CHECK(t.isCancelled());
}

TEST(Cancel, cancelInterruptsLongWaitWithin100ms) {
    CancelToken t;
    std::atomic<long long> latency{-1};
    std::atomic<bool> completed{true};
    std::atomic<Clock::rep> cancelAt{0};

    std::thread worker([&] {
        completed = waitFor(10000, t);
        auto now = Clock::now().time_since_epoch().count();
        latency = std::chrono::duration_cast<std::chrono::milliseconds>(Clock::duration(now - cancelAt.load())).count();
    });
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    cancelAt = Clock::now().time_since_epoch().count();
    t.cancel();
    worker.join();

    CHECK(!completed.load());
    CHECK(latency.load() >= 0);
    CHECK(latency.load() < 100);
}

TEST(Cancel, resetAllowsReuse) {
    CancelToken t;
    t.cancel();
    t.reset();
    CHECK(!t.isCancelled());
    CHECK(waitFor(10, t));
}

TEST(Cancel, manyWaitersAreAllReleased) {
    CancelToken t;
    std::atomic<int> released{0};
    std::vector<std::thread> threads;
    for (int i = 0; i < 16; ++i) {
        threads.emplace_back([&] {
            if (!waitFor(10000, t)) ++released;
        });
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(30));
    auto start = Clock::now();
    t.cancel();
    for (auto& th : threads) th.join();
    CHECK_EQ(released.load(), 16);
    CHECK(msSince(start) < 100);
}

TEST(Cancel, concurrentCancelAndPollingHasNoRace) {
    // 配合 -fsanitize=thread 使用；这里至少保证不崩溃、结果一致
    for (int round = 0; round < 50; ++round) {
        CancelToken t;
        std::atomic<bool> sawCancel{false};
        std::thread poller([&] {
            while (!t.isCancelled()) std::this_thread::yield();
            sawCancel = true;
        });
        std::thread canceller([&] { t.cancel(); });
        canceller.join();
        poller.join();
        CHECK(sawCancel.load());
    }
}

TEST(Cancel, systemClockSleepIsCancellable) {
    SystemClock clock;
    CancelToken t;
    std::thread canceller([&] {
        std::this_thread::sleep_for(std::chrono::milliseconds(30));
        t.cancel();
    });
    auto start = Clock::now();
    CHECK(!clock.sleep(5000, t));
    CHECK(msSince(start) < 200);
    canceller.join();
    std::int64_t a = clock.nowMs();
    CHECK(clock.nowMs() >= a);
}

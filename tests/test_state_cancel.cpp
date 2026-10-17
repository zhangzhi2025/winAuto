#include <atomic>
#include <chrono>
#include <set>
#include <thread>
#include <utility>
#include <vector>

#include "core/CancelToken.h"
#include "core/Clock.h"
#include "core/ExecutionState.h"
#include "testing.h"

using namespace wa;
using S = ExecutionState;
using namespace std::chrono;

TEST(state_machine_full_transition_table) {
    const S all[] = {S::Idle, S::Validating, S::Running, S::Paused,
                     S::Cancelling, S::Succeeded, S::Failed, S::Cancelled};
    const std::set<std::pair<S, S>> allowed = {
        {S::Idle, S::Validating},     {S::Validating, S::Running}, {S::Validating, S::Failed},
        {S::Validating, S::Cancelling}, {S::Running, S::Paused},   {S::Running, S::Cancelling},
        {S::Running, S::Succeeded},   {S::Running, S::Failed},    {S::Paused, S::Running},
        {S::Paused, S::Cancelling},   {S::Cancelling, S::Cancelled},
        {S::Succeeded, S::Idle},      {S::Failed, S::Idle},       {S::Cancelled, S::Idle}};
    for (S from : all) {
        for (S to : all) {
            const bool expected = allowed.count({from, to}) > 0;
            if (ExecutionStateMachine::isAllowed(from, to) != expected) {
                testing::reportFailure(__FILE__, __LINE__,
                                       std::string("transition ") + toString(from) + " -> " +
                                           toString(to) + " expected " + (expected ? "allowed" : "rejected"));
            }
        }
    }
}

TEST(idle_to_paused_is_rejected_and_state_is_kept) {
    ExecutionStateMachine sm;
    CHECK(!sm.transition(S::Paused));
    CHECK(sm.state() == S::Idle);
    CHECK(!sm.transition(S::Running));
    CHECK(sm.state() == S::Idle);
}

TEST(happy_path_and_reset) {
    ExecutionStateMachine sm;
    CHECK(sm.transition(S::Validating));
    CHECK(sm.transition(S::Running));
    CHECK(sm.transition(S::Succeeded));
    CHECK(isTerminal(sm.state()));
    CHECK(!sm.transition(S::Running));  // 终态不能直接重新运行
    CHECK(sm.transition(S::Idle));
}

TEST(cancel_is_possible_from_running_and_paused) {
    ExecutionStateMachine a;
    a.transition(S::Validating);
    a.transition(S::Running);
    CHECK(a.transition(S::Cancelling));
    CHECK(a.transition(S::Cancelled));

    ExecutionStateMachine b;
    b.transition(S::Validating);
    b.transition(S::Running);
    b.transition(S::Paused);
    CHECK(b.transition(S::Cancelling));
    CHECK(b.transition(S::Cancelled));
    CHECK(!b.transition(S::Succeeded));
}

TEST(wait_runs_full_duration_without_cancel) {
    CancelToken t;
    const auto start = steady_clock::now();
    CHECK(t.waitFor(milliseconds(60)));
    CHECK(duration_cast<milliseconds>(steady_clock::now() - start).count() >= 55);
    CHECK(wa::waitFor(milliseconds(0), t));
}

TEST(cancel_interrupts_a_long_wait_within_100ms) {
    CancelToken t;
    std::thread canceller([&] {
        std::this_thread::sleep_for(milliseconds(30));
        t.cancel();
    });
    const auto start = steady_clock::now();
    const bool completed = t.waitFor(seconds(10));
    const auto elapsed = duration_cast<milliseconds>(steady_clock::now() - start).count();
    canceller.join();
    CHECK(!completed);
    CHECK(elapsed < 130);  // 30ms 后取消 + 100ms 余量，远小于 10s
    CHECK(t.isCancelled());
}

TEST(already_cancelled_token_returns_immediately) {
    CancelToken t;
    t.cancel();
    const auto start = steady_clock::now();
    CHECK(!t.waitFor(seconds(5)));
    CHECK(duration_cast<milliseconds>(steady_clock::now() - start).count() < 50);
}

TEST(many_waiters_are_all_woken) {
    CancelToken t;
    std::atomic<int> woke{0};
    std::vector<std::thread> threads;
    for (int i = 0; i < 8; ++i) {
        threads.emplace_back([&] {
            if (!t.waitFor(seconds(10))) ++woke;
        });
    }
    std::this_thread::sleep_for(milliseconds(30));
    t.cancel();
    for (auto& th : threads) th.join();
    CHECK_EQ(woke.load(), 8);
}

TEST(concurrent_cancel_and_polling_has_no_race) {
    // 配合 -fsanitize=thread 运行时，可检出数据竞争
    for (int round = 0; round < 50; ++round) {
        CancelToken t;
        std::atomic<bool> seen{false};
        std::thread reader([&] {
            while (!t.isCancelled()) std::this_thread::yield();
            seen = true;
        });
        std::thread writer([&] { t.cancel(); });
        writer.join();
        reader.join();
        CHECK(seen.load());
    }
}

TEST(steady_clock_sleep_is_cancellable) {
    SteadyClock clock;
    CancelToken t;
    const std::int64_t t0 = clock.nowMs();
    CHECK(clock.sleepFor(20, t));
    CHECK(clock.nowMs() - t0 >= 18);
    t.cancel();
    CHECK(!clock.sleepFor(5000, t));
}

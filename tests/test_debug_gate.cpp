#include <atomic>
#include <chrono>
#include <thread>

#include "TestUtil.h"
#include "core/DebugGate.h"
#include "fakes/Fakes.h"
#include "testing.h"

using namespace wa;
using namespace wa::fakes;
using namespace testutil;
using namespace std::chrono_literals;

namespace {
// 等待条件成立，最多 10 秒（慢速/高负载 CI 下 TSan 会拖慢线程）
template <class F>
bool waitUntil(F f) {
    for (int i = 0; i < 2000; ++i) {
        if (f()) return true;
        std::this_thread::sleep_for(5ms);
    }
    return false;
}
const Step kStep = step("S1", StepType::Click);
}  // namespace

TEST(not_paused_runs_straight_through) {
    CancelToken t;
    DebugGate g(t);
    CHECK(g.gate(kStep, 0) == StepGate::Run);
    CHECK(!g.isWaiting());
}

TEST(start_paused_waits_until_step_then_pauses_again) {
    CancelToken t;
    DebugGate g(t);
    g.startPaused(true);
    std::atomic<int> result{-1};
    std::atomic<size_t> waitIdx{99};
    g.onWaiting = [&](size_t i, bool w) {
        if (w) waitIdx = i;
    };
    std::thread th([&] { result = static_cast<int>(g.gate(kStep, 3)); });
    CHECK(waitUntil([&] { return g.isWaiting(); }));
    CHECK_EQ(waitIdx.load(), static_cast<size_t>(3));
    CHECK_EQ(result.load(), -1);  // 还在等
    g.step();
    th.join();
    CHECK(static_cast<StepGate>(result.load()) == StepGate::Run);

    // step 之后仍是暂停状态：下一步又要等
    std::thread th2([&] { result = static_cast<int>(g.gate(kStep, 4)); });
    CHECK(waitUntil([&] { return g.isWaiting(); }));
    g.resume();
    th2.join();
    CHECK(static_cast<StepGate>(result.load()) == StepGate::Run);
    CHECK(g.gate(kStep, 5) == StepGate::Run);  // resume 之后不再暂停
}

TEST(skip_returns_skip_and_stays_paused) {
    CancelToken t;
    DebugGate g(t);
    g.startPaused(true);
    std::atomic<int> result{-1};
    std::thread th([&] { result = static_cast<int>(g.gate(kStep, 0)); });
    CHECK(waitUntil([&] { return g.isWaiting(); }));
    g.skip();
    th.join();
    CHECK(static_cast<StepGate>(result.load()) == StepGate::Skip);
    std::thread th2([&] { result = static_cast<int>(g.gate(kStep, 1)); });
    CHECK(waitUntil([&] { return g.isWaiting(); }));
    g.stop();
    th2.join();
    CHECK(static_cast<StepGate>(result.load()) == StepGate::Stop);
}

TEST(commands_without_a_waiting_step_are_ignored) {
    CancelToken t;
    DebugGate g(t);
    g.step();  // 没人等待：不应积压
    g.skip();
    CHECK(g.gate(kStep, 0) == StepGate::Run);
    g.pause();
    std::atomic<int> result{-1};
    std::thread th([&] { result = static_cast<int>(g.gate(kStep, 1)); });
    CHECK(waitUntil([&] { return g.isWaiting(); }));  // 确实在等，没有被旧命令放行
    g.stop();
    th.join();
}

TEST(pause_takes_effect_before_next_step) {
    CancelToken t;
    DebugGate g(t);
    CHECK(g.gate(kStep, 0) == StepGate::Run);
    g.pause();
    std::atomic<bool> done{false};
    std::thread th([&] {
        g.gate(kStep, 1);
        done = true;
    });
    CHECK(waitUntil([&] { return g.isWaiting(); }));
    CHECK(!done.load());
    g.resume();
    th.join();
}

TEST(cancel_token_wakes_a_waiting_gate) {
    CancelToken t;
    DebugGate g(t);
    g.startPaused(true);
    std::atomic<int> result{-1};
    std::thread th([&] { result = static_cast<int>(g.gate(kStep, 0)); });
    CHECK(waitUntil([&] { return g.isWaiting(); }));
    t.cancel();  // 例如全局停止热键
    th.join();
    CHECK(static_cast<StepGate>(result.load()) == StepGate::Stop);
}

TEST(stop_before_waiting_stops_next_gate) {
    CancelToken t;
    DebugGate g(t);
    g.stop();
    CHECK(g.gate(kStep, 0) == StepGate::Stop);
}

TEST(engine_single_stepping_end_to_end) {
    FakeClock clock;
    FakeWindowProvider windows;
    windows.windows.push_back(FakeWindowProvider::window(1, "notepad.exe", "Untitled"));
    FakeElementLocator locator;
    locator.handler = [](const LeafLocator&, int) {
        return Result<ElementRef>::success(FakeElementLocator::element("b", true));
    };
    FakeInputController input;
    CancelToken token;
    DebugGate gate(token);
    gate.startPaused(true);

    Workflow wf = minimalWorkflow();
    wf.steps = {clickStep("S1"), clickStep("S2"), clickStep("S3")};

    EngineDeps deps;
    deps.windows = &windows;
    deps.locator = &locator;
    deps.input = &input;
    deps.clock = &clock;
    deps.beforeStep = [&](const Step& s, size_t i) { return gate.gate(s, i); };
    WorkflowEngine engine(deps);

    RunReport report;
    // 用 onWaiting 的回调计数同步：第 n 次"开始等待"才表示引擎真的到了第 n 个闸门
    std::atomic<int> arrivals{0};
    std::atomic<size_t> actionsAtArrival{0};
    gate.onWaiting = [&](size_t, bool waiting) {
        if (!waiting) return;
        actionsAtArrival = input.actions.size();  // 此刻引擎线程停在闸门，读取是安全的
        ++arrivals;
    };
    std::thread th([&] { report = engine.run(wf, token); });

    CHECK(waitUntil([&] { return arrivals.load() == 1; }));
    CHECK_EQ(actionsAtArrival.load(), static_cast<size_t>(0));
    gate.step();  // 执行 S1
    CHECK(waitUntil([&] { return arrivals.load() == 2; }));
    CHECK_EQ(actionsAtArrival.load(), static_cast<size_t>(1));
    gate.skip();  // 跳过 S2
    CHECK(waitUntil([&] { return arrivals.load() == 3; }));
    CHECK_EQ(actionsAtArrival.load(), static_cast<size_t>(1));
    gate.resume();
    th.join();

    CHECK(report.state == ExecutionState::Succeeded);
    CHECK_EQ(input.actions.size(), static_cast<size_t>(2));  // S1 和 S3
    CHECK(report.steps[1].skipped);
}

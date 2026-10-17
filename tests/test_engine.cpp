#include <string>
#include <vector>

#include "TestUtil.h"
#include "core/Engine.h"
#include "fakes/Fakes.h"
#include "testing.h"

using namespace wa;
using namespace wa::fakes;
using namespace testutil;

namespace {

struct Env {
    FakeClock clock;
    FakeWindowProvider windows;
    FakeElementLocator locator;
    FakeInputController input;
    CancelToken token;
    PromptFn prompt;
    EngineObserver observer;

    Env() { windows.windows.push_back(FakeWindowProvider::window(1, "notepad.exe", "Untitled")); }

    RunReport run(const Workflow& wf, RunOptions options = {}) {
        EngineDeps deps;
        deps.windows = &windows;
        deps.locator = &locator;
        deps.input = &input;
        deps.clock = &clock;
        deps.prompt = prompt;
        deps.observer = observer;
        WorkflowEngine engine(deps);
        return engine.run(wf, token, options);
    }
};

Workflow flow(std::initializer_list<Step> steps) {
    Workflow wf = minimalWorkflow();
    wf.steps.assign(steps.begin(), steps.end());
    wf.execution.stepTimeoutMs = 300;
    return wf;
}

// 报告里所有可能出现文本的地方，拼成一个字符串，用于检查敏感内容是否泄露
std::string everything(const RunReport& r) {
    std::string s = r.detail + "|" + r.failedStepId + "|";
    for (const auto& st : r.steps) s += st.stepId + st.type + st.detail + "|";
    for (const auto& l : r.logs) s += l.stepId + l.message + "|";
    return s;
}

Result<ElementRef> found(const std::string& name = "el", bool invoke = true, bool value = false) {
    return Result<ElementRef>::success(FakeElementLocator::element(name, invoke, value));
}
Result<ElementRef> notFound() { return Result<ElementRef>::failure(ErrCode::ElementNotFound, "none"); }

}  // namespace

TEST(runs_all_steps_in_order_and_records_timing) {
    Env env;
    env.locator.handler = [](const LeafLocator&, int) { return found("ok"); };
    Workflow wf = flow({step("S1", StepType::ActivateWindow), clickStep("S2"),
                        typeStep("S3", "hello"), hotkeyStep("S4", {"ctrl", "s"}), waitStep("S5", 250)});
    RunReport r = env.run(wf);

    CHECK(r.state == ExecutionState::Succeeded);
    CHECK(r.code == ErrCode::Ok);
    CHECK_EQ(r.steps.size(), size_t(5));
    CHECK_EQ(env.windows.activated.size(), size_t(1));
    CHECK_EQ(env.windows.activated[0], std::uint64_t(1));
    const std::vector<std::string> expected = {"invoke:ok", "type:unicode:hello", "hotkey:CTRL+S"};
    CHECK(env.input.actions == expected);
    for (const auto& s : r.steps) CHECK(s.code == ErrCode::Ok);
    CHECK_EQ(r.steps[4].durationMs, 250);       // wait 步骤耗时来自（虚拟）时钟
    CHECK(r.steps[4].startMs >= r.steps[3].startMs);
    CHECK_EQ(r.steps[0].attempts, 1);
}

TEST(failing_step_stops_the_run_and_reports_step_id_and_code) {
    Env env;
    Workflow wf = flow({step("S1", StepType::ActivateWindow), clickStep("S2"),
                        typeStep("S3", "never")});
    RunReport r = env.run(wf);  // locator 默认一直 ElementNotFound

    CHECK(r.state == ExecutionState::Failed);
    CHECK(r.code == ErrCode::ElementNotFound);
    CHECK_EQ(r.failedStepId, "S2");
    CHECK_EQ(r.steps.size(), size_t(2));  // S3 没有执行
    CHECK(env.input.actions.empty());
    CHECK(r.steps[1].code == ErrCode::ElementNotFound);
}

TEST(element_not_found_is_retried_until_success) {
    Env env;
    // 每次尝试在 300ms 超时内轮询 4 次（t=0,100,200,300）；前 4 次调用全失败 = 第 1 次尝试失败
    env.locator.handler = [](const LeafLocator&, int idx) { return idx < 4 ? notFound() : found("late"); };
    Step c = clickStep("S2");
    c.retry = RetryPolicy{1, 500};
    RunReport r = env.run(flow({step("S1", StepType::ActivateWindow), c}));

    CHECK(r.state == ExecutionState::Succeeded);
    CHECK_EQ(r.steps[1].attempts, 2);
    CHECK_EQ(env.input.actions.size(), size_t(1));
    CHECK_EQ(env.input.actions[0], "invoke:late");
}

TEST(retries_are_exhausted_then_fail) {
    Env env;
    Step c = clickStep("S2");
    c.retry = RetryPolicy{2, 100};
    RunReport r = env.run(flow({c}));
    CHECK(r.state == ExecutionState::Failed);
    CHECK_EQ(r.steps[0].attempts, 3);
    CHECK(r.code == ErrCode::ElementNotFound);
}

TEST(ambiguous_element_is_never_retried) {
    Env env;
    env.locator.handler = [](const LeafLocator&, int) {
        return Result<ElementRef>::failure(ErrCode::ElementAmbiguous, "2 matches");
    };
    Step c = clickStep("S2");
    c.retry = RetryPolicy{3, 100};
    RunReport r = env.run(flow({c}));
    CHECK(r.state == ExecutionState::Failed);
    CHECK(r.code == ErrCode::ElementAmbiguous);
    CHECK_EQ(r.steps[0].attempts, 1);
    CHECK_EQ(env.locator.calls.size(), size_t(1));
    CHECK(env.input.actions.empty());
}

TEST(fallback_moves_on_only_when_not_found) {
    Env env;
    env.locator.handler = [](const LeafLocator& l, int) {
        if (std::holds_alternative<UiaLocator>(l)) return notFound();
        return found("pt", /*invoke=*/false);
    };
    Step c = step("S2", StepType::Click);
    FallbackLocator fb;
    fb.items.push_back(uia("missing", "Button"));
    fb.items.push_back(WindowPointLocator{10, 10, Anchor::TopLeft});
    c.locator = Locator{fb};
    RunReport r = env.run(flow({c}));

    CHECK(r.state == ExecutionState::Succeeded);
    CHECK_EQ(env.locator.calls.size(), size_t(2));
    CHECK_EQ(env.locator.calls[0], "uia");
    CHECK_EQ(env.locator.calls[1], "window_point");
    CHECK_EQ(env.input.actions[0], "click:120,210");  // 元素 bounds 中心点
}

TEST(fallback_stops_immediately_on_ambiguity) {
    Env env;
    env.locator.handler = [](const LeafLocator& l, int) {
        if (std::holds_alternative<UiaLocator>(l)) {
            return Result<ElementRef>::failure(ErrCode::ElementAmbiguous, "many");
        }
        return found("pt", false);
    };
    Step c = step("S2", StepType::Click);
    FallbackLocator fb;
    fb.items.push_back(uia("dup", "Button"));
    fb.items.push_back(WindowPointLocator{10, 10, Anchor::TopLeft});
    c.locator = Locator{fb};
    RunReport r = env.run(flow({c}));

    CHECK(r.code == ErrCode::ElementAmbiguous);
    CHECK_EQ(env.locator.calls.size(), size_t(1));  // 没有进入第二个策略
    CHECK(env.input.actions.empty());
}

TEST(click_point_mode_ignores_invoke_support) {
    Env env;
    env.locator.handler = [](const LeafLocator&, int) { return found("b", /*invoke=*/true); };
    Step c = clickStep("S2");
    c.clickMode = ClickMode::Point;
    RunReport r = env.run(flow({c}));
    CHECK(r.state == ExecutionState::Succeeded);
    CHECK_EQ(env.input.actions[0], "click:120,210");
}

TEST(wait_for_element_times_out_with_timeout_code) {
    Env env;
    Step w = step("S1", StepType::WaitForElement);
    w.locator = Locator{uia("never", "Button")};
    w.timeoutMs = 500;
    const std::int64_t t0 = env.clock.now;
    RunReport r = env.run(flow({w}));

    CHECK(r.state == ExecutionState::Failed);
    CHECK(r.code == ErrCode::Timeout);
    CHECK(env.clock.now - t0 >= 500);
}

TEST(wait_for_element_succeeds_when_it_appears) {
    Env env;
    env.locator.handler = [](const LeafLocator&, int idx) { return idx < 3 ? notFound() : found(); };
    Step w = step("S1", StepType::WaitForElement);
    w.locator = Locator{uia("later", "Button")};
    w.timeoutMs = 1000;
    RunReport r = env.run(flow({w}));
    CHECK(r.state == ExecutionState::Succeeded);
    CHECK_EQ(env.locator.calls.size(), size_t(4));
}

TEST(assert_element_is_a_single_check) {
    Env env;
    Step a = step("S1", StepType::AssertElement);
    a.locator = Locator{uia("nope", "Button")};
    RunReport r = env.run(flow({a}));
    CHECK(r.code == ErrCode::ElementNotFound);
    CHECK_EQ(env.locator.calls.size(), size_t(1));
}

TEST(cancel_during_a_wait_step) {
    Env env;
    env.clock.onSleep = [&](std::int64_t) { env.token.cancel(); };
    RunReport r = env.run(flow({waitStep("S1", 5000), typeStep("S2", "never")}));

    CHECK(r.state == ExecutionState::Cancelled);
    CHECK(r.code == ErrCode::Cancelled);
    CHECK_EQ(r.failedStepId, "S1");
    CHECK(env.input.actions.empty());
}

TEST(cancel_between_steps_stops_before_the_next_one) {
    Env env;
    env.observer.onStepFinished = [&](const StepRecord& rec) {
        if (rec.stepId == "S1") env.token.cancel();
    };
    RunReport r = env.run(flow({step("S1", StepType::ActivateWindow), typeStep("S2", "never")}));
    CHECK(r.state == ExecutionState::Cancelled);
    CHECK_EQ(r.steps.size(), size_t(1));
    CHECK(env.input.actions.empty());
}

TEST(cancel_during_element_polling) {
    Env env;
    env.clock.onSleep = [&](std::int64_t) { env.token.cancel(); };
    RunReport r = env.run(flow({clickStep("S1")}));
    CHECK(r.state == ExecutionState::Cancelled);
    CHECK_EQ(env.locator.calls.size(), size_t(1));
}

TEST(cancel_before_start) {
    Env env;
    env.token.cancel();
    RunReport r = env.run(flow({step("S1", StepType::ActivateWindow)}));
    CHECK(r.state == ExecutionState::Cancelled);
    CHECK(r.steps.empty());
    CHECK(env.windows.activated.empty());
}

TEST(elevated_target_is_refused_before_the_first_step) {
    Env env;
    env.windows.windows = {FakeWindowProvider::window(1, "notepad.exe", "", /*elevated=*/true)};
    RunReport r = env.run(flow({step("S1", StepType::ActivateWindow), typeStep("S2", "x")}));

    CHECK(r.state == ExecutionState::Failed);
    CHECK(r.code == ErrCode::WindowElevated);
    CHECK(r.steps.empty());
    CHECK(env.windows.activated.empty());
    CHECK(env.input.actions.empty());
}

TEST(elevated_target_is_allowed_when_we_are_elevated_too) {
    Env env;
    env.windows.windows = {FakeWindowProvider::window(1, "notepad.exe", "", true)};
    env.windows.selfElevated = true;
    RunReport r = env.run(flow({step("S1", StepType::ActivateWindow)}));
    CHECK(r.state == ExecutionState::Succeeded);
}

TEST(missing_window_fails_after_its_timeout) {
    Env env;
    env.windows.windows.clear();
    Workflow wf = flow({step("S1", StepType::ActivateWindow)});
    wf.windows["main"].timeoutMs = 300;
    const std::int64_t t0 = env.clock.now;
    RunReport r = env.run(wf);
    CHECK(r.code == ErrCode::WindowNotFound);
    CHECK(env.clock.now - t0 >= 300);
    CHECK(r.steps.empty());
}

TEST(window_that_appears_late_is_found) {
    Env env;
    env.windows.notFoundFirst = 2;
    RunReport r = env.run(flow({step("S1", StepType::ActivateWindow)}));
    CHECK(r.state == ExecutionState::Succeeded);
    CHECK_EQ(env.windows.findCalls, 3);
}

TEST(ambiguous_window_match_is_refused) {
    Env env;
    env.windows.windows.push_back(FakeWindowProvider::window(2, "notepad.exe", "Other"));
    Workflow wf = flow({step("S1", StepType::ActivateWindow)});
    wf.windows["main"].timeoutMs = 200;
    RunReport r = env.run(wf);
    CHECK(r.code == ErrCode::WindowNotFound);
    CHECK_CONTAINS(r.detail, "unique");
    CHECK(env.windows.activated.empty());
}

TEST(window_closed_mid_run_fails_without_retry) {
    Env env;
    env.observer.onStepFinished = [&](const StepRecord& rec) {
        if (rec.stepId == "S1") env.windows.invalidHandles.insert(1);
    };
    Step t = typeStep("S2", "x");
    t.retry = RetryPolicy{3, 100};
    RunReport r = env.run(flow({step("S1", StepType::ActivateWindow), t}));
    CHECK(r.code == ErrCode::WindowNotFound);
    CHECK_EQ(r.failedStepId, "S2");
    CHECK_EQ(r.steps[1].attempts, 1);
    CHECK(env.input.actions.empty());
}

TEST(failed_expectation_fails_the_step) {
    Env env;
    Step t = typeStep("S1", "hello");
    Expect e;
    e.locator = Locator{uia("result", "Text")};
    t.expect = e;
    RunReport r = env.run(flow({t}));
    CHECK(r.state == ExecutionState::Failed);
    CHECK(r.code == ErrCode::ExpectationFailed);
    CHECK_EQ(env.input.actions.size(), size_t(1));  // 动作已执行，但校验没通过
}

TEST(satisfied_expectation_passes_and_expectation_failure_is_retryable) {
    Env env;
    // 前 4 次调用（= 第 1 次尝试的 expect 轮询）找不到，之后找到
    env.locator.handler = [](const LeafLocator&, int idx) { return idx < 4 ? notFound() : found(); };
    Step t = typeStep("S1", "hello");
    Expect e;
    e.locator = Locator{uia("result", "Text")};
    t.expect = e;
    t.retry = RetryPolicy{1, 100};
    RunReport r = env.run(flow({t}));
    CHECK(r.state == ExecutionState::Succeeded);
    CHECK_EQ(r.steps[0].attempts, 2);
    CHECK_EQ(env.input.actions.size(), size_t(2));  // 重试会重新执行动作本身
}

TEST(invalid_workflow_is_rejected_before_anything_runs) {
    Env env;
    Workflow wf = flow({step("S1", StepType::ActivateWindow, "ghost")});
    RunReport r = env.run(wf);
    CHECK(r.state == ExecutionState::Failed);
    CHECK(r.code == ErrCode::InvalidWorkflow);
    CHECK_CONTAINS(r.detail, "steps[0].window");
    CHECK_EQ(env.windows.findCalls, 0);
    CHECK(r.steps.empty());
}

TEST(empty_workflow_is_rejected_by_the_engine) {
    Env env;
    Workflow wf = minimalWorkflow();
    wf.steps.clear();
    RunReport r = env.run(wf);
    CHECK(r.code == ErrCode::InvalidWorkflow);
}

TEST(high_risk_operations_need_confirmation) {
    Env env;
    Workflow wf = flow({step("S1", StepType::ActivateWindow), hotkeyStep("S2", {"ALT", "F4"})});
    RunReport r = env.run(wf);
    CHECK(r.state == ExecutionState::Failed);
    CHECK(r.code == ErrCode::ConfirmationRequired);
    CHECK(env.windows.activated.empty());
    CHECK(env.input.actions.empty());

    RunOptions confirmed;
    confirmed.confirmedRisks = true;
    Env env2;
    RunReport r2 = env2.run(wf, confirmed);
    CHECK(r2.state == ExecutionState::Succeeded);
    CHECK_EQ(env2.input.actions[0], "hotkey:ALT+F4");
}

TEST(on_failure_continue_runs_remaining_steps_but_run_still_fails) {
    Env env;
    Workflow wf = flow({step("S1", StepType::ActivateWindow), clickStep("S2"), typeStep("S3", "after")});
    wf.execution.onFailure = FailurePolicy::Continue;
    RunReport r = env.run(wf);

    CHECK(r.state == ExecutionState::Failed);
    CHECK(r.code == ErrCode::ElementNotFound);
    CHECK_EQ(r.failedStepId, "S2");
    CHECK_EQ(r.steps.size(), size_t(3));
    CHECK(r.steps[2].code == ErrCode::Ok);
    CHECK_EQ(env.input.actions[0], "type:unicode:after");
}

TEST(step_level_on_failure_overrides_the_global_policy) {
    Env env;
    Step c = clickStep("S2");
    c.onFailure = FailurePolicy::Stop;
    Workflow wf = flow({c, typeStep("S3", "never")});
    wf.execution.onFailure = FailurePolicy::Continue;
    RunReport r = env.run(wf);
    CHECK_EQ(r.steps.size(), size_t(1));

    Env env2;
    Step c2 = clickStep("S2");
    c2.onFailure = FailurePolicy::Continue;
    Workflow wf2 = flow({c2, typeStep("S3", "yes")});
    RunReport r2 = env2.run(wf2);  // 全局 stop，步骤级 continue
    CHECK_EQ(r2.steps.size(), size_t(2));
    CHECK(r2.state == ExecutionState::Failed);
}

TEST(input_failures_are_not_retried) {
    Env env;
    env.input.hook = [](const std::string&) {
        return Status::failure(ErrCode::InputBlocked, "UIPI");
    };
    Step t = typeStep("S1", "x");
    t.retry = RetryPolicy{3, 100};
    RunReport r = env.run(flow({t}));
    CHECK(r.code == ErrCode::InputBlocked);
    CHECK_EQ(r.steps[0].attempts, 1);
}

TEST(activation_failure_stops_the_run) {
    Env env;
    env.windows.activateResult = Status::failure(ErrCode::InputBlocked, "foreground lock");
    RunReport r = env.run(flow({step("S1", StepType::ActivateWindow), typeStep("S2", "x")}));
    CHECK(r.code == ErrCode::InputBlocked);
    CHECK(env.input.actions.empty());
}

TEST(set_value_requires_value_pattern) {
    Env env;
    env.locator.handler = [](const LeafLocator&, int) { return found("edit", true, /*value=*/false); };
    Step s = step("S1", StepType::SetValue);
    s.locator = Locator{uia("box", "Edit")};
    s.text = "abc";
    RunReport r = env.run(flow({s}));
    CHECK(r.code == ErrCode::Unsupported);

    Env env2;
    env2.locator.handler = [](const LeafLocator&, int) { return found("edit", true, true); };
    RunReport r2 = env2.run(flow({s}));
    CHECK(r2.state == ExecutionState::Succeeded);
    CHECK_EQ(env2.input.actions[0], "setValue:edit:abc");
}

TEST(observer_receives_state_and_step_events) {
    Env env;
    std::vector<ExecutionState> states;
    std::vector<std::string> started, finished;
    env.observer.onStateChanged = [&](ExecutionState s) { states.push_back(s); };
    env.observer.onStepStarted = [&](const std::string& id) { started.push_back(id); };
    env.observer.onStepFinished = [&](const StepRecord& r) { finished.push_back(r.stepId); };
    env.run(flow({step("S1", StepType::ActivateWindow), waitStep("S2", 10)}));

    const std::vector<ExecutionState> expectedStates = {ExecutionState::Validating, ExecutionState::Running,
                                                        ExecutionState::Succeeded};
    CHECK(states == expectedStates);
    CHECK((started == std::vector<std::string>{"S1", "S2"}));
    CHECK((finished == std::vector<std::string>{"S1", "S2"}));
}

TEST(cancelled_run_goes_through_cancelling_state) {
    Env env;
    std::vector<ExecutionState> states;
    env.observer.onStateChanged = [&](ExecutionState s) { states.push_back(s); };
    env.clock.onSleep = [&](std::int64_t) { env.token.cancel(); };
    env.run(flow({waitStep("S1", 1000)}));
    CHECK(states.size() >= 3);
    CHECK(states[states.size() - 2] == ExecutionState::Cancelling);
    CHECK(states.back() == ExecutionState::Cancelled);
}

TEST(logs_cover_the_run_with_monotonic_timestamps) {
    Env env;
    RunReport r = env.run(flow({step("S1", StepType::ActivateWindow), waitStep("S2", 200)}));
    CHECK(r.logs.size() >= 4);
    for (size_t i = 1; i < r.logs.size(); ++i) CHECK(r.logs[i].tMs >= r.logs[i - 1].tMs);
    bool sawStart = false;
    for (const auto& l : r.logs) sawStart = sawStart || l.message.find("start") != std::string::npos;
    CHECK(sawStart);
}

// ------------------------------------------------------------------ 敏感信息

TEST(sensitive_text_never_appears_in_logs_or_report) {
    Env env;
    env.input.hook = [](const std::string& action) {
        // 模拟平台层把输入内容带进了错误信息
        return Status::failure(ErrCode::InputBlocked, "cannot send " + action);
    };
    std::string observed;
    env.observer.onStepFinished = [&](const StepRecord& rec) { observed += rec.detail; };
    env.observer.onLog = [&](const LogEntry& e) { observed += e.message; };
    Workflow wf = flow({typeStep("S1", "hunter2-secret", /*sensitive=*/true)});
    RunReport r = env.run(wf);
    CHECK_NOT_CONTAINS(observed, "hunter2-secret");  // 实时回调同样不泄露

    CHECK(r.code == ErrCode::InputBlocked);
    CHECK_NOT_CONTAINS(everything(r), "hunter2-secret");
    CHECK_CONTAINS(everything(r), "***");  // 有脱敏痕迹，而不是丢失了错误信息
    CHECK_EQ(env.input.actions.size(), size_t(1));  // 但真实输入用的是原文
}

TEST(prompted_text_is_used_but_never_logged) {
    Env env;
    int asked = 0;
    env.prompt = [&](const Step& s) {
        ++asked;
        CHECK_EQ(s.id, "S1");
        return Result<std::string>::success("p@ss-from-user");
    };
    env.input.hook = [](const std::string& a) {
        return Status::failure(ErrCode::InputBlocked, "blocked: " + a);
    };
    Step t = step("S1", StepType::InputText);
    t.textFromPrompt = true;
    t.retry = RetryPolicy{2, 10};
    RunReport r = env.run(flow({t}));

    CHECK_EQ(asked, 1);  // 重试不会再次询问
    CHECK_EQ(env.input.actions[0], "type:unicode:p@ss-from-user");
    CHECK_NOT_CONTAINS(everything(r), "p@ss-from-user");
}

TEST(prompt_cancelled_by_user_cancels_the_run) {
    Env env;
    env.prompt = [](const Step&) { return Result<std::string>::failure(ErrCode::Cancelled, "user closed dialog"); };
    Step t = step("S1", StepType::InputText);
    t.textFromPrompt = true;
    RunReport r = env.run(flow({t}));
    CHECK(r.state == ExecutionState::Cancelled);
    CHECK(env.input.actions.empty());
}

TEST(prompt_without_provider_is_unsupported) {
    Env env;
    Step t = step("S1", StepType::InputText);
    t.textFromPrompt = true;
    RunReport r = env.run(flow({t}));
    CHECK(r.code == ErrCode::Unsupported);
    CHECK(env.input.actions.empty());
}

TEST(non_sensitive_text_is_not_redacted_from_platform_errors) {
    Env env;
    env.input.hook = [](const std::string&) { return Status::failure(ErrCode::InputBlocked, "blocked: hello"); };
    RunReport r = env.run(flow({typeStep("S1", "hello", /*sensitive=*/false)}));
    CHECK_CONTAINS(r.detail, "hello");
}

TEST(click_waits_until_disabled_button_becomes_enabled) {
    Env env;
    env.locator.handler = [](const LeafLocator&, int idx) {
        ElementRef e = FakeElementLocator::element("b", true);
        e.enabled = idx >= 2;  // 前两次轮询仍是禁用
        return Result<ElementRef>::success(e);
    };
    RunReport r = env.run(flow({clickStep("S2")}));
    CHECK(r.state == ExecutionState::Succeeded);
    CHECK_EQ(env.input.actions.size(), static_cast<size_t>(1));
    CHECK_EQ(env.locator.calls.size(), static_cast<size_t>(3));
}

TEST(click_on_permanently_disabled_element_fails_with_clear_reason) {
    Env env;
    env.locator.handler = [](const LeafLocator&, int) {
        ElementRef e = FakeElementLocator::element("b", true);
        e.enabled = false;
        return Result<ElementRef>::success(e);
    };
    Step c = clickStep("S2");
    c.timeoutMs = 400;
    RunReport r = env.run(flow({c}));
    CHECK(r.state == ExecutionState::Failed);
    CHECK(r.code == ErrCode::ElementNotFound);
    CHECK(env.input.actions.empty());  // 绝不点击禁用控件
    CHECK_CONTAINS(r.detail, "disabled");
}

TEST(wait_for_element_does_not_require_enabled) {
    Env env;
    env.locator.handler = [](const LeafLocator&, int) {
        ElementRef e = FakeElementLocator::element("b", true);
        e.enabled = false;
        return Result<ElementRef>::success(e);
    };
    Step w = step("S1", StepType::WaitForElement);
    w.locator = Locator{uia("x", "Button")};
    RunReport r = env.run(flow({w}));
    CHECK(r.state == ExecutionState::Succeeded);
}

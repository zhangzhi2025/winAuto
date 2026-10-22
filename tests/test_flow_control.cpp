// 阶段 6：条件与循环（if / repeat / while）、失败截图、剪贴板输入
#include <string>
#include <vector>

#include "TestUtil.h"
#include "core/Engine.h"
#include "core/Summary.h"
#include "core/WorkflowEditor.h"
#include "fakes/Fakes.h"
#include "nlohmann/json.hpp"
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
    FakeScreenshot shots;
    FakeClipboard clipboard;
    std::vector<std::string> trace;
    CancelToken token;
    StepGateFn gate;
    PromptFn prompt;
    bool withScreenshot = true;
    bool withClipboard = true;

    Env() {
        windows.windows.push_back(FakeWindowProvider::window(1, "notepad.exe", "Untitled"));
        input.trace = &trace;
        clipboard.trace = &trace;
    }

    RunReport run(const Workflow& wf, RunOptions options = {}) {
        EngineDeps deps;
        deps.windows = &windows;
        deps.locator = &locator;
        deps.input = &input;
        deps.clock = &clock;
        deps.screenshot = withScreenshot ? &shots : nullptr;
        deps.clipboard = withClipboard ? &clipboard : nullptr;
        deps.beforeStep = gate;
        deps.prompt = prompt;
        WorkflowEngine engine(deps);
        return engine.run(wf, token, options);
    }
};

Workflow flow(std::vector<Step> steps) {
    Workflow wf = minimalWorkflow();
    wf.steps = std::move(steps);
    wf.execution.stepTimeoutMs = 300;
    return wf;
}

Expect cond(const std::string& automationId, const std::string& type = "element_exists") {
    Expect e;
    e.type = type;
    e.locator = Locator{uia(automationId, "Button")};
    return e;
}

Step ifStep(const std::string& id, const std::string& automationId, std::vector<Step> then,
            std::vector<Step> otherwise = {}, const std::string& type = "element_exists") {
    Step s = step(id, StepType::If);
    s.condition = cond(automationId, type);
    s.body = std::move(then);
    s.elseBody = std::move(otherwise);
    return s;
}

Step repeatStep(const std::string& id, int times, std::vector<Step> body) {
    Step s = step(id, StepType::Repeat, "");
    s.times = times;
    s.body = std::move(body);
    return s;
}

Step whileStep(const std::string& id, const std::string& automationId, int maxIter, std::vector<Step> body) {
    Step s = step(id, StepType::While);
    s.condition = cond(automationId);
    s.maxIterations = maxIter;
    s.body = std::move(body);
    return s;
}


Result<ElementRef> found() { return Result<ElementRef>::success(FakeElementLocator::element("el")); }
Result<ElementRef> notFound() { return Result<ElementRef>::failure(ErrCode::ElementNotFound, "none"); }

// 按 automationId 脚本化：names 里的存在，其余不存在
void presentOnly(Env& env, std::vector<std::string> names) {
    env.locator.handler = [names](const LeafLocator& l, int) {
        const std::string id = std::get<UiaLocator>(l).automationId;
        for (const auto& n : names) {
            if (n == id) return found();
        }
        return notFound();
    };
}

std::vector<std::string> ids(const RunReport& r) {
    std::vector<std::string> out;
    for (const auto& s : r.steps) out.push_back(s.stepId);
    return out;
}

}  // namespace

// ============================================================ JSON

TEST(nested_blocks_roundtrip_through_json) {
    Workflow wf = flow({ifStep("I", "c", {clickStep("A"), repeatStep("R", 2, {waitStep("W", 10)})},
                               {whileStep("L", "d", 5, {clickStep("B")})}),
                        repeatStep("R2", 1, {clickStep("C")})});
    wf.steps[0].timeoutMs = 750;
    wf.steps[1].intervalMs = 0;
    wf.steps[0].elseBody[0].intervalMs = 250;
    wf.execution.maxStepExecutions = 123;
    const std::string text = writeWorkflow(wf);
    auto back = readWorkflow(text);
    CHECK(back.ok());
    CHECK(back.value.unknownFields.empty());
    CHECK_EQ(writeWorkflow(back.value), text);  // 写 → 读 → 写 完全一致
    CHECK_EQ(back.value.steps[0].body.size(), size_t(2));
    CHECK_EQ(back.value.steps[0].body[1].body[0].id, std::string("W"));
    CHECK_EQ(back.value.steps[0].elseBody[0].maxIterations.value_or(0), 5);
    CHECK_EQ(back.value.execution.maxStepExecutions, 123);
    CHECK(validateWorkflow(back.value).ok());

    // 默认的 maxStepExecutions 不写出
    Workflow plain = flow({clickStep("A")});
    CHECK(writeWorkflow(plain).find("maxStepExecutions") == std::string::npos);
}

TEST(block_fields_on_the_wrong_step_type_are_reported) {
    const char* text = R"({
      "schemaVersion":"1.0","id":"x","name":"x",
      "windows":{"main":{"processName":"a.exe"}},
      "steps":[
        {"id":"A","type":"click","window":"main","locator":{"strategy":"uia","automationId":"b"},"times":3,"then":[]},
        {"id":"B","type":"repeat","times":2,"else":[],"condition":{"type":"element_exists","locator":{"strategy":"uia","automationId":"b"}},"body":[]}
      ]})";
    auto r = readWorkflow(text);
    CHECK(r.ok());
    auto has = [&](const std::string& f) {
        for (const auto& u : r.value.unknownFields) {
            if (u == f) return true;
        }
        return false;
    };
    CHECK(has("steps[0].times"));
    CHECK(has("steps[0].then"));
    CHECK(has("steps[1].else"));
    CHECK(has("steps[1].condition"));
    CHECK(!validateWorkflow(r.value).ok());
}

TEST(deeply_nested_json_is_rejected_by_the_reader) {
    std::string open, close;
    for (int i = 0; i < kMaxNesting + 3; ++i) {
        open += R"({"id":"R)" + std::to_string(i) + R"(","type":"repeat","times":1,"body":[)";
        close += "]}";
    }
    const std::string text = R"({"schemaVersion":"1.0","id":"x","name":"x","windows":{"m":{"processName":"a.exe"}},"steps":[)" +
                             open + close + "]}";
    auto r = readWorkflow(text);
    CHECK(!r.ok());
    CHECK_CONTAINS(r.detail, "nested too deeply");
}

TEST(nested_step_json_paths_in_read_errors) {
    const char* text = R"({"schemaVersion":"1.0","id":"x","name":"x","windows":{"m":{"processName":"a.exe"}},
      "steps":[{"id":"R","type":"repeat","times":1,"body":[{"id":"A","type":"wait","durationMs":"soon"}]}]})";
    auto r = readWorkflow(text);
    CHECK(!r.ok());
    CHECK_CONTAINS(r.detail, "steps[0].body[0].durationMs");
}

// ============================================================ 校验器

TEST(validator_checks_block_fields) {
    Workflow wf = flow({step("I", StepType::If)});
    auto r = validateWorkflow(wf);
    CHECK(r.has("missing_field"));
    CHECK(r.find("missing_field", "steps[0].condition") != nullptr);

    wf.steps[0].condition = cond("c", "element_blinking");
    CHECK(validateWorkflow(wf).has("unknown_condition_type"));

    wf = flow({step("R", StepType::Repeat, "")});
    CHECK(validateWorkflow(wf).find("missing_field", "steps[0].times") != nullptr);
    wf.steps[0].times = 0;
    CHECK(validateWorkflow(wf).has("out_of_range"));
    wf.steps[0].times = kMaxLoopCount + 1;
    CHECK(validateWorkflow(wf).has("out_of_range"));
    wf.steps[0].times = 5;
    wf.steps[0].body = {waitStep("W", 1)};
    CHECK(validateWorkflow(wf).ok());

    wf = flow({step("L", StepType::While)});
    wf.steps[0].condition = cond("c");
    CHECK(validateWorkflow(wf).find("missing_field", "steps[0].maxIterations") != nullptr);  // while 必须有上限
    wf.steps[0].maxIterations = kMaxLoopCount + 1;
    CHECK(!validateWorkflow(wf).ok());
    wf.steps[0].maxIterations = 3;
    wf.steps[0].body = {waitStep("W", 1)};
    CHECK(validateWorkflow(wf).ok());
    wf.steps[0].intervalMs = -1;
    CHECK(!validateWorkflow(wf).ok());
}

TEST(validator_reports_nested_paths_and_global_ids) {
    Workflow wf = flow({ifStep("I", "c", {step("A", StepType::Click)}, {clickStep("I")})});
    auto r = validateWorkflow(wf);
    CHECK(r.find("locator_required", "steps[0].then[0].locator") != nullptr);
    CHECK(r.find("duplicate_step_id", "steps[0].else[0].id") != nullptr);  // id 在整棵树里唯一
}

TEST(validator_block_rules) {
    Workflow wf = flow({ifStep("I", "c", {clickStep("A")})});
    wf.steps[0].expect = cond("x");
    wf.steps[0].retry = RetryPolicy{};
    auto r = validateWorkflow(wf);
    CHECK(r.has("block_no_expect"));
    CHECK(r.has("block_no_retry"));

    wf = flow({ifStep("I", "c", {})});
    r = validateWorkflow(wf);
    CHECK(r.ok());
    CHECK(r.has("empty_block"));  // 空块只是警告

    wf = flow({clickStep("A")});
    wf.steps[0].body = {waitStep("W", 1)};
    CHECK(validateWorkflow(wf).has("children_not_allowed"));

    wf = flow({repeatStep("R", 1, {waitStep("W", 1)})});
    wf.steps[0].elseBody = {waitStep("W2", 1)};
    CHECK(validateWorkflow(wf).has("children_not_allowed"));

    // 条件的 timeoutMs 允许 0；普通步骤不允许
    wf = flow({ifStep("I", "c", {waitStep("W", 1)})});
    wf.steps[0].timeoutMs = 0;
    CHECK(validateWorkflow(wf).ok());
    wf = flow({clickStep("A")});
    wf.steps[0].timeoutMs = 0;
    CHECK(!validateWorkflow(wf).ok());

    // if 的条件里 window 必填、repeat 不要 window
    wf = flow({ifStep("I", "c", {waitStep("W", 1)})});
    wf.steps[0].window = "nope";
    CHECK(validateWorkflow(wf).has("unknown_window"));
    wf = flow({repeatStep("R", 1, {waitStep("W", 1)})});
    CHECK(validateWorkflow(wf).ok());
}

TEST(validator_limits_nesting_depth) {
    Step inner = waitStep("W", 1);
    for (int i = 0; i < kMaxNesting + 2; ++i) inner = repeatStep("R" + std::to_string(i), 1, {inner});
    Workflow wf = flow({inner});
    CHECK(validateWorkflow(wf).has("nesting_too_deep"));

    Step ok = waitStep("W", 1);
    for (int i = 0; i < kMaxNesting; ++i) ok = repeatStep("R" + std::to_string(i), 1, {ok});
    CHECK(validateWorkflow(flow({ok})).ok());  // 恰好 kMaxNesting 层块可以
}

TEST(validator_max_step_executions_range) {
    Workflow wf = flow({waitStep("W", 1)});
    wf.execution.maxStepExecutions = 0;
    CHECK(!validateWorkflow(wf).ok());
    wf.execution.maxStepExecutions = kMaxStepExecutionsLimit + 1;
    CHECK(!validateWorkflow(wf).ok());
    wf.execution.maxStepExecutions = 5;
    CHECK(validateWorkflow(wf).ok());
}

TEST(validator_warns_that_screenshots_stop_after_sensitive_steps) {
    Workflow wf = flow({typeStep("T", "secret", true), clickStep("A")});
    wf.execution.screenshotOnFailure = true;
    CHECK(validateWorkflow(wf).has("screenshot_stops_after_sensitive"));
    wf.execution.screenshotOnFailure = false;
    CHECK(!validateWorkflow(wf).has("screenshot_stops_after_sensitive"));
}

// ============================================================ 引擎：if

TEST(if_runs_then_branch_when_condition_holds) {
    Env env;
    presentOnly(env, {"c"});
    Workflow wf = flow({ifStep("I", "c", {typeStep("A", "yes")}, {typeStep("B", "no")})});
    RunReport r = env.run(wf);
    CHECK(r.state == ExecutionState::Succeeded);
    const std::vector<std::string> expect = {"type:unicode:yes"};
    CHECK(env.input.actions == expect);
    // 记录是先序：块在子步骤之前；块自己的结果是 Ok
    CHECK(ids(r) == (std::vector<std::string>{"I", "A"}));
    CHECK_EQ(r.steps[0].detail, std::string("condition true"));
    CHECK_EQ(r.steps[1].path, std::string("I:then/A"));
    CHECK_EQ(r.steps[0].row, size_t(0));
    CHECK_EQ(r.steps[1].row, size_t(1));
}

TEST(if_runs_else_branch_and_element_missing_inverts) {
    Env env;
    presentOnly(env, {});
    Workflow wf = flow({ifStep("I", "c", {typeStep("A", "yes")}, {typeStep("B", "no")})});
    RunReport r = env.run(wf);
    CHECK(r.state == ExecutionState::Succeeded);
    CHECK(env.input.actions == (std::vector<std::string>{"type:unicode:no"}));
    CHECK_EQ(r.steps[1].path, std::string("I:else/B"));

    Env env2;
    presentOnly(env2, {});
    Workflow wf2 = flow({ifStep("I", "c", {typeStep("A", "gone")}, {}, "element_missing")});
    env2.run(wf2);
    CHECK(env2.input.actions == (std::vector<std::string>{"type:unicode:gone"}));

    Env env3;  // 没有 else 且条件不成立：什么都不做，仍然成功
    presentOnly(env3, {});
    RunReport r3 = env3.run(flow({ifStep("I", "c", {typeStep("A", "x")})}));
    CHECK(r3.state == ExecutionState::Succeeded);
    CHECK(env3.input.actions.empty());
    CHECK_EQ(r3.steps[0].detail, std::string("condition false"));
}

TEST(if_condition_error_fails_instead_of_taking_else) {
    Env env;
    env.locator.handler = [](const LeafLocator&, int) {
        return Result<ElementRef>::failure(ErrCode::ElementAmbiguous, "two matches");
    };
    RunReport r = env.run(flow({ifStep("I", "c", {typeStep("A", "x")}, {typeStep("B", "y")})}));
    CHECK(r.state == ExecutionState::Failed);
    CHECK(r.code == ErrCode::ElementAmbiguous);  // 歧义不能被悄悄当成"不存在"
    CHECK_EQ(r.failedStepId, std::string("I"));
    CHECK(env.input.actions.empty());
}

TEST(if_condition_can_wait_for_the_element) {
    Env env;
    env.locator.handler = [](const LeafLocator&, int call) { return call >= 3 ? found() : notFound(); };
    Workflow wf = flow({ifStep("I", "c", {typeStep("A", "x")}, {typeStep("B", "y")})});
    wf.steps[0].timeoutMs = 1000;
    RunReport r = env.run(wf);
    CHECK(env.input.actions == (std::vector<std::string>{"type:unicode:x"}));  // 等到了 → then
    CHECK(r.state == ExecutionState::Succeeded);

    Env env2;  // 等不到 → 超时不是错误，走 else
    presentOnly(env2, {});
    Workflow wf2 = flow({ifStep("I", "c", {typeStep("A", "x")}, {typeStep("B", "y")})});
    wf2.steps[0].timeoutMs = 300;
    RunReport r2 = env2.run(wf2);
    CHECK(r2.state == ExecutionState::Succeeded);
    CHECK(env2.input.actions == (std::vector<std::string>{"type:unicode:y"}));
    CHECK(env2.clock.sleepCalls >= 2);  // 确实轮询过
}

TEST(if_fails_when_its_window_disappears) {
    Env env;
    env.windows.invalidHandles.insert(1);
    presentOnly(env, {"c"});
    RunReport r = env.run(flow({ifStep("I", "c", {typeStep("A", "x")})}));
    CHECK(r.code == ErrCode::WindowNotFound);
}

// ============================================================ 引擎：repeat / while

TEST(repeat_runs_body_n_times_with_interval) {
    Env env;
    Workflow wf = flow({repeatStep("R", 3, {typeStep("A", "x")})});
    wf.steps[0].intervalMs = 40;
    const auto t0 = env.clock.now;
    RunReport r = env.run(wf);
    CHECK(r.state == ExecutionState::Succeeded);
    CHECK_EQ(env.input.actions.size(), size_t(3));
    CHECK_EQ(env.clock.now - t0, 80);  // 迭代之间睡两次，最后一轮之后不睡
    CHECK_EQ(r.steps.size(), size_t(4));
    CHECK_EQ(r.steps[0].detail, std::string("ran 3 iterations"));
    CHECK_EQ(r.steps[1].path, std::string("R#1/A"));
    CHECK_EQ(r.steps[3].path, std::string("R#3/A"));
}

TEST(while_loops_until_condition_stops_holding) {
    Env env;
    // 条件检查：前两次存在，之后消失。body 里的 click 不查询该 id
    int checks = 0;
    env.locator.handler = [&](const LeafLocator& l, int) {
        const std::string id = std::get<UiaLocator>(l).automationId;
        if (id == "more") return ++checks <= 2 ? found() : notFound();
        return found();
    };
    Workflow wf = flow({whileStep("L", "more", 10, {clickStep("A", "next")})});
    wf.steps[0].intervalMs = 0;
    RunReport r = env.run(wf);
    CHECK(r.state == ExecutionState::Succeeded);
    CHECK_EQ(env.input.actions.size(), size_t(2));
    CHECK_EQ(r.steps[0].detail, std::string("ran 2 iterations"));
}

TEST(while_with_false_condition_never_runs_body) {
    Env env;
    presentOnly(env, {});
    RunReport r = env.run(flow({whileStep("L", "c", 3, {typeStep("A", "x")})}));
    CHECK(r.state == ExecutionState::Succeeded);
    CHECK(env.input.actions.empty());
    CHECK_EQ(r.steps[0].detail, std::string("ran 0 iterations"));
}

TEST(while_exceeding_max_iterations_fails_with_limit_exceeded) {
    Env env;
    presentOnly(env, {"c"});  // 条件永远成立
    Workflow wf = flow({whileStep("L", "c", 3, {typeStep("A", "x")}), typeStep("AFTER", "after")});
    wf.steps[0].intervalMs = 0;
    RunReport r = env.run(wf);
    CHECK(r.state == ExecutionState::Failed);
    CHECK(r.code == ErrCode::LimitExceeded);
    CHECK_EQ(r.failedStepId, std::string("L"));
    CHECK_EQ(env.input.actions.size(), size_t(3));  // 恰好跑满 3 轮，没有第 4 轮，也没有后续步骤
    CHECK_CONTAINS(r.detail, "3 iterations");

    // onFailure=continue 时允许继续后面的步骤，但整体仍是失败
    Env env2;
    presentOnly(env2, {"c"});
    Workflow wf2 = flow({whileStep("L", "c", 2, {typeStep("A", "x")}), typeStep("AFTER", "after")});
    wf2.steps[0].intervalMs = 0;
    wf2.steps[0].onFailure = FailurePolicy::Continue;
    RunReport r2 = env2.run(wf2);
    CHECK(r2.state == ExecutionState::Failed);
    CHECK(r2.code == ErrCode::LimitExceeded);
    CHECK_EQ(env2.input.actions.back(), std::string("type:unicode:after"));
}

TEST(total_step_budget_stops_runaway_nested_loops_even_with_continue) {
    Env env;
    Workflow wf = flow({repeatStep("O", 50, {repeatStep("I", 50, {typeStep("A", "x")})}), typeStep("AFTER", "z")});
    wf.execution.maxStepExecutions = 100;
    wf.execution.onFailure = FailurePolicy::Continue;
    wf.steps[0].intervalMs = 0;
    wf.steps[0].body[0].intervalMs = 0;
    RunReport r = env.run(wf);
    CHECK(r.state == ExecutionState::Failed);
    CHECK(r.code == ErrCode::LimitExceeded);
    CHECK(env.input.actions.size() < size_t(100));
    CHECK(env.input.actions.back() != std::string("type:unicode:z"));  // 预算耗尽不受 continue 影响，直接终止
    CHECK_CONTAINS(r.detail, "maxStepExecutions");
}

TEST(failing_child_stops_the_whole_run_unless_continue) {
    Env env;
    env.input.hook = [](const std::string& a) {
        return a.rfind("type:", 0) == 0 ? Status::failure(ErrCode::InputBlocked, "blocked") : okStatus();
    };
    RunReport r = env.run(flow({repeatStep("R", 3, {typeStep("A", "x")}), waitStep("W", 1)}));
    CHECK(r.state == ExecutionState::Failed);
    CHECK(r.code == ErrCode::InputBlocked);
    CHECK_EQ(r.failedStepId, std::string("A"));
    CHECK_EQ(env.input.actions.size(), size_t(1));  // 第一轮就停了

    Env env2;
    env2.input.hook = env.input.hook;
    Workflow wf = flow({repeatStep("R", 3, {typeStep("A", "x")}), waitStep("W", 1)});
    wf.steps[0].body[0].onFailure = FailurePolicy::Continue;
    RunReport r2 = env2.run(wf);
    CHECK(r2.state == ExecutionState::Failed);  // 失败被记住，最终仍是 Failed
    CHECK(r2.code == ErrCode::InputBlocked);
    CHECK_EQ(env2.input.actions.size(), size_t(3));  // 三轮都跑了
    CHECK_EQ(r2.steps.back().stepId, std::string("W"));
}

TEST(cancel_inside_a_loop_stops_immediately) {
    Env env;
    int n = 0;
    env.input.hook = [&](const std::string&) {
        if (++n == 2) env.token.cancel();
        return okStatus();
    };
    RunReport r = env.run(flow({repeatStep("R", 100, {typeStep("A", "x")})}));
    CHECK(r.state == ExecutionState::Cancelled);
    CHECK_EQ(env.input.actions.size(), size_t(2));
}

// ============================================================ 引擎：闸门与范围

TEST(gate_sees_pre_order_rows_and_every_iteration) {
    Env env;
    presentOnly(env, {"c"});
    std::vector<std::pair<std::string, size_t>> seen;
    env.gate = [&](const Step& s, size_t row) {
        seen.emplace_back(s.id, row);
        return StepGate::Run;
    };
    Workflow wf = flow({ifStep("I", "c", {repeatStep("R", 2, {typeStep("A", "x")})}, {typeStep("E", "e")}),
                        waitStep("W", 1)});
    wf.steps[0].body[0].intervalMs = 0;
    env.run(wf);
    // 行号：I=0 R=1 A=2 E=3 W=4；A 在循环里被闸门看到两次
    const std::vector<std::pair<std::string, size_t>> expect = {
        {"I", 0}, {"R", 1}, {"A", 2}, {"A", 2}, {"W", 4}};
    CHECK(seen == expect);
}

TEST(gate_skip_applies_to_blocks_and_nested_steps) {
    Env env;
    presentOnly(env, {"c"});
    env.gate = [](const Step& s, size_t) { return s.id == "I" ? StepGate::Skip : StepGate::Run; };
    RunReport r = env.run(flow({ifStep("I", "c", {typeStep("A", "x")}), typeStep("B", "b")}));
    CHECK(r.state == ExecutionState::Succeeded);
    CHECK(r.steps[0].skipped);
    CHECK(env.input.actions == (std::vector<std::string>{"type:unicode:b"}));  // 整块被跳过

    Env env2;
    env2.gate = [](const Step& s, size_t) { return s.id == "A" ? StepGate::Skip : StepGate::Run; };
    RunReport r2 = env2.run(flow({repeatStep("R", 2, {typeStep("A", "x"), typeStep("B", "b")})}));
    CHECK_EQ(env2.input.actions.size(), size_t(2));  // 每轮只跳过 A
    size_t skipped = 0;
    for (const auto& s : r2.steps) skipped += s.skipped ? 1 : 0;
    CHECK_EQ(skipped, size_t(2));
}

TEST(range_selecting_a_block_runs_the_whole_block) {
    Env env;
    presentOnly(env, {"c"});
    Workflow wf = flow({typeStep("PRE", "pre"), ifStep("I", "c", {typeStep("A", "a"), typeStep("A2", "a2")}),
                        typeStep("POST", "post")});
    RunOptions o;
    o.firstStep = 1;
    o.lastStep = 1;
    env.run(wf, o);
    CHECK(env.input.actions == (std::vector<std::string>{"type:unicode:a", "type:unicode:a2"}));
}

TEST(range_selecting_a_nested_step_runs_only_that_step_ignoring_the_condition) {
    Env env;
    presentOnly(env, {});  // 条件不成立；若不忽略外层条件，A 不会执行
    Workflow wf = flow({ifStep("I", "c", {typeStep("A", "a"), typeStep("A2", "a2")}), typeStep("POST", "post")});
    RunOptions o;
    o.firstStep = 2;  // A2
    o.lastStep = 2;
    RunReport r = env.run(wf, o);
    CHECK(r.state == ExecutionState::Succeeded);
    CHECK(env.input.actions == (std::vector<std::string>{"type:unicode:a2"}));
    CHECK(env.locator.calls.empty());  // 条件根本没有被评估
    CHECK_EQ(r.steps.size(), size_t(1));
}

TEST(range_preflight_only_resolves_windows_that_will_run) {
    Env env;
    Workflow wf = flow({waitStep("W", 1), ifStep("I", "c", {typeStep("A", "a")})});
    wf.windows["other"] = WindowSpec{"missing.exe", "", 100};
    wf.steps[1].body[0].window = "other";
    RunOptions o;
    o.firstStep = 0;
    o.lastStep = 0;  // 只跑 wait，用不到 other 窗口
    CHECK(env.run(wf, o).state == ExecutionState::Succeeded);
    RunReport r = env.run(wf);  // 全跑则需要 other 窗口，找不到就在第一步之前拒绝
    CHECK(r.code == ErrCode::WindowNotFound);
    CHECK(r.steps.empty());
}

// ============================================================ 失败截图

namespace {
Workflow failingFlow(bool screenshot) {
    Workflow wf = flow({clickStep("S1")});
    wf.execution.screenshotOnFailure = screenshot;
    return wf;
}
RunOptions withDir() {
    RunOptions o;
    o.screenshotDir = "/shots";
    return o;
}
}  // namespace

TEST(failure_takes_a_screenshot_of_the_target_window) {
    Env env;  // locator 默认找不到 → 失败
    RunReport r = env.run(failingFlow(true), withDir());
    CHECK(r.state == ExecutionState::Failed);
    CHECK_EQ(env.shots.shots.size(), size_t(1));
    CHECK_EQ(env.shots.shots[0].first, std::uint64_t(1));
    CHECK_EQ(env.shots.shots[0].second, std::string("/shots/fail-001-S1.png"));
    CHECK_EQ(r.steps[0].screenshot, std::string("/shots/fail-001-S1.png"));
    CHECK(r.screenshots == (std::vector<std::string>{"/shots/fail-001-S1.png"}));
}

TEST(no_screenshot_when_disabled_or_unconfigured_or_successful) {
    Env a;
    a.run(failingFlow(false), withDir());
    CHECK(a.shots.shots.empty());  // 流程没开

    Env b;
    b.run(failingFlow(true));  // 调用方没给目录
    CHECK(b.shots.shots.empty());

    Env c;
    c.withScreenshot = false;
    CHECK(c.run(failingFlow(true), withDir()).state == ExecutionState::Failed);  // 没有实现也不影响结果
    CHECK(c.shots.shots.empty());

    Env d;
    d.locator.handler = [](const LeafLocator&, int) { return found(); };
    d.run(failingFlow(true), withDir());
    CHECK(d.shots.shots.empty());  // 成功不截图
}

TEST(screenshot_failure_does_not_change_the_result_code) {
    Env env;
    env.shots.result = Status::failure(ErrCode::Internal, "disk full");
    RunReport r = env.run(failingFlow(true), withDir());
    CHECK(r.code == ErrCode::ElementNotFound);
    CHECK(r.screenshots.empty());
    CHECK(r.steps[0].screenshot.empty());
    bool logged = false;
    for (const auto& l : r.logs) logged = logged || l.message.find("screenshot failed") != std::string::npos;
    CHECK(logged);
}

TEST(no_screenshot_for_cancel_or_windowless_steps_or_gone_windows) {
    Env env;
    env.input.hook = [&](const std::string&) {
        env.token.cancel();
        return okStatus();
    };
    Workflow wf = flow({typeStep("T", "x")});
    wf.execution.screenshotOnFailure = true;
    env.run(wf, withDir());
    CHECK(env.shots.shots.empty());  // 取消不是失败

    Env env2;  // wait 步骤没有窗口
    env2.clock.onSleep = [&](std::int64_t) { env2.windows.foreground = false; };
    Workflow wf2 = flow({waitStep("W", 5), typeStep("T", "x")});
    wf2.execution.screenshotOnFailure = true;
    RunReport r2 = env2.run(wf2, withDir());
    CHECK(r2.code == ErrCode::InputBlocked);
    CHECK_EQ(env2.shots.shots.size(), size_t(1));  // T 失败截图；W 没有

    Env env3;  // 窗口已经消失：没有可截的东西
    env3.windows.invalidHandles.insert(1);
    RunReport r3 = env3.run(failingFlow(true), withDir());
    CHECK(r3.code == ErrCode::WindowNotFound);
    CHECK(env3.shots.shots.empty());
}

TEST(screenshots_stop_after_a_sensitive_step_has_run) {
    Env env;
    env.input.hook = [](const std::string&) { return Status::failure(ErrCode::InputBlocked, "blocked"); };
    Workflow wf = flow({hotkeyStep("H1", {"ctrl", "a"}), typeStep("T", "secret", true), hotkeyStep("H2", {"ctrl", "b"})});
    wf.execution.screenshotOnFailure = true;
    wf.execution.onFailure = FailurePolicy::Continue;
    RunReport r = env.run(wf, withDir());
    CHECK_EQ(env.shots.shots.size(), size_t(1));  // 只有敏感步骤之前的 H1
    CHECK_EQ(env.shots.shots[0].second, std::string("/shots/fail-001-H1.png"));
    CHECK(r.steps[1].screenshot.empty());  // 敏感步骤自己失败也不截图
    CHECK(r.steps[2].screenshot.empty());

    Env env2;  // runtime prompt 同理
    env2.input.hook = env.input.hook;
    env2.prompt = [](const Step&) { return Result<std::string>::success("pw"); };
    Step p = step("P", StepType::InputText);
    p.textFromPrompt = true;
    Workflow wf2 = flow({p});
    wf2.execution.screenshotOnFailure = true;
    env2.run(wf2, withDir());
    CHECK(env2.shots.shots.empty());
}

TEST(screenshot_count_is_capped_and_file_names_are_sanitized) {
    Env env;
    Workflow wf = flow({repeatStep("R", 30, {clickStep("a/b:c*?")})});
    wf.execution.screenshotOnFailure = true;
    wf.execution.onFailure = FailurePolicy::Continue;
    wf.steps[0].intervalMs = 0;
    RunReport r = env.run(wf, withDir());
    CHECK_EQ(env.shots.shots.size(), size_t(20));
    CHECK_EQ(env.shots.shots[0].second, std::string("/shots/fail-001-a_b_c__.png"));
    CHECK_EQ(env.shots.shots[19].second, std::string("/shots/fail-020-a_b_c__.png"));
    CHECK_EQ(r.screenshots.size(), size_t(20));
}

TEST(failed_block_condition_is_captured_too) {
    Env env;
    env.locator.handler = [](const LeafLocator&, int) {
        return Result<ElementRef>::failure(ErrCode::ElementAmbiguous, "two");
    };
    Workflow wf = flow({ifStep("I", "c", {waitStep("W", 1)})});
    wf.execution.screenshotOnFailure = true;
    env.run(wf, withDir());
    CHECK_EQ(env.shots.shots.size(), size_t(1));
    CHECK_EQ(env.shots.shots[0].second, std::string("/shots/fail-001-I.png"));
}

// ============================================================ 剪贴板输入

namespace {
Workflow clipFlow(const std::string& text = "你好 world") {
    Step t = typeStep("T", text);
    t.method = InputMethod::Clipboard;
    return flow({t});
}
}  // namespace

TEST(clipboard_input_sets_pastes_and_restores_in_order) {
    Env env;
    RunReport r = env.run(clipFlow());
    CHECK(r.state == ExecutionState::Succeeded);
    const std::vector<std::string> expect = {"clip:set:你好 world", "hotkey:CTRL+V", "clip:restore"};
    CHECK(env.trace == expect);
    CHECK_EQ(env.clipboard.content, std::string("ORIGINAL"));  // 用户原有内容回来了
    for (const auto& a : env.input.actions) CHECK(a.rfind("type:", 0) != 0);  // 没有走逐字输入
}

TEST(clipboard_restore_waits_for_the_target_to_read) {
    Env env;
    const auto t0 = env.clock.now;
    env.run(clipFlow());
    CHECK(env.clock.now - t0 >= 200);  // 粘贴之后留出时间再还原，否则目标会读到旧内容
}

TEST(clipboard_input_requires_the_foreground_and_touches_nothing_otherwise) {
    Env env;
    env.windows.foreground = false;
    RunReport r = env.run(clipFlow());
    CHECK(r.code == ErrCode::InputBlocked);
    CHECK_EQ(env.clipboard.replaceCalls, 0);
    CHECK(env.trace.empty());
}

TEST(clipboard_input_without_clipboard_support_is_unsupported) {
    Env env;
    env.withClipboard = false;
    RunReport r = env.run(clipFlow());
    CHECK(r.code == ErrCode::Unsupported);
    CHECK(env.input.actions.empty());
}

TEST(clipboard_is_restored_even_when_the_paste_fails) {
    Env env;
    env.input.hook = [](const std::string& a) {
        return a.rfind("hotkey:", 0) == 0 ? Status::failure(ErrCode::InputBlocked, "nope") : okStatus();
    };
    RunReport r = env.run(clipFlow());
    CHECK(r.code == ErrCode::InputBlocked);
    CHECK_EQ(env.clipboard.restoreCalls, 1);
    CHECK_EQ(env.clipboard.content, std::string("ORIGINAL"));
}

TEST(clipboard_replace_failure_sends_no_paste_and_needs_no_restore) {
    Env env;
    env.clipboard.replaceResult = Status::failure(ErrCode::InputBlocked, "clipboard is locked by another program");
    RunReport r = env.run(clipFlow());
    CHECK(r.code == ErrCode::InputBlocked);
    CHECK_CONTAINS(r.detail, "locked");
    CHECK(env.input.actions.empty());
    CHECK_EQ(env.clipboard.restoreCalls, 0);
}

TEST(clipboard_restore_failure_is_only_a_warning) {
    Env env;
    env.clipboard.restoreResult = Status::failure(ErrCode::Internal, "cannot restore");
    RunReport r = env.run(clipFlow());
    CHECK(r.state == ExecutionState::Succeeded);  // 文本已经输入成功
    bool warned = false;
    for (const auto& l : r.logs) warned = warned || l.message.find("could not restore") != std::string::npos;
    CHECK(warned);
}

TEST(clipboard_is_restored_when_cancelled_right_after_pasting) {
    Env env;
    env.input.hook = [&](const std::string& a) {
        if (a.rfind("hotkey:", 0) == 0) env.token.cancel();
        return okStatus();
    };
    RunReport r = env.run(clipFlow());
    CHECK(r.state == ExecutionState::Cancelled);
    CHECK_EQ(env.clipboard.restoreCalls, 1);
    CHECK_EQ(env.clipboard.content, std::string("ORIGINAL"));
}

TEST(clipboard_text_is_not_shown_in_summary_when_sensitive_is_rejected_by_validator) {
    Workflow wf = clipFlow();
    wf.steps[0].sensitive = true;
    CHECK(validateWorkflow(wf).has("sensitive_clipboard"));
    Env env;
    RunReport r = env.run(wf);
    CHECK(r.code == ErrCode::InvalidWorkflow);  // 引擎也拒绝运行
    CHECK(env.trace.empty());
}

// ============================================================ 摘要

TEST(summary_shows_blocks_with_indentation_and_risks) {
    Workflow wf = flow({ifStep("I", "c", {typeStep("A", "x")}, {repeatStep("R", 2, {clickStep("B")})}),
                        whileStep("L", "d", 7, {waitStep("W", 5)})});
    wf.steps[0].body[0].method = InputMethod::Clipboard;
    wf.execution.screenshotOnFailure = true;
    const std::string text = summarizeWorkflow(wf, validateWorkflow(wf));
    CHECK_CONTAINS(text, "共 6 步");
    CHECK_CONTAINS(text, "如果 控件存在");
    CHECK_CONTAINS(text, "否则：");
    CHECK_CONTAINS(text, "重复 2 次");
    CHECK_CONTAINS(text, "最多 7 次");
    CHECK_CONTAINS(text, "通过剪贴板");
    CHECK_CONTAINS(text, "循环保护");
    CHECK_CONTAINS(text, "自动截取目标窗口");
    // 子步骤比父步骤缩进更深
    const auto posI = text.find("  I  ");
    const auto posA = text.find("      A  ");
    CHECK(posI != std::string::npos);
    CHECK(posA != std::string::npos);
}

// ============================================================ 编辑器

namespace {
WorkflowEditor treeEditor() {
    // 行：I=0 A=1 R=2 B=3 E=4 W=5
    WorkflowEditor e;
    e.load(flow({ifStep("I", "c", {clickStep("A"), repeatStep("R", 2, {clickStep("B")})}, {clickStep("E")}), waitStep("W", 1)}));
    return e;
}
std::string idAt(const WorkflowEditor& e, size_t row) {
    const Step* s = e.stepAtRow(row);
    return s ? s->id : "<none>";
}
}  // namespace

TEST(editor_rows_follow_pre_order) {
    WorkflowEditor e = treeEditor();
    CHECK_EQ(e.rowCount(), size_t(6));
    const std::string order[] = {"I", "A", "R", "B", "E", "W"};
    for (size_t i = 0; i < 6; ++i) CHECK_EQ(idAt(e, i), order[i]);
    CHECK(e.stepAtRow(6) == nullptr);
}

TEST(editor_add_into_after_and_before) {
    WorkflowEditor e = treeEditor();
    auto r = e.addStepInto(0, StepType::Wait, true);  // 加到 if 的 else 末尾
    CHECK(r.ok());
    CHECK_EQ(e.workflow().steps[0].elseBody.size(), size_t(2));
    CHECK_EQ(r.value, size_t(5));  // I A R B E new W
    CHECK(!e.addStepInto(1, StepType::Wait).ok());           // A 不是块
    CHECK(!e.addStepInto(2, StepType::Wait, true).ok());     // repeat 没有 else

    const size_t after = e.addStepAfter(2, StepType::Click);  // 在 R 之后、同级（仍在 then 里）
    CHECK_EQ(after, size_t(4));
    CHECK_EQ(e.workflow().steps[0].body.size(), size_t(3));
    CHECK_EQ(e.workflow().steps[0].body[2].type, StepType::Click);

    const size_t before = e.addStep(0, StepType::Wait);  // 顶层第一个之前
    CHECK_EQ(before, size_t(0));
    CHECK_EQ(e.workflow().steps.size(), size_t(3));
    const size_t atEnd = e.addStep(999, StepType::Wait);  // 越界 → 顶层末尾
    CHECK_EQ(atEnd, e.rowCount() - 1);

    // 所有 id 在整棵树里唯一
    std::set<std::string> seen;
    for (size_t i = 0; i < e.rowCount(); ++i) CHECK(seen.insert(idAt(e, i)).second);
}

TEST(editor_remove_takes_the_subtree) {
    WorkflowEditor e = treeEditor();
    CHECK(e.removeStep(2).ok());  // R 及其 B
    CHECK_EQ(e.rowCount(), size_t(4));
    CHECK_EQ(idAt(e, 2), std::string("E"));
    CHECK(!e.removeStep(99).ok());
    e.undo();
    CHECK_EQ(e.rowCount(), size_t(6));
}

TEST(editor_move_stays_inside_its_own_list) {
    WorkflowEditor e = treeEditor();
    CHECK(e.moveStep(2, -1).ok());  // R 上移到 A 之前
    CHECK_EQ(idAt(e, 1), std::string("R"));
    CHECK_EQ(idAt(e, 2), std::string("B"));  // 子步骤跟着走
    CHECK_EQ(idAt(e, 3), std::string("A"));
    CHECK(!e.moveStep(1, -1).ok());  // 已是 then 列表的第一个
    CHECK(!e.moveStep(4, 1).ok());   // E 是 else 里唯一的
    CHECK(e.moveStep(0, 1).ok());    // 顶层 I 下移到 W 之后
    CHECK_EQ(idAt(e, 0), std::string("W"));
}

TEST(editor_indent_and_outdent) {
    WorkflowEditor e = treeEditor();
    // W 紧跟在 if 之后：缩进进入 if 的 then（末尾）
    auto in = e.indentStep(5);
    CHECK(in.ok());
    CHECK_EQ(e.workflow().steps.size(), size_t(1));
    CHECK_EQ(e.workflow().steps[0].body.back().id, std::string("W"));
    CHECK_EQ(in.value, size_t(4));

    auto out = e.outdentStep(in.value);  // 再移出：回到 if 之后的顶层
    CHECK(out.ok());
    CHECK_EQ(e.workflow().steps.size(), size_t(2));
    CHECK_EQ(e.workflow().steps[1].id, std::string("W"));

    auto toElse = e.indentStep(out.value, true);  // 缩进到 else
    CHECK(toElse.ok());
    CHECK_EQ(e.workflow().steps[0].elseBody.back().id, std::string("W"));

    CHECK(!e.indentStep(0).ok());  // 第一个没有上一个同级
    CHECK(!e.indentStep(1).ok());  // A 之上是块？A 是 then 的第一个 → 没有上一个同级
    CHECK(!e.outdentStep(0).ok());  // 顶层不能再移出
    // 上一个同级不是块
    WorkflowEditor e2;
    e2.load(flow({clickStep("X"), clickStep("Y")}));
    CHECK(!e2.indentStep(1).ok());
}

TEST(editor_indent_into_else_requires_an_if) {
    WorkflowEditor e;
    e.load(flow({repeatStep("R", 1, {waitStep("W0", 1)}), waitStep("W", 1)}));
    CHECK(!e.indentStep(2, true).ok());
    CHECK(e.indentStep(2, false).ok());
}

TEST(editor_duplicate_copies_the_subtree_with_fresh_ids) {
    WorkflowEditor e = treeEditor();
    auto d = e.duplicateStep(2);  // 复制 R（含 B）
    CHECK(d.ok());
    CHECK_EQ(d.value, size_t(4));
    CHECK_EQ(e.workflow().steps[0].body.size(), size_t(3));
    const Step& copy = e.workflow().steps[0].body[2];
    CHECK_EQ(copy.type, StepType::Repeat);
    CHECK_EQ(copy.body.size(), size_t(1));
    std::set<std::string> seen;
    for (size_t i = 0; i < e.rowCount(); ++i) CHECK(seen.insert(idAt(e, i)).second);  // 没有重复 id
    CHECK(validateWorkflow(e.workflow()).ok());
}

TEST(editor_update_keeps_ids_unique_across_the_tree_and_preserves_children) {
    WorkflowEditor e = treeEditor();
    Step s = *e.stepAtRow(3);  // B（嵌套两层）
    s.id = "E";                // 与 else 里的 E 冲突
    CHECK(!e.updateStep(3, s).ok());
    s.id = "B2";
    CHECK(e.updateStep(3, s).ok());
    CHECK_EQ(idAt(e, 3), std::string("B2"));

    Step block = *e.stepAtRow(0);
    block.timeoutMs = 500;
    CHECK(e.updateStep(0, block).ok());
    CHECK_EQ(e.rowCount(), size_t(6));  // 子步骤原样保留
}

TEST(editor_issues_belong_to_the_step_that_owns_them) {
    WorkflowEditor e;
    e.load(flow({ifStep("I", "c", {step("A", StepType::Click)})}));  // A 缺 locator
    CHECK(!e.issuesForStep(1).empty());
    for (const auto& i : e.issuesForStep(1)) CHECK_CONTAINS(i.path, "steps[0].then[0]");
    for (const auto& i : e.issuesForStep(0)) CHECK(i.path.find("then[") == std::string::npos);  // 父步骤不背子步骤的错
}

TEST(editor_window_rename_and_remove_see_nested_steps) {
    WorkflowEditor e = treeEditor();
    CHECK(!e.removeWindow("main").ok());
    CHECK(e.renameWindow("main", "np").ok());
    CHECK_EQ(e.stepAtRow(3)->window, std::string("np"));  // B 嵌套在 R 里
    CHECK_EQ(e.stepAtRow(0)->window, std::string("np"));
}

TEST(editor_refuses_blocks_nested_too_deeply) {
    WorkflowEditor e;
    e.load(flow({repeatStep("R0", 1, {waitStep("W", 1)})}));
    size_t row = 0;
    for (int d = 1; d < kMaxNesting; ++d) {
        auto r = e.addStepInto(row, StepType::Repeat);
        CHECK(r.ok());
        row = r.value;
    }
    CHECK(!e.addStepInto(row, StepType::Repeat).ok());   // 再深一层的块被拒绝
    CHECK(e.addStepInto(row, StepType::Wait).ok());      // 叶子步骤仍可以
    CHECK(validateWorkflow(e.workflow()).errorCount() == 0 || !validateWorkflow(e.workflow()).has("nesting_too_deep"));
}

TEST(editor_default_blocks_need_user_input_before_running) {
    WorkflowEditor e;
    e.load(flow({waitStep("W", 1)}));
    e.addStep(0, StepType::If);
    e.addStep(0, StepType::While);
    e.addStep(0, StepType::Repeat);
    const auto r = validateWorkflow(e.workflow());
    CHECK(!r.ok());  // if / while 的条件定位器还是空的，必须先拾取控件
    CHECK(r.has("uia_no_condition"));
    CHECK_EQ(e.workflow().steps[0].times.value_or(0), 3);
    CHECK(e.workflow().steps[0].window.empty());
    CHECK_EQ(e.workflow().steps[1].maxIterations.value_or(0), 10);
}

#include <functional>
#include <string>
#include <vector>

#include "TestUtil.h"
#include "core/Engine.h"
#include "fakes/Fakes.h"
#include "nlohmann/json.hpp"
#include "testing.h"

using namespace wa;
using namespace wa::fakes;
using namespace testutil;
using json = nlohmann::json;

namespace {

struct FakeScriptRunner : IScriptRunner {
    std::function<Result<ScriptResult>(const ScriptRequest&, const CancelToken&,
                                       const ScriptHostHandler&)> handler;
    std::vector<std::string> inputData;

    Result<ScriptResult> run(const ScriptRequest& request, const CancelToken& token,
                             const ScriptHostHandler& host) override {
        inputData.push_back(request.workflowDataJson);
        return handler ? handler(request, token, host) : Result<ScriptResult>::success({});
    }
};

struct Env {
    FakeClock clock;
    FakeWindowProvider windows;
    FakeElementLocator locator;
    FakeInputController input;
    FakeScriptRunner scripts;
    CancelToken token;

    Env() { windows.windows.push_back(FakeWindowProvider::window(1, "notepad.exe", "Untitled")); }

    RunReport run(const Workflow& workflow, RunOptions options = {}) {
        EngineDeps deps;
        deps.windows = &windows;
        deps.locator = &locator;
        deps.input = &input;
        deps.clock = &clock;
        deps.scripts = &scripts;
        WorkflowEngine engine(deps);
        return engine.run(workflow, token, options);
    }
};

Step script(const std::string& id) {
    Step step;
    step.id = id;
    step.type = StepType::Script;
    step.scriptLanguage = ScriptLanguage::Python;
    step.scriptCode = "pass";
    return step;
}

RunOptions confirmed() {
    RunOptions options;
    options.confirmedRisks = true;
    return options;
}

}  // namespace

TEST(script_steps_share_mutable_data_but_each_run_starts_from_workflow_data) {
    Env env;
    Workflow workflow = minimalWorkflow();
    workflow.initialDataJson = R"({"count":0})";
    workflow.steps = {script("S1"), script("S2")};
    int stepIndex = 0;
    env.scripts.handler = [&stepIndex](const ScriptRequest&, const CancelToken&,
                                       const ScriptHostHandler& host) {
        auto call = [&host](const std::string& operation, const json& args) {
            return host(ScriptHostRequest{1, operation, args.dump()});
        };
        const auto read = call("data.get", json{{"key", "count"}});
        if (!read.ok()) return Result<ScriptResult>::failure(read.code, read.detail);
        const json value = json::parse(read.value.resultJson);
        const int expected = stepIndex == 0 ? 0 : 1;
        if (!value["found"].get<bool>() || value["value"].get<int>() != expected) {
            return Result<ScriptResult>::failure(ErrCode::Internal, "run data was not shared");
        }
        ++stepIndex;
        if (stepIndex == 1) {
            const auto update = call("data.set", json{{"key", "count"}, {"value", 1}});
            if (!update.ok()) return Result<ScriptResult>::failure(update.code, update.detail);
        }
        return Result<ScriptResult>::success({});
    };

    const RunReport first = env.run(workflow, confirmed());
    CHECK(first.state == ExecutionState::Succeeded);
    CHECK_EQ(stepIndex, 2);
    CHECK_EQ(env.scripts.inputData.size(), size_t(2));
    CHECK_EQ(env.scripts.inputData[0], R"({"count":0})");

    stepIndex = 0;
    const RunReport second = env.run(workflow, confirmed());
    CHECK(second.state == ExecutionState::Succeeded);
    CHECK_EQ(stepIndex, 2);
    CHECK_EQ(env.scripts.inputData[2], R"({"count":0})");
    CHECK_EQ(workflow.initialDataJson, R"({"count":0})");
}

TEST(script_broker_dispatches_input_through_checked_host_interfaces) {
    Env env;
    Workflow workflow = minimalWorkflow();
    workflow.steps = {script("S1")};
    env.scripts.handler = [](const ScriptRequest&, const CancelToken&,
                             const ScriptHostHandler& host) {
        const auto response = host(ScriptHostRequest{
            1, "input.text", json{{"window", "main"}, {"text", "private-value"}}.dump()});
        return response.ok() ? Result<ScriptResult>::success({})
                             : Result<ScriptResult>::failure(response.code, response.detail);
    };

    const RunReport report = env.run(workflow, confirmed());
    CHECK(report.state == ExecutionState::Succeeded);
    CHECK_EQ(env.input.actions.size(), size_t(1));
    CHECK_EQ(env.input.actions[0], "type:unicode:private-value");
    CHECK_NOT_CONTAINS(report.detail, "private-value");
    for (const auto& entry : report.logs) CHECK_NOT_CONTAINS(entry.message, "private-value");
}

TEST(script_failure_is_attributed_and_continue_policy_runs_following_steps) {
    Env env;
    Workflow workflow = minimalWorkflow();
    workflow.execution.onFailure = FailurePolicy::Continue;
    workflow.steps = {script("S1"), script("S2")};
    int calls = 0;
    env.scripts.handler = [&calls](const ScriptRequest&, const CancelToken&,
                                   const ScriptHostHandler&) {
        ++calls;
        if (calls == 1) return Result<ScriptResult>::failure(ErrCode::Timeout, "script step timed out");
        return Result<ScriptResult>::success({});
    };

    const RunReport report = env.run(workflow, confirmed());
    CHECK(report.state == ExecutionState::Failed);
    CHECK(report.code == ErrCode::Timeout);
    CHECK_EQ(report.failedStepId, "S1");
    CHECK_EQ(report.steps.size(), size_t(2));
    CHECK(report.steps[1].code == ErrCode::Ok);
    CHECK_EQ(calls, 2);
}

TEST(script_steps_require_explicit_trust_confirmation) {
    Env env;
    Workflow workflow = minimalWorkflow();
    workflow.steps = {script("S1")};
    const RunReport report = env.run(workflow);
    CHECK(report.code == ErrCode::ConfirmationRequired);
    CHECK(env.scripts.inputData.empty());
}

TEST(runtime_and_protocol_failures_are_reported_at_the_script_step) {
    for (const std::string diagnostic : {"Python runtime not found", "malformed worker protocol"}) {
        Env env;
        Workflow workflow = minimalWorkflow();
        workflow.steps = {script("SCRIPT")};
        env.scripts.handler = [&diagnostic](const ScriptRequest&, const CancelToken&,
                                             const ScriptHostHandler&) {
            return Result<ScriptResult>::failure(ErrCode::Unsupported, diagnostic);
        };
        const RunReport report = env.run(workflow, confirmed());
        CHECK(report.state == ExecutionState::Failed);
        CHECK(report.code == ErrCode::Unsupported);
        CHECK_EQ(report.failedStepId, "SCRIPT");
        CHECK_CONTAINS(report.detail, diagnostic);
        CHECK_EQ(report.steps.size(), size_t(1));
        CHECK_EQ(report.steps[0].stepId, "SCRIPT");
    }
}

TEST(script_timeout_and_cancellation_remain_distinct) {
    for (const ErrCode code : {ErrCode::Timeout, ErrCode::Cancelled}) {
        Env env;
        Workflow workflow = minimalWorkflow();
        workflow.steps = {script("SCRIPT")};
        env.scripts.handler = [code](const ScriptRequest&, const CancelToken&,
                                      const ScriptHostHandler&) {
            return Result<ScriptResult>::failure(code, "worker stopped");
        };
        const RunReport report = env.run(workflow, confirmed());
        CHECK(report.code == code);
        CHECK(report.state == (code == ErrCode::Timeout ? ExecutionState::Failed : ExecutionState::Cancelled));
    }
}

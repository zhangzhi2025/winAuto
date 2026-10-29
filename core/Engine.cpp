#include "core/Engine.h"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <map>
#include <memory>
#include <set>
#include <thread>

#include "core/Validator.h"
#include "core/Json.h"
#include "nlohmann/json.hpp"

namespace wa {

namespace {
constexpr int kPollIntervalMs = 100;
using json = nlohmann::json;

bool asLeaf(const Locator& loc, LeafLocator* out) {
    if (const auto* u = std::get_if<UiaLocator>(&loc)) { *out = *u; return true; }
    if (const auto* w = std::get_if<WindowPointLocator>(&loc)) { *out = *w; return true; }
    if (const auto* s = std::get_if<ScreenPointLocator>(&loc)) { *out = *s; return true; }
    if (const auto* i = std::get_if<ImageLocator>(&loc)) { *out = *i; return true; }
    return false;
}

bool validWorkflowData(const json& value, int depth = 0) {
    if (depth > kMaxWorkflowDataDepth) return false;
    if (value.is_object()) {
        for (auto it = value.begin(); it != value.end(); ++it) {
            if (it.key().empty() || it.key().size() > kMaxWorkflowDataKeyLength ||
                !validWorkflowData(it.value(), depth + 1)) return false;
        }
    } else if (value.is_array()) {
        for (const auto& item : value) {
            if (!validWorkflowData(item, depth + 1)) return false;
        }
    } else if (!(value.is_null() || value.is_boolean() || value.is_number() || value.is_string())) {
        return false;
    }
    return true;
}
}  // namespace

bool WorkflowEngine::isRetryable(ErrCode code) {
    return code == ErrCode::ElementNotFound || code == ErrCode::Timeout ||
           code == ErrCode::ExpectationFailed;
}

// ============================================================================ Run

struct WorkflowEngine::Run {
    Run(const EngineDeps& d, const Workflow& w, const CancelToken& t)
        : deps(d), wf(w), token(t) {}

    const EngineDeps& deps;
    const Workflow& wf;
    const CancelToken& token;

    ExecutionStateMachine sm;
    ExecutionLogger logger;
    RunReport report;
    std::int64_t t0 = 0;
    std::map<std::string, std::uint64_t> handles;  // 窗口别名 -> 句柄
    json workflowData = json::object();
    std::chrono::steady_clock::time_point scriptDeadline{};
    std::shared_ptr<CancelToken> scriptCancellation;

    // ---- 流程控制 / 范围 / 截图 的运行状态
    const RunOptions* options = nullptr;
    std::vector<StepRow> rows;                 // 先序展开
    std::map<const Step*, size_t> rowOf;       // 步骤 -> 行号
    size_t first = 0, last = 0;                // 本次运行的行范围
    std::vector<std::string> scope;            // 外层块及迭代，如 {"S3#2"}
    std::int64_t executions = 0;               // 已执行的步骤次数（含块语句与循环里的每一次）
    std::int64_t maxExecutions = kDefaultMaxStepExecutions;
    std::optional<RunReport> term;             // 一旦设置，整个运行立刻结束
    ErrCode firstCode = ErrCode::Ok;           // continue 策略下记住第一个失败
    std::string firstStepId;
    std::string firstDetail;
    bool sensitiveSeen = false;                // 已经执行过敏感 / 运行时询问的步骤：之后不再截图
    int shotSeq = 0;

    // ------------------------------------------------------------ 基础
    std::int64_t elapsed() const { return deps.clock->nowMs() - t0; }

    void setState(ExecutionState s) {
        if (sm.transition(s)) {
            report.state = s;
            if (deps.observer.onStateChanged) deps.observer.onStateChanged(s);
        }
    }

    void log(LogLevel lv, const std::string& stepId, const std::string& msg) {
        logger.log(lv, elapsed(), stepId, msg);
    }

    // 进入终态前统一收尾，保证所有输出字符串都经过脱敏
    RunReport finish() {
        report.detail = logger.scrub(report.detail);
        for (auto& s : report.steps) s.detail = logger.scrub(s.detail);
        report.logs = logger.entries();
        return std::move(report);
    }

    RunReport fail(ErrCode code, const std::string& stepId, const std::string& detail) {
        report.code = code;
        report.failedStepId = stepId;
        report.detail = detail;
        log(LogLevel::Error, stepId,
            std::string("run failed: ") + errName(code) + (detail.empty() ? "" : " - " + detail));
        setState(ExecutionState::Failed);
        return finish();
    }

    RunReport cancelled(const std::string& stepId) {
        report.code = ErrCode::Cancelled;
        report.failedStepId = stepId;
        report.detail = "cancelled by user";
        log(LogLevel::Warn, stepId, "run cancelled");
        setState(ExecutionState::Cancelling);
        setState(ExecutionState::Cancelled);
        return finish();
    }

    // ------------------------------------------------------------ 窗口
    Result<std::uint64_t> resolveWindow(const WindowSpec& spec) {
        const std::int64_t start = deps.clock->nowMs();
        for (;;) {
            if (token.isCancelled()) return Result<std::uint64_t>::failure(ErrCode::Cancelled);
            Result<WindowInfo> r = deps.windows->findWindow(spec);
            if (r.ok()) {
                if (r.value.elevated && !deps.windows->isSelfElevated()) {
                    return Result<std::uint64_t>::failure(
                        ErrCode::WindowElevated,
                        "target process \"" + r.value.processName +
                            "\" runs with higher privileges; automation is refused");
                }
                return Result<std::uint64_t>::success(r.value.handle);
            }
            if (r.code != ErrCode::WindowNotFound) return failAs<std::uint64_t>(r);
            const std::int64_t remaining = spec.timeoutMs - (deps.clock->nowMs() - start);
            if (remaining <= 0) {
                return Result<std::uint64_t>::failure(
                    ErrCode::WindowNotFound,
                    r.detail.empty() ? "window \"" + spec.processName + "\" not found" : r.detail);
            }
            if (!deps.clock->sleepFor(static_cast<int>(std::min<std::int64_t>(kPollIntervalMs, remaining)),
                                      token)) {
                return Result<std::uint64_t>::failure(ErrCode::Cancelled);
            }
        }
    }

    Status launchProgram(const Step& step) {
        if (!deps.processes) {
            return Status::failure(ErrCode::Unsupported, "no process launcher is available");
        }
        Result<std::uint32_t> started = deps.processes->launch(step.programPath, step.arguments);
        if (!started.ok()) return failAs<Unit>(started);
        if (started.value == 0) {
            return Status::failure(ErrCode::Internal, "process launcher returned an invalid process ID");
        }

        const int timeoutMs = timeoutFor(step);
        const std::int64_t start = deps.clock->nowMs();
        for (;;) {
            if (token.isCancelled()) return Status::failure(ErrCode::Cancelled);
            std::vector<WindowInfo> matches;
            for (const WindowInfo& window : deps.windows->enumerateWindows()) {
                if (window.processId == started.value) matches.push_back(window);
            }
            if (matches.size() > 1) {
                return Status::failure(ErrCode::WindowNotFound,
                                       "launched process has multiple visible windows; refusing to choose one");
            }
            if (matches.size() == 1) {
                if (matches.front().elevated && !deps.windows->isSelfElevated()) {
                    return Status::failure(ErrCode::WindowElevated,
                                           "launched program runs with higher privileges; automation is refused");
                }
                handles[step.window] = matches.front().handle;
                log(LogLevel::Info, step.id, "launched program window bound to \"" + step.window + "\"");
                return okStatus();
            }
            const std::int64_t remaining = timeoutMs - (deps.clock->nowMs() - start);
            if (remaining <= 0) {
                return Status::failure(ErrCode::Timeout,
                                       "no visible window appeared for the launched process within " +
                                           std::to_string(timeoutMs) + " ms");
            }
            if (!deps.clock->sleepFor(static_cast<int>(std::min<std::int64_t>(kPollIntervalMs, remaining)),
                                      token)) {
                return Status::failure(ErrCode::Cancelled);
            }
        }
    }

    // ------------------------------------------------------------ 定位
    // 单次定位。fallback 规则：
    //   - ElementNotFound：试下一个
    //   - Unsupported（该策略当前平台不可用，如还没实现的 uia/image）：跳过，试下一个
    //   - 其他错误（歧义、取消、窗口丢失等）：立刻失败，绝不继续猜
    //   - 全部走完：只要有过"没找到"就报 ElementNotFound；全部都不可用则保持 Unsupported，
    //     不把"做不到"伪装成"没找到"（否则会被当作可重试的错误）
    Result<ElementRef> locateOnce(std::uint64_t handle, const Locator& loc) {
        if (const auto* fb = std::get_if<FallbackLocator>(&loc)) {
            bool sawNotFound = false;
            std::string unsupportedDetail;
            for (const auto& item : fb->items) {
                Result<ElementRef> r = deps.locator->locate(handle, item, token);
                if (r.ok()) return r;
                if (r.code == ErrCode::ElementNotFound) {
                    sawNotFound = true;
                } else if (r.code == ErrCode::Unsupported) {
                    if (unsupportedDetail.empty()) unsupportedDetail = r.detail;
                } else {
                    return r;
                }
            }
            if (sawNotFound) {
                return Result<ElementRef>::failure(ErrCode::ElementNotFound,
                                                   "no locator in the fallback chain matched");
            }
            return Result<ElementRef>::failure(
                ErrCode::Unsupported, "no locator in the fallback chain is available: " + unsupportedDetail);
        }
        LeafLocator leaf;
        if (!asLeaf(loc, &leaf)) return Result<ElementRef>::failure(ErrCode::Internal, "bad locator");
        return deps.locator->locate(handle, leaf, token);
    }

    // 轮询直到找到或超时。timeoutCode：超时时返回的错误码。
    // requireEnabled：找到但被禁用的控件视为"还没准备好"，继续轮询（按钮常在表单填完后才启用）；
    // 超时后给出"found but disabled"的明确原因，而不是笼统的 not found。
    Result<ElementRef> locatePolling(std::uint64_t handle, const Locator& loc, int timeoutMs,
                                     ErrCode timeoutCode, bool requireEnabled = false) {
        const std::int64_t start = deps.clock->nowMs();
        bool sawDisabled = false;
        for (;;) {
            if (token.isCancelled()) return Result<ElementRef>::failure(ErrCode::Cancelled);
            Result<ElementRef> r = locateOnce(handle, loc);
            if (r.ok() && requireEnabled && !r.value.enabled) {
                sawDisabled = true;
                r = Result<ElementRef>::failure(ErrCode::ElementNotFound, "element is disabled");
            }
            if (r.ok() || r.code != ErrCode::ElementNotFound) return r;
            const std::int64_t remaining = timeoutMs - (deps.clock->nowMs() - start);
            if (remaining <= 0) {
                return Result<ElementRef>::failure(
                    timeoutCode, std::string(sawDisabled ? "element found but still disabled after "
                                                         : "element not found within ") +
                                     std::to_string(timeoutMs) + " ms");
            }
            if (!deps.clock->sleepFor(static_cast<int>(std::min<std::int64_t>(kPollIntervalMs, remaining)),
                                      token)) {
                return Result<ElementRef>::failure(ErrCode::Cancelled);
            }
        }
    }

    // ------------------------------------------------------------ 单步
    int timeoutFor(const Step& s) const { return s.timeoutMs.value_or(wf.execution.stepTimeoutMs); }

    static Status notForeground(const Step& s) {
        return Status::failure(ErrCode::InputBlocked,
                               "target window \"" + s.window +
                                   "\" is not in the foreground; add an activate_window step before keyboard input");
    }

    Status execOnce(const Step& s, std::uint64_t handle, const std::string& text) {
        switch (s.type) {
            case StepType::ActivateWindow:
                return deps.windows->activateWindow(handle);

            case StepType::Click: {
                Result<ElementRef> r = locatePolling(handle, *s.locator, timeoutFor(s),
                                                     ErrCode::ElementNotFound, true);
                if (!r.ok()) return failAs<Unit>(r);
                if (s.clickMode == ClickMode::Auto && r.value.supportsInvoke) {
                    return deps.input->invoke(r.value);
                }
                return deps.input->clickPoint(r.value.bounds.center(), handle);
            }

            case StepType::MouseMove:
            case StepType::MouseClick: {
                const bool click = s.type == StepType::MouseClick;
                Result<ElementRef> r = locatePolling(handle, *s.locator, timeoutFor(s),
                                                     ErrCode::ElementNotFound, click);
                if (!r.ok()) return failAs<Unit>(r);
                const Point point = r.value.bounds.center();
                return click ? deps.input->clickPoint(point, handle) : deps.input->movePoint(point, handle);
            }

            case StepType::SetValue: {
                Result<ElementRef> r = locatePolling(handle, *s.locator, timeoutFor(s),
                                                     ErrCode::ElementNotFound, true);
                if (!r.ok()) return failAs<Unit>(r);
                if (!r.value.supportsValue) {
                    return Status::failure(ErrCode::Unsupported,
                                           "target control does not support ValuePattern");
                }
                return deps.input->setValue(r.value, text);
            }

            case StepType::InputText:
                if (!deps.windows->isForeground(handle)) return notForeground(s);
                if (s.method == InputMethod::Clipboard) return pasteViaClipboard(s, text);
                return deps.input->typeText(text, s.method);

            case StepType::Hotkey: {
                if (!deps.windows->isForeground(handle)) return notForeground(s);
                std::vector<std::string> keys;
                for (const auto& k : s.keys) keys.push_back(normalizeKey(k));
                return deps.input->hotkey(keys);
            }

            case StepType::Wait:
                if (!deps.clock->sleepFor(s.durationMs.value_or(0), token)) {
                    return Status::failure(ErrCode::Cancelled);
                }
                return okStatus();

            case StepType::WaitForElement: {
                Result<ElementRef> r =
                    locatePolling(handle, *s.locator, timeoutFor(s), ErrCode::Timeout);
                return r.ok() ? okStatus() : failAs<Unit>(r);
            }

            case StepType::AssertElement: {
                Result<ElementRef> r = locateOnce(handle, *s.locator);
                return r.ok() ? okStatus() : failAs<Unit>(r);
            }

            case StepType::If:
            case StepType::Repeat:
            case StepType::While:
                return Status::failure(ErrCode::Internal, "block steps are not executed as leaf steps");

            case StepType::Script:
                return runScript(s);

            case StepType::LaunchProgram:
                return launchProgram(s);

            case StepType::Unknown:
                break;
        }
        return Status::failure(ErrCode::Internal, "unknown step type");
    }

    Result<ScriptHostResponse> handleScriptRequest(const ScriptHostRequest& request) {
        if (token.isCancelled()) {
            return Result<ScriptHostResponse>::failure(ErrCode::Cancelled, "script cancelled");
        }
        if (scriptCancellation && scriptCancellation->isCancelled()) {
            return Result<ScriptHostResponse>::failure(ErrCode::Timeout, "script step deadline exceeded");
        }
        if (std::chrono::steady_clock::now() >= scriptDeadline) {
            return Result<ScriptHostResponse>::failure(ErrCode::Timeout, "script step deadline exceeded");
        }
        json args;
        try {
            if (request.argumentsJson.size() > kMaxWorkflowDataBytes) {
                return Result<ScriptHostResponse>::failure(ErrCode::LimitExceeded, "host request too large");
            }
            args = json::parse(request.argumentsJson);
        } catch (const std::exception&) {
            return Result<ScriptHostResponse>::failure(ErrCode::Unsupported, "invalid host request JSON");
        }
        if (!args.is_object()) {
            return Result<ScriptHostResponse>::failure(ErrCode::Unsupported, "host request arguments must be an object");
        }

        auto success = [](const json& value) {
            return Result<ScriptHostResponse>::success(ScriptHostResponse{value.dump()});
        };
        auto stringArg = [&args](const char* name) -> const std::string* {
            const auto it = args.find(name);
            return it != args.end() && it->is_string() ? &it->get_ref<const std::string&>() : nullptr;
        };
        if (request.operation == "data.get") {
            const std::string* key = stringArg("key");
            if (!key || key->empty() || key->size() > kMaxWorkflowDataKeyLength) {
                return Result<ScriptHostResponse>::failure(ErrCode::Unsupported, "data.get requires a valid key");
            }
            const auto it = workflowData.find(*key);
            return success(json{{"found", it != workflowData.end()},
                                {"value", it == workflowData.end() ? json(nullptr) : it.value()}});
        }
        if (request.operation == "data.set") {
            const std::string* key = stringArg("key");
            const auto value = args.find("value");
            if (!key || key->empty() || key->size() > kMaxWorkflowDataKeyLength || value == args.end()) {
                return Result<ScriptHostResponse>::failure(ErrCode::Unsupported, "data.set requires a key and value");
            }
            json updated = workflowData;
            updated[*key] = *value;
            if (!validWorkflowData(updated) || updated.dump().size() > kMaxWorkflowDataBytes) {
                return Result<ScriptHostResponse>::failure(ErrCode::LimitExceeded, "workflow data limit exceeded");
            }
            if (scriptCancellation && scriptCancellation->isCancelled()) {
                return Result<ScriptHostResponse>::failure(ErrCode::Timeout, "script step deadline exceeded");
            }
            workflowData = std::move(updated);
            return success(json{{"ok", true}});
        }
        if (request.operation == "window.activate") {
            const std::string* alias = stringArg("window");
            const auto it = alias ? handles.find(*alias) : handles.end();
            if (it == handles.end()) {
                return Result<ScriptHostResponse>::failure(ErrCode::WindowNotFound, "unknown workflow window alias");
            }
            if (!deps.windows->isWindowValid(it->second)) {
                return Result<ScriptHostResponse>::failure(ErrCode::WindowNotFound, "workflow window is no longer valid");
            }
            if (scriptCancellation && scriptCancellation->isCancelled()) {
                return Result<ScriptHostResponse>::failure(ErrCode::Timeout, "script step deadline exceeded");
            }
            const Status activated = deps.windows->activateWindow(it->second);
            return activated.ok() ? success(json{{"ok", true}})
                                  : Result<ScriptHostResponse>::failure(activated.code, activated.detail);
        }

        const bool elementOperation = request.operation == "element.read" ||
                                      request.operation == "element.click" ||
                                      request.operation == "element.move" ||
                                      request.operation == "element.set_value";
        const bool inputOperation = request.operation == "input.text" ||
                                    request.operation == "input.hotkey";
        if (!elementOperation && !inputOperation) {
            return Result<ScriptHostResponse>::failure(ErrCode::Unsupported, "unsupported script host operation");
        }
        const std::string* alias = stringArg("window");
        const auto handleIt = alias ? handles.find(*alias) : handles.end();
        if (handleIt == handles.end()) {
            return Result<ScriptHostResponse>::failure(ErrCode::WindowNotFound, "unknown workflow window alias");
        }
        const std::uint64_t handle = handleIt->second;
        if (!deps.windows->isWindowValid(handle)) {
            return Result<ScriptHostResponse>::failure(ErrCode::WindowNotFound, "workflow window is no longer valid");
        }
        if (request.operation == "input.text" || request.operation == "input.hotkey") {
            if (!deps.windows->isForeground(handle)) {
                return Result<ScriptHostResponse>::failure(ErrCode::InputBlocked, "target window is not in the foreground");
            }
            if (request.operation == "input.text") {
                const std::string* text = stringArg("text");
                if (!text) return Result<ScriptHostResponse>::failure(ErrCode::Unsupported, "input.text requires text");
                if (scriptCancellation && scriptCancellation->isCancelled()) {
                    return Result<ScriptHostResponse>::failure(ErrCode::Timeout, "script step deadline exceeded");
                }
                logger.addSecret(*text);
                const Status input = deps.input->typeText(*text, InputMethod::Unicode);
                return input.ok() ? success(json{{"ok", true}})
                                  : Result<ScriptHostResponse>::failure(input.code, input.detail);
            }
            const auto keys = args.find("keys");
            if (keys == args.end() || !keys->is_array() || keys->empty() || keys->size() > 8) {
                return Result<ScriptHostResponse>::failure(ErrCode::Unsupported, "input.hotkey requires 1 to 8 keys");
            }
            std::vector<std::string> normalized;
            for (const auto& key : *keys) {
                if (!key.is_string()) return Result<ScriptHostResponse>::failure(ErrCode::Unsupported, "hotkey keys must be strings");
                normalized.push_back(normalizeKey(key.get<std::string>()));
            }
            Workflow probe;
            probe.schemaVersion = wf.schemaVersion;
            probe.id = "script-hotkey";
            probe.name = "script-hotkey";
            probe.windows.emplace(*alias, wf.windows.at(*alias));
            Step hotkeyStep;
            hotkeyStep.id = "HOTKEY";
            hotkeyStep.type = StepType::Hotkey;
            hotkeyStep.window = *alias;
            hotkeyStep.keys = normalized;
            probe.steps.push_back(std::move(hotkeyStep));
            const ValidationReport checked = validateWorkflow(probe);
            if (!checked.ok()) {
                return Result<ScriptHostResponse>::failure(ErrCode::Unsupported, "invalid script hotkey");
            }
            if (scriptCancellation && scriptCancellation->isCancelled()) {
                return Result<ScriptHostResponse>::failure(ErrCode::Timeout, "script step deadline exceeded");
            }
            const Status sent = deps.input->hotkey(normalized);
            return sent.ok() ? success(json{{"ok", true}})
                             : Result<ScriptHostResponse>::failure(sent.code, sent.detail);
        }
        const auto locatorJson = args.find("locator");
        if (locatorJson == args.end()) {
            return Result<ScriptHostResponse>::failure(ErrCode::Unsupported, "automation request requires a locator");
        }
        Result<Locator> parsed = readLocatorJson(locatorJson->dump());
        if (!parsed.ok()) {
            return Result<ScriptHostResponse>::failure(ErrCode::Unsupported, "invalid automation locator");
        }
        Workflow probe;
        probe.schemaVersion = wf.schemaVersion;
        probe.id = "script-locator";
        probe.name = "script-locator";
        probe.windows.emplace(*alias, wf.windows.at(*alias));
        Step target;
        target.id = "ELEMENT";
        target.type = StepType::Click;
        target.window = *alias;
        target.locator = parsed.value;
        probe.steps.push_back(std::move(target));
        const ValidationReport checked = validateWorkflow(probe);
        if (!checked.ok()) {
            return Result<ScriptHostResponse>::failure(ErrCode::Unsupported, "invalid automation locator");
        }
        LeafLocator locator;
        if (!asLeaf(parsed.value, &locator)) {
            return Result<ScriptHostResponse>::failure(ErrCode::Unsupported, "fallback locators are not supported by scripts");
        }
        const CancelToken& hostToken = scriptCancellation ? *scriptCancellation : token;
        Result<ElementRef> found = deps.locator->locate(handle, locator, hostToken);
        if (!found.ok()) return Result<ScriptHostResponse>::failure(found.code, found.detail);
        if (token.isCancelled()) {
            return Result<ScriptHostResponse>::failure(ErrCode::Cancelled, "script cancelled");
        }
        if ((scriptCancellation && scriptCancellation->isCancelled()) ||
            std::chrono::steady_clock::now() >= scriptDeadline) {
            return Result<ScriptHostResponse>::failure(ErrCode::Timeout, "script step deadline exceeded");
        }
        if (!found.value.enabled && request.operation != "element.read" &&
            request.operation != "element.move") {
            return Result<ScriptHostResponse>::failure(ErrCode::InputBlocked, "target element is disabled");
        }
        if (request.operation == "element.read") {
            return success(json{{"name", found.value.name},
                                {"controlType", found.value.controlType},
                                {"bounds", {{"x", found.value.bounds.x}, {"y", found.value.bounds.y},
                                            {"width", found.value.bounds.w}, {"height", found.value.bounds.h}}},
                                {"enabled", found.value.enabled}});
        }
        if (request.operation == "element.click") {
            if (found.value.supportsInvoke) {
                const Status invoked = deps.input->invoke(found.value);
                return invoked.ok() ? success(json{{"ok", true}})
                                    : Result<ScriptHostResponse>::failure(invoked.code, invoked.detail);
            }
            const Status click = deps.input->clickPoint(found.value.bounds.center(), handle);
            return click.ok() ? success(json{{"ok", true}})
                              : Result<ScriptHostResponse>::failure(click.code, click.detail);
        }
        if (request.operation == "element.move") {
            if (scriptCancellation && scriptCancellation->isCancelled()) {
                return Result<ScriptHostResponse>::failure(ErrCode::Timeout, "script step deadline exceeded");
            }
            const Status move = deps.input->movePoint(found.value.bounds.center(), handle);
            return move.ok() ? success(json{{"ok", true}})
                             : Result<ScriptHostResponse>::failure(move.code, move.detail);
        }
        if (request.operation == "element.set_value") {
            const std::string* text = stringArg("text");
            if (!text) return Result<ScriptHostResponse>::failure(ErrCode::Unsupported, "set_value requires text");
            if (!found.value.supportsValue) {
                return Result<ScriptHostResponse>::failure(ErrCode::Unsupported, "element does not support ValuePattern");
            }
            logger.addSecret(*text);
            const Status set = deps.input->setValue(found.value, *text);
            return set.ok() ? success(json{{"ok", true}})
                            : Result<ScriptHostResponse>::failure(set.code, set.detail);
        }
        return Result<ScriptHostResponse>::failure(ErrCode::Unsupported, "unsupported script host operation");
    }

    Status runScript(const Step& step) {
        if (!deps.scripts) return Status::failure(ErrCode::Unsupported, "script runtime is not configured");
        if (!step.scriptLanguage || !step.scriptCode) {
            return Status::failure(ErrCode::InvalidWorkflow, "script language or source is missing");
        }
        scriptDeadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutFor(step));
        scriptCancellation = std::make_shared<CancelToken>();
        auto stopped = std::make_shared<std::atomic<bool>>(false);
        const auto cancellation = scriptCancellation;
        const auto deadline = scriptDeadline;
        std::thread watchdog([this, stopped, cancellation, deadline] {
            while (!stopped->load(std::memory_order_acquire)) {
                if (token.isCancelled() || std::chrono::steady_clock::now() >= deadline) {
                    cancellation->cancel();
                    return;
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
            }
        });
        ScriptRequest request{step.id, *step.scriptLanguage, *step.scriptCode, timeoutFor(step), workflowData.dump()};
        const ScriptHostHandler host = [this](const ScriptHostRequest& operation) {
            Result<ScriptHostResponse> response = handleScriptRequest(operation);
            if (token.isCancelled()) {
                return Result<ScriptHostResponse>::failure(ErrCode::Cancelled, "script cancelled");
            }
            if (std::chrono::steady_clock::now() >= scriptDeadline) {
                return Result<ScriptHostResponse>::failure(ErrCode::Timeout, "script step deadline exceeded");
            }
            return response;
        };
        Result<ScriptResult> result = deps.scripts->run(request, *scriptCancellation, host);
        stopped->store(true, std::memory_order_release);
        watchdog.join();
        scriptCancellation.reset();
        scriptDeadline = {};
        if (token.isCancelled()) return Status::failure(ErrCode::Cancelled, "script cancelled by user");
        if (std::chrono::steady_clock::now() >= deadline) {
            return Status::failure(ErrCode::Timeout, "script step timed out");
        }
        if (!result.ok()) return Status::failure(result.code, result.detail);
        return okStatus();
    }

    // 剪贴板输入：保存剪贴板 → 写入文本 → Ctrl+V → 等目标读完 → 还原。
    // 还原一定会执行（包括粘贴失败、被取消）；"等目标读完"用不可取消的短睡眠，避免取消时立刻还原，
    // 让目标程序粘贴到旧内容。
    static constexpr int kPasteSettleMs = 250;
    Status pasteViaClipboard(const Step& s, const std::string& text) {
        if (!deps.clipboard) {
            return Status::failure(ErrCode::Unsupported, "clipboard access is not available in this environment");
        }
        Status st = deps.clipboard->replaceText(text);
        if (!st.ok()) return st;  // 实现保证失败时剪贴板未被改动，无需还原
        const Status sent = deps.input->hotkey({"CTRL", "V"});
        if (sent.ok()) {
            const CancelToken never;
            deps.clock->sleepFor(kPasteSettleMs, never);
        }
        const Status restored = deps.clipboard->restore();
        if (!restored.ok()) {
            log(LogLevel::Warn, s.id, "could not restore the clipboard: " + restored.detail);
        } else if (!restored.detail.empty()) {
            log(LogLevel::Info, s.id, restored.detail);
        }
        if (!sent.ok()) return sent;
        if (token.isCancelled()) return Status::failure(ErrCode::Cancelled);
        return okStatus();
    }

    Status checkExpect(const Step& s, std::uint64_t handle) {
        Result<ElementRef> r =
            locatePolling(handle, s.expect->locator, timeoutFor(s), ErrCode::ExpectationFailed);
        if (r.ok()) return okStatus();
        if (r.code == ErrCode::ElementNotFound) {
            return Status::failure(ErrCode::ExpectationFailed, "expected element did not appear");
        }
        return failAs<Unit>(r);
    }

    // ------------------------------------------------------------ 记录
    std::string pathFor(const Step& s) const {
        std::string p;
        for (const auto& sc : scope) p += sc + "/";
        return p + s.id;
    }

    StepRecord beginRecord(const Step& s) {
        StepRecord rec;
        rec.stepId = s.id;
        rec.type = toString(s.type);
        rec.row = rowOf.at(&s);
        rec.path = pathFor(s);
        rec.startMs = elapsed();
        if (deps.observer.onStepStarted) deps.observer.onStepStarted(s.id);
        log(LogLevel::Info, s.id, std::string("start ") + toString(s.type));
        return rec;
    }

    static std::string safeName(const std::string& id) {
        std::string out;
        for (unsigned char c : id) {
            out += (std::isalnum(c) || c == '-' || c == '_') ? static_cast<char>(c) : '_';
        }
        if (out.empty()) out = "step";
        if (out.size() > 40) out.resize(40);
        return out;
    }

    // 失败截图。规则：工作流开了开关 + 调用方给了目录 + 有截图实现；目标窗口还在；
    // 之前没执行过敏感/询问步骤；单次运行最多 kMaxFailureShots 张（循环里反复失败不会写满磁盘）。
    // 截图失败只记日志，不改变步骤结果。
    static constexpr int kMaxFailureShots = 20;
    void maybeScreenshot(const Step& s, StepRecord& rec) {
        if (!wf.execution.screenshotOnFailure || !options || options->screenshotDir.empty() || !deps.screenshot) return;
        if (sensitiveSeen || rec.code == ErrCode::Ok || rec.code == ErrCode::Cancelled) return;
        if (!requiresWindow(s.type) || s.window.empty()) return;
        if (static_cast<int>(report.screenshots.size()) >= kMaxFailureShots) return;
        const auto h = handles.find(s.window);
        if (h == handles.end() || !deps.windows->isWindowValid(h->second)) return;

        char num[16];
        std::snprintf(num, sizeof num, "%03d", ++shotSeq);
        const std::string path = options->screenshotDir + "/fail-" + num + "-" + safeName(s.id) + ".png";
        const Status st = deps.screenshot->captureWindow(h->second, path);
        if (st.ok()) {
            rec.screenshot = path;
            report.screenshots.push_back(path);
            log(LogLevel::Info, s.id, "screenshot saved: " + path);
        } else {
            log(LogLevel::Warn, s.id, "screenshot failed: " + st.detail);
        }
    }

    // 一条记录写完之后的统一收尾：截图 → 通知 → 失败策略。返回 true 表示整个运行应立即结束（term 已设置）。
    bool finishStep(const Step& s, size_t slot) {
        StepRecord& rec = report.steps[slot];
        if (rec.code != ErrCode::Ok && rec.code != ErrCode::Cancelled) maybeScreenshot(s, rec);
        if (deps.observer.onStepFinished) deps.observer.onStepFinished(rec);

        if (rec.code == ErrCode::Ok) return false;
        if (rec.code == ErrCode::Cancelled) {
            term = cancelled(s.id);
            return true;
        }
        if (firstCode == ErrCode::Ok) {
            firstCode = rec.code;
            firstStepId = s.id;
            firstDetail = rec.detail;
        }
        const FailurePolicy policy = s.onFailure.value_or(wf.execution.onFailure);
        if (policy == FailurePolicy::Stop) {
            term = fail(rec.code, s.id, rec.detail);
            return true;
        }
        log(LogLevel::Warn, s.id, "failure ignored (onFailure=continue)");
        return false;
    }

    // ------------------------------------------------------------ 条件
    // 条件是否成立。timeoutMs>0 时轮询到成立或超时（超时 = 不成立，不是错误）。
    // 定位出错（歧义、窗口丢失、策略不可用）是错误，绝不当作"不存在"：否则歧义会被悄悄当成 false 走另一条分支。
    Result<bool> evalCondition(const Step& s, std::uint64_t handle) {
        const bool wantMissing = s.condition->type == "element_missing";
        const int timeoutMs = s.timeoutMs.value_or(0);
        const std::int64_t start = deps.clock->nowMs();
        for (;;) {
            if (token.isCancelled()) return Result<bool>::failure(ErrCode::Cancelled);
            if (!deps.windows->isWindowValid(handle)) {
                return Result<bool>::failure(ErrCode::WindowNotFound,
                                             "window \"" + s.window + "\" is no longer valid");
            }
            const Result<ElementRef> r = locateOnce(handle, s.condition->locator);
            bool exists = false;
            if (r.ok()) exists = true;
            else if (r.code != ErrCode::ElementNotFound) return failAs<bool>(r);
            if (exists != wantMissing) return Result<bool>::success(true);
            const std::int64_t remaining = timeoutMs - (deps.clock->nowMs() - start);
            if (remaining <= 0) return Result<bool>::success(false);
            if (!deps.clock->sleepFor(static_cast<int>(std::min<std::int64_t>(kPollIntervalMs, remaining)),
                                      token)) {
                return Result<bool>::failure(ErrCode::Cancelled);
            }
        }
    }

    // ------------------------------------------------------------ 块语句
    // 先在 report 里占位（保持先序），子步骤执行完再回填块自己的结果。
    bool execBlock(const Step& s) {
        const size_t slot = report.steps.size();
        report.steps.push_back(beginRecord(s));
        const std::uint64_t handle = requiresWindow(s.type) ? handles[s.window] : 0;

        auto setResult = [&](ErrCode code, const std::string& detail) {
            StepRecord& r = report.steps[slot];
            r.code = code;
            r.detail = logger.scrub(detail);
            r.durationMs = elapsed() - r.startMs;
        };
        auto failBlock = [&](ErrCode code, const std::string& detail) {
            setResult(code, detail);
            log(LogLevel::Error, s.id, std::string("failed: ") + errName(code) + (detail.empty() ? "" : " - " + detail));
            return finishStep(s, slot);
        };
        auto runBody = [&](const std::vector<Step>& body, const std::string& tag) {
            scope.push_back(tag);
            const bool stop = execList(body, true);
            scope.pop_back();
            return stop;
        };
        auto pause = [&](int ms) { return ms <= 0 || deps.clock->sleepFor(ms, token); };
        const int interval = s.intervalMs.value_or(kDefaultLoopIntervalMs);

        if (s.type == StepType::If) {
            const Result<bool> c = evalCondition(s, handle);
            if (!c.ok()) return failBlock(c.code, c.detail);
            log(LogLevel::Info, s.id, std::string("condition is ") + (c.value ? "true -> then" : "false -> else"));
            if (runBody(c.value ? s.body : s.elseBody, s.id + (c.value ? ":then" : ":else"))) return true;
            setResult(ErrCode::Ok, c.value ? "condition true" : "condition false");
            return finishStep(s, slot);
        }

        if (s.type == StepType::Repeat) {
            const int times = s.times.value_or(0);
            for (int i = 1; i <= times; ++i) {
                if (token.isCancelled()) return failBlock(ErrCode::Cancelled, "");
                if (runBody(s.body, s.id + "#" + std::to_string(i))) return true;
                if (i < times && !pause(interval)) return failBlock(ErrCode::Cancelled, "");
            }
            setResult(ErrCode::Ok, "ran " + std::to_string(times) + " iterations");
            return finishStep(s, slot);
        }

        // While：每轮先判断条件；跑满 maxIterations 轮后条件仍成立 = 失败，而不是悄悄退出或继续跑
        const int maxIter = s.maxIterations.value_or(0);
        for (int i = 0;; ++i) {
            const Result<bool> c = evalCondition(s, handle);
            if (!c.ok()) return failBlock(c.code, c.detail);
            if (!c.value) {
                setResult(ErrCode::Ok, "ran " + std::to_string(i) + " iterations");
                return finishStep(s, slot);
            }
            if (i >= maxIter) {
                return failBlock(ErrCode::LimitExceeded,
                                 "condition still true after " + std::to_string(maxIter) + " iterations");
            }
            if (runBody(s.body, s.id + "#" + std::to_string(i + 1))) return true;
            if (!pause(interval)) return failBlock(ErrCode::Cancelled, "");
        }
    }

    // ------------------------------------------------------------ 列表
    // 范围规则见 RunOptions。forced：外层块已被选中，整块执行，不再看范围。
    bool inRange(size_t row) const { return row >= first && row <= last; }
    bool intersects(size_t row) const { return row <= last && rows[row].subtreeEnd >= first; }

    // 返回 true 表示整个运行应立即结束（term 已设置）
    bool execList(const std::vector<Step>& list, bool forced) {
        for (const Step& step : list) {
            const size_t row = rowOf.at(&step);
            if (!forced && !inRange(row)) {
                // 自己不在范围内，但子孙可能在：只下钻，不执行自身（单独运行嵌套步骤时忽略外层条件和循环）
                if (intersects(row) && (execList(step.body, false) || execList(step.elseBody, false))) return true;
                continue;
            }
            if (execStep(step, row)) return true;
        }
        return false;
    }

    bool execStep(const Step& step, size_t row) {
        if (token.isCancelled()) {
            term = cancelled(step.id);
            return true;
        }
        if (deps.beforeStep) {
            const StepGate gate = deps.beforeStep(step, row);
            if (token.isCancelled() || gate == StepGate::Stop) {
                term = cancelled(step.id);
                return true;
            }
            if (gate == StepGate::Skip) {
                StepRecord skipped;
                skipped.stepId = step.id;
                skipped.type = toString(step.type);
                skipped.row = row;
                skipped.path = pathFor(step);
                skipped.skipped = true;
                skipped.detail = "skipped by user";
                log(LogLevel::Info, step.id, "step skipped by user");
                report.steps.push_back(skipped);
                if (deps.observer.onStepFinished) deps.observer.onStepFinished(skipped);
                return false;
            }
        }
        if (++executions > maxExecutions) {
            // 总量保护不受 onFailure 影响：它存在的意义就是让失控的嵌套循环一定会停下来
            term = fail(ErrCode::LimitExceeded, step.id,
                        "more than " + std::to_string(maxExecutions) + " step executions (execution.maxStepExecutions)");
            return true;
        }
        if (isBlockType(step.type)) return execBlock(step);

        const size_t slot = report.steps.size();
        report.steps.push_back(runStep(step));
        return finishStep(step, slot);
    }

    // 执行一个步骤（含重试与后置校验），返回记录
    StepRecord runStep(const Step& s) {
        StepRecord rec = beginRecord(s);

        auto finishRec = [&](ErrCode code, const std::string& detail) {
            rec.code = code;
            rec.detail = logger.scrub(detail);  // 观察者和报告拿到的都是脱敏后的文本
            rec.durationMs = elapsed() - rec.startMs;
            return rec;
        };

        // 敏感文本：先登记再使用，之后所有输出都会被脱敏
        std::string text;
        if (s.type == StepType::InputText || s.type == StepType::SetValue) {
            if (s.textFromPrompt || s.sensitive) sensitiveSeen = true;  // 从这一步起不再截图
            if (s.textFromPrompt) {
                if (!deps.prompt) {
                    return finishRec(ErrCode::Unsupported, "no prompt provider for textFrom=prompt");
                }
                Result<std::string> p = deps.prompt(s);
                if (!p.ok()) return finishRec(p.code, p.detail);
                text = p.value;
                logger.addSecret(text);
            } else {
                text = s.text.value_or("");
                if (s.sensitive) logger.addSecret(text);
            }
        }

        const RetryPolicy retry = s.retry.value_or(wf.execution.retry);
        const int maxAttempts = s.type == StepType::LaunchProgram ? 1 : 1 + std::max(0, retry.times);

        std::uint64_t handle = 0;
        if (requiresWindow(s.type)) handle = handles[s.window];

        Status st = okStatus();
        for (int attempt = 1; attempt <= maxAttempts; ++attempt) {
            rec.attempts = attempt;
            if (token.isCancelled()) {
                st = Status::failure(ErrCode::Cancelled);
                break;
            }
            if (requiresWindow(s.type) && !deps.windows->isWindowValid(handle)) {
                st = Status::failure(ErrCode::WindowNotFound,
                                     "window \"" + s.window + "\" is no longer valid");
                break;
            }
            st = execOnce(s, handle, text);
            if (st.ok() && s.expect) st = checkExpect(s, handle);
            if (st.ok()) break;
            if (!isRetryable(st.code) || attempt == maxAttempts) break;

            log(LogLevel::Warn, s.id,
                std::string("attempt ") + std::to_string(attempt) + " failed: " + errName(st.code) +
                    ", retrying");
            if (!deps.clock->sleepFor(retry.intervalMs, token)) {
                st = Status::failure(ErrCode::Cancelled);
                break;
            }
        }

        if (st.ok()) {
            log(LogLevel::Info, s.id, "ok");
        } else {
            log(LogLevel::Error, s.id,
                std::string("failed: ") + errName(st.code) +
                    (st.detail.empty() ? "" : " - " + st.detail));
        }
        return finishRec(st.code, st.detail);
    }
};

// ============================================================================ run

namespace {

// 预检要用到哪些窗口：和 Run::execList 的范围规则保持一致
void collectWindows(const std::vector<Step>& list, bool forced, const std::vector<StepRow>& rows,
                    const std::map<const Step*, size_t>& rowOf, size_t first, size_t last,
                    std::set<std::string>& used) {
    for (const Step& s : list) {
        const size_t row = rowOf.at(&s);
        const bool in = forced || (row >= first && row <= last);
        if (in) {
            if (!s.window.empty()) used.insert(s.window);
            collectWindows(s.body, true, rows, rowOf, first, last, used);
            collectWindows(s.elseBody, true, rows, rowOf, first, last, used);
        } else if (row <= last && rows[row].subtreeEnd >= first) {
            collectWindows(s.body, false, rows, rowOf, first, last, used);
            collectWindows(s.elseBody, false, rows, rowOf, first, last, used);
        }
    }
}

}  // namespace

RunReport WorkflowEngine::run(const Workflow& wf, const CancelToken& token,
                              const RunOptions& options) {
    Run run(deps_, wf, token);
    run.t0 = deps_.clock->nowMs();
    run.logger.setSink(deps_.observer.onLog);
    run.options = &options;

    run.setState(ExecutionState::Validating);

    const ValidationReport vr = validateWorkflow(wf);
    if (!vr.ok()) {
        const Issue* first = nullptr;
        for (const auto& i : vr.issues) {
            if (i.severity == Severity::Error) {
                first = &i;
                break;
            }
        }
        return run.fail(ErrCode::InvalidWorkflow, "",
                        first ? first->path + ": " + first->message : "invalid workflow");
    }
    if (vr.needsConfirmation() && !options.confirmedRisks) {
        return run.fail(ErrCode::ConfirmationRequired, "",
                        "workflow contains high-risk operations that were not confirmed");
    }
    if (token.isCancelled()) return run.cancelled("");

    try {
        run.workflowData = json::parse(wf.initialDataJson);
    } catch (const std::exception&) {
        return run.fail(ErrCode::InvalidWorkflow, "", "workflow data is not valid JSON");
    }

    run.setState(ExecutionState::Running);
    run.log(LogLevel::Info, "", "run started: " + wf.name);

    run.rows = flattenSteps(wf);
    for (size_t i = 0; i < run.rows.size(); ++i) run.rowOf[run.rows[i].step] = i;
    run.maxExecutions = wf.execution.maxStepExecutions;

    const size_t total = run.rows.size();
    run.first = options.firstStep.value_or(0);
    run.last = options.lastStep ? std::min(*options.lastStep, total ? total - 1 : 0) : (total ? total - 1 : 0);
    if (total == 0 || run.first > run.last) {
        return run.fail(ErrCode::InvalidWorkflow, "", "step range is empty or out of bounds");
    }

    // 预检：会执行的步骤用到的目标窗口必须存在且未提权，之后才执行第一步
    std::set<std::string> used;
    collectWindows(wf.steps, false, run.rows, run.rowOf, run.first, run.last, used);
    const bool scriptsSelected = std::any_of(run.rows.begin() + static_cast<std::ptrdiff_t>(run.first),
                                             run.rows.begin() + static_cast<std::ptrdiff_t>(run.last + 1),
                                             [](const StepRow& row) { return row.step->type == StepType::Script; });
    if (scriptsSelected) {
        for (const auto& [alias, spec] : wf.windows) {
            (void)spec;
            used.insert(alias);
        }
    }
    std::map<std::string, size_t> launchRows;
    for (size_t i = 0; i < run.rows.size(); ++i) {
        const Step& step = *run.rows[i].step;
        if (step.type == StepType::LaunchProgram && !step.window.empty()) {
            launchRows.emplace(step.window, i);
        }
    }
    for (const auto& [alias, spec] : wf.windows) {
        if (!used.count(alias)) continue;
        if (spec.processName.empty() && spec.titleContains.empty() && spec.processId.empty() &&
            spec.width.empty() && spec.height.empty()) {
            const auto launch = launchRows.find(alias);
            if (launch == launchRows.end() || launch->second < run.first || launch->second > run.last) {
                return run.fail(ErrCode::InvalidWorkflow, "",
                                "window \"" + alias + "\" is blank; include its launch_program step in the selected range");
            }
            continue;
        }
        Result<std::uint64_t> r = run.resolveWindow(spec);
        if (r.code == ErrCode::Cancelled) return run.cancelled("");
        if (!r.ok()) {
            return run.fail(r.code, "", "window \"" + alias + "\": " + r.detail);
        }
        run.handles[alias] = r.value;
        run.log(LogLevel::Info, "", "window \"" + alias + "\" resolved");
    }

    if (run.execList(wf.steps, false)) return std::move(*run.term);

    if (run.firstCode != ErrCode::Ok) {
        return run.fail(run.firstCode, run.firstStepId, run.firstDetail);
    }
    run.log(LogLevel::Info, "", "run succeeded");
    run.setState(ExecutionState::Succeeded);
    return run.finish();
}

}  // namespace wa

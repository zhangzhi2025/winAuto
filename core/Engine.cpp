#include "core/Engine.h"

#include <algorithm>
#include <map>

#include "core/Validator.h"

namespace wa {

namespace {
constexpr int kPollIntervalMs = 100;

bool asLeaf(const Locator& loc, LeafLocator* out) {
    if (const auto* u = std::get_if<UiaLocator>(&loc)) { *out = *u; return true; }
    if (const auto* w = std::get_if<WindowPointLocator>(&loc)) { *out = *w; return true; }
    if (const auto* s = std::get_if<ScreenPointLocator>(&loc)) { *out = *s; return true; }
    if (const auto* i = std::get_if<ImageLocator>(&loc)) { *out = *i; return true; }
    return false;
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
    Result<ElementRef> locatePolling(std::uint64_t handle, const Locator& loc, int timeoutMs,
                                     ErrCode timeoutCode) {
        const std::int64_t start = deps.clock->nowMs();
        for (;;) {
            if (token.isCancelled()) return Result<ElementRef>::failure(ErrCode::Cancelled);
            Result<ElementRef> r = locateOnce(handle, loc);
            if (r.ok() || r.code != ErrCode::ElementNotFound) return r;
            const std::int64_t remaining = timeoutMs - (deps.clock->nowMs() - start);
            if (remaining <= 0) {
                return Result<ElementRef>::failure(
                    timeoutCode, "element not found within " + std::to_string(timeoutMs) + " ms");
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
                                                     ErrCode::ElementNotFound);
                if (!r.ok()) return failAs<Unit>(r);
                if (s.clickMode == ClickMode::Auto && r.value.supportsInvoke) {
                    return deps.input->invoke(r.value);
                }
                return deps.input->clickPoint(r.value.bounds.center(), handle);
            }

            case StepType::SetValue: {
                Result<ElementRef> r = locatePolling(handle, *s.locator, timeoutFor(s),
                                                     ErrCode::ElementNotFound);
                if (!r.ok()) return failAs<Unit>(r);
                if (!r.value.supportsValue) {
                    return Status::failure(ErrCode::Unsupported,
                                           "target control does not support ValuePattern");
                }
                return deps.input->setValue(r.value, text);
            }

            case StepType::InputText:
                if (!deps.windows->isForeground(handle)) return notForeground(s);
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

            case StepType::Unknown:
                break;
        }
        return Status::failure(ErrCode::Internal, "unknown step type");
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

    // 执行一个步骤（含重试与后置校验），返回记录
    StepRecord runStep(const Step& s) {
        StepRecord rec;
        rec.stepId = s.id;
        rec.type = toString(s.type);
        rec.startMs = elapsed();
        if (deps.observer.onStepStarted) deps.observer.onStepStarted(s.id);
        log(LogLevel::Info, s.id, std::string("start ") + toString(s.type));

        auto finishRec = [&](ErrCode code, const std::string& detail) {
            rec.code = code;
            rec.detail = logger.scrub(detail);  // 观察者和报告拿到的都是脱敏后的文本
            rec.durationMs = elapsed() - rec.startMs;
            return rec;
        };

        // 敏感文本：先登记再使用，之后所有输出都会被脱敏
        std::string text;
        if (s.type == StepType::InputText || s.type == StepType::SetValue) {
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
        const int maxAttempts = 1 + std::max(0, retry.times);

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

RunReport WorkflowEngine::run(const Workflow& wf, const CancelToken& token,
                              const RunOptions& options) {
    Run run(deps_, wf, token);
    run.t0 = deps_.clock->nowMs();
    run.logger.setSink(deps_.observer.onLog);

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

    run.setState(ExecutionState::Running);
    run.log(LogLevel::Info, "", "run started: " + wf.name);

    // 预检：所有目标窗口必须存在且未提权，之后才执行第一步
    for (const auto& [alias, spec] : wf.windows) {
        Result<std::uint64_t> r = run.resolveWindow(spec);
        if (r.code == ErrCode::Cancelled) return run.cancelled("");
        if (!r.ok()) {
            return run.fail(r.code, "", "window \"" + alias + "\": " + r.detail);
        }
        run.handles[alias] = r.value;
        run.log(LogLevel::Info, "", "window \"" + alias + "\" resolved");
    }

    ErrCode firstCode = ErrCode::Ok;
    std::string firstStep;
    std::string firstDetail;

    for (const Step& step : wf.steps) {
        if (token.isCancelled()) return run.cancelled(step.id);

        StepRecord rec = run.runStep(step);
        run.report.steps.push_back(rec);
        if (deps_.observer.onStepFinished) deps_.observer.onStepFinished(rec);

        if (rec.code == ErrCode::Ok) continue;
        if (rec.code == ErrCode::Cancelled) return run.cancelled(step.id);

        if (firstCode == ErrCode::Ok) {
            firstCode = rec.code;
            firstStep = step.id;
            firstDetail = rec.detail;
        }
        const FailurePolicy policy = step.onFailure.value_or(wf.execution.onFailure);
        if (policy == FailurePolicy::Stop) {
            return run.fail(rec.code, step.id, rec.detail);
        }
        run.log(LogLevel::Warn, step.id, "failure ignored (onFailure=continue)");
    }

    if (firstCode != ErrCode::Ok) {
        return run.fail(firstCode, firstStep, firstDetail);
    }
    run.log(LogLevel::Info, "", "run succeeded");
    run.setState(ExecutionState::Succeeded);
    return run.finish();
}

}  // namespace wa

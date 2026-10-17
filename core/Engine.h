#pragma once
// 工作流引擎：顺序执行、重试、超时、取消、失败策略、后置校验。
// 同步执行：调用方应在专用执行线程里调用 run()；另一个线程通过 CancelToken 停止它。
#include <functional>
#include <optional>
#include <string>
#include <vector>

#include "core/ExecutionState.h"
#include "core/Interfaces.h"
#include "core/Logger.h"
#include "core/Model.h"

namespace wa {

struct StepRecord {
    std::string stepId;
    std::string type;
    std::int64_t startMs = 0;     // 相对本次运行开始
    std::int64_t durationMs = 0;
    int attempts = 0;
    ErrCode code = ErrCode::Ok;
    std::string detail;
    bool skipped = false;  // 调试时被用户跳过，未执行
};

struct RunReport {
    ExecutionState state = ExecutionState::Idle;
    ErrCode code = ErrCode::Ok;  // 整个流程的结果；continue 策略下为第一个失败步骤的错误码
    std::string failedStepId;
    std::string detail;
    std::vector<StepRecord> steps;
    std::vector<LogEntry> logs;
};

struct RunOptions {
    // 校验报告含高风险项（如 Alt+F4）时，用户已在界面上确认
    bool confirmedRisks = false;
    // 只执行 [firstStep, lastStep]（含，0 起）。未设置表示到头/到尾；两者相同即"只执行这一步"。
    // 校验仍针对整个流程；窗口预检只针对范围内用到的窗口。
    std::optional<size_t> firstStep;
    std::optional<size_t> lastStep;
};

// 每步执行前的调试闸门。调用在引擎所在线程，可以阻塞（暂停/单步）；阻塞时应监听取消令牌。
enum class StepGate { Run, Skip, Stop };
using StepGateFn = std::function<StepGate(const Step& step, size_t index)>;

struct EngineObserver {
    std::function<void(ExecutionState)> onStateChanged;
    std::function<void(const std::string& stepId)> onStepStarted;
    std::function<void(const StepRecord&)> onStepFinished;
    std::function<void(const LogEntry&)> onLog;
};

// 运行时询问敏感文本（textFrom=prompt）。用户取消时返回 Cancelled。
using PromptFn = std::function<Result<std::string>(const Step& step)>;

struct EngineDeps {
    IWindowProvider* windows = nullptr;
    IElementLocator* locator = nullptr;
    IInputController* input = nullptr;
    IClock* clock = nullptr;
    StepGateFn beforeStep;  // 可为空：不拦截
    PromptFn prompt;  // 可为空；为空时 textFrom=prompt 的步骤失败（Unsupported）
    EngineObserver observer;
};

class WorkflowEngine {
public:
    explicit WorkflowEngine(EngineDeps deps) : deps_(std::move(deps)) {}

    RunReport run(const Workflow& wf, const CancelToken& token, const RunOptions& options = {});

    static bool isRetryable(ErrCode code);

private:
    struct Run;  // 单次运行的上下文，定义在 .cpp
    EngineDeps deps_;
};

}  // namespace wa

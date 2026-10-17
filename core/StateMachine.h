// 执行状态机：显式的转换表，非法转换被拒绝。
//
//   Idle -> Validating -> Running -> Succeeded | Failed
//   Running <-> Paused           （引擎暂停/恢复留到后续阶段，转换表已预留）
//   Validating | Running | Paused -> Cancelling -> Cancelled
//   Validating -> Failed（校验未通过）
//   Succeeded | Failed | Cancelled -> Idle（复位后可再次运行）
#pragma once

#include <functional>
#include <mutex>
#include <string>

namespace winauto {

enum class ExecutionState { Idle, Validating, Running, Paused, Cancelling, Succeeded, Failed, Cancelled };

const char* toString(ExecutionState s);
bool isTerminal(ExecutionState s);
bool canTransition(ExecutionState from, ExecutionState to);

class ExecutionStateMachine {
public:
    using Listener = std::function<void(ExecutionState from, ExecutionState to)>;

    ExecutionState state() const;
    // 合法则切换并通知监听者（在锁外调用），返回 true；非法则保持原状态并返回 false。
    bool transitionTo(ExecutionState next, std::string* error = nullptr);
    void setListener(Listener l);

private:
    mutable std::mutex mutex_;
    ExecutionState state_ = ExecutionState::Idle;
    Listener listener_;
};

}  // namespace winauto

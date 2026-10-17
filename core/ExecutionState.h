#pragma once
#include <cstdint>

namespace wa {

enum class ExecutionState : std::uint8_t {
    Idle,
    Validating,
    Running,
    Paused,
    Cancelling,
    Succeeded,
    Failed,
    Cancelled
};

const char* toString(ExecutionState s);
bool isTerminal(ExecutionState s);  // Succeeded / Failed / Cancelled

// 合法转换表：
//   Idle        -> Validating
//   Validating  -> Running | Failed | Cancelling
//   Running     -> Paused | Cancelling | Succeeded | Failed
//   Paused      -> Running | Cancelling
//   Cancelling  -> Cancelled
//   终态        -> Idle（复位）
class ExecutionStateMachine {
public:
    ExecutionState state() const { return state_; }
    static bool isAllowed(ExecutionState from, ExecutionState to);
    bool canTransition(ExecutionState to) const { return isAllowed(state_, to); }
    // 合法则转换并返回 true；非法则保持原状态并返回 false
    bool transition(ExecutionState to);

private:
    ExecutionState state_ = ExecutionState::Idle;
};

}  // namespace wa

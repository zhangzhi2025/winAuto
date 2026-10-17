#include "core/ExecutionState.h"

namespace wa {

const char* toString(ExecutionState s) {
    switch (s) {
        case ExecutionState::Idle: return "Idle";
        case ExecutionState::Validating: return "Validating";
        case ExecutionState::Running: return "Running";
        case ExecutionState::Paused: return "Paused";
        case ExecutionState::Cancelling: return "Cancelling";
        case ExecutionState::Succeeded: return "Succeeded";
        case ExecutionState::Failed: return "Failed";
        case ExecutionState::Cancelled: return "Cancelled";
    }
    return "Unknown";
}

bool isTerminal(ExecutionState s) {
    return s == ExecutionState::Succeeded || s == ExecutionState::Failed ||
           s == ExecutionState::Cancelled;
}

bool ExecutionStateMachine::isAllowed(ExecutionState from, ExecutionState to) {
    using S = ExecutionState;
    switch (from) {
        case S::Idle: return to == S::Validating;
        case S::Validating: return to == S::Running || to == S::Failed || to == S::Cancelling;
        case S::Running:
            return to == S::Paused || to == S::Cancelling || to == S::Succeeded || to == S::Failed;
        case S::Paused: return to == S::Running || to == S::Cancelling;
        case S::Cancelling: return to == S::Cancelled;
        case S::Succeeded:
        case S::Failed:
        case S::Cancelled: return to == S::Idle;
    }
    return false;
}

bool ExecutionStateMachine::transition(ExecutionState to) {
    if (!canTransition(to)) return false;
    state_ = to;
    return true;
}

}  // namespace wa

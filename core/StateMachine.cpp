#include "core/StateMachine.h"

namespace winauto {

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
    return s == ExecutionState::Succeeded || s == ExecutionState::Failed || s == ExecutionState::Cancelled;
}

bool canTransition(ExecutionState from, ExecutionState to) {
    using S = ExecutionState;
    switch (from) {
    case S::Idle: return to == S::Validating;
    case S::Validating: return to == S::Running || to == S::Failed || to == S::Cancelling;
    case S::Running: return to == S::Paused || to == S::Cancelling || to == S::Succeeded || to == S::Failed;
    case S::Paused: return to == S::Running || to == S::Cancelling;
    case S::Cancelling: return to == S::Cancelled;
    case S::Succeeded:
    case S::Failed:
    case S::Cancelled: return to == S::Idle;
    }
    return false;
}

ExecutionState ExecutionStateMachine::state() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return state_;
}

bool ExecutionStateMachine::transitionTo(ExecutionState next, std::string* error) {
    ExecutionState from;
    Listener listener;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        from = state_;
        if (!canTransition(from, next)) {
            if (error) *error = std::string("illegal transition ") + toString(from) + " -> " + toString(next);
            return false;
        }
        state_ = next;
        listener = listener_;
    }
    if (listener) listener(from, next);
    return true;
}

void ExecutionStateMachine::setListener(Listener l) {
    std::lock_guard<std::mutex> lock(mutex_);
    listener_ = std::move(l);
}

}  // namespace winauto

#include "core/Error.h"

namespace winauto {

const char* toString(ErrCode code) {
    switch (code) {
    case ErrCode::Ok: return "Ok";
    case ErrCode::WindowNotFound: return "WindowNotFound";
    case ErrCode::WindowAmbiguous: return "WindowAmbiguous";
    case ErrCode::WindowElevated: return "WindowElevated";
    case ErrCode::ActivationFailed: return "ActivationFailed";
    case ErrCode::ElementNotFound: return "ElementNotFound";
    case ErrCode::ElementAmbiguous: return "ElementAmbiguous";
    case ErrCode::Timeout: return "Timeout";
    case ErrCode::Cancelled: return "Cancelled";
    case ErrCode::InputBlocked: return "InputBlocked";
    case ErrCode::ExpectationFailed: return "ExpectationFailed";
    case ErrCode::Unsupported: return "Unsupported";
    case ErrCode::PromptUnavailable: return "PromptUnavailable";
    case ErrCode::InvalidWorkflow: return "InvalidWorkflow";
    case ErrCode::Internal: return "Internal";
    }
    return "Unknown";
}

const char* describe(ErrCode code) {
    switch (code) {
    case ErrCode::Ok: return "Success";
    case ErrCode::WindowNotFound: return "Target window not found, closed or replaced";
    case ErrCode::WindowAmbiguous: return "More than one window matches the target";
    case ErrCode::WindowElevated: return "Target window runs with higher privileges than this tool";
    case ErrCode::ActivationFailed: return "Could not bring the window to the foreground";
    case ErrCode::ElementNotFound: return "Target element was not found";
    case ErrCode::ElementAmbiguous: return "More than one element matches; refusing to guess";
    case ErrCode::Timeout: return "Operation timed out";
    case ErrCode::Cancelled: return "Cancelled by user";
    case ErrCode::InputBlocked: return "Input was blocked by the system or the target";
    case ErrCode::ExpectationFailed: return "Step expectation was not met";
    case ErrCode::Unsupported: return "Not supported by this build or adapter";
    case ErrCode::PromptUnavailable: return "Runtime input is required but no prompt is available";
    case ErrCode::InvalidWorkflow: return "Workflow is invalid or needs confirmation";
    case ErrCode::Internal: return "Internal error";
    }
    return "Unknown error";
}

bool isRetryable(ErrCode code) {
    switch (code) {
    case ErrCode::ElementNotFound:
    case ErrCode::Timeout:
    case ErrCode::ExpectationFailed:
    case ErrCode::ActivationFailed:
        return true;
    default:
        return false;
    }
}

}  // namespace winauto

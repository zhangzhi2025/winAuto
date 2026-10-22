#include "core/ErrCode.h"

namespace wa {

const char* errName(ErrCode c) {
    switch (c) {
        case ErrCode::Ok: return "Ok";
        case ErrCode::InvalidWorkflow: return "InvalidWorkflow";
        case ErrCode::ConfirmationRequired: return "ConfirmationRequired";
        case ErrCode::WindowNotFound: return "WindowNotFound";
        case ErrCode::WindowElevated: return "WindowElevated";
        case ErrCode::ElementNotFound: return "ElementNotFound";
        case ErrCode::ElementAmbiguous: return "ElementAmbiguous";
        case ErrCode::ExpectationFailed: return "ExpectationFailed";
        case ErrCode::Timeout: return "Timeout";
        case ErrCode::Cancelled: return "Cancelled";
        case ErrCode::InputBlocked: return "InputBlocked";
        case ErrCode::Unsupported: return "Unsupported";
        case ErrCode::LimitExceeded: return "LimitExceeded";
        case ErrCode::Internal: return "Internal";
    }
    return "Unknown";
}

const char* errText(ErrCode c) {
    switch (c) {
        case ErrCode::Ok: return "成功";
        case ErrCode::InvalidWorkflow: return "流程文件无效";
        case ErrCode::ConfirmationRequired: return "流程包含高风险操作，需要确认后才能运行";
        case ErrCode::WindowNotFound: return "未找到目标窗口，或窗口已关闭";
        case ErrCode::WindowElevated: return "目标窗口以更高权限运行，本程序不会自动提权";
        case ErrCode::ElementNotFound: return "未找到目标控件";
        case ErrCode::ElementAmbiguous: return "找到多个匹配控件，无法确定目标";
        case ErrCode::ExpectationFailed: return "步骤执行后的校验条件未满足";
        case ErrCode::Timeout: return "等待超时";
        case ErrCode::Cancelled: return "已取消";
        case ErrCode::InputBlocked: return "输入被系统或目标程序阻止";
        case ErrCode::Unsupported: return "目标或平台不支持该操作";
        case ErrCode::LimitExceeded: return "循环次数或步骤执行次数超过上限";
        case ErrCode::Internal: return "内部错误";
    }
    return "未知错误";
}

}  // namespace wa

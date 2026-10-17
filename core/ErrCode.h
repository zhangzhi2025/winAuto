#pragma once
// 统一错误码与结果类型。引擎的重试、失败策略和 UI 提示都基于 ErrCode，而不是字符串。
#include <string>
#include <utility>

namespace wa {

enum class ErrCode {
    Ok = 0,
    InvalidWorkflow,       // 流程文件非法（语法或语义）
    ConfirmationRequired,  // 含高风险操作，需用户确认后才能运行
    WindowNotFound,        // 目标窗口不存在或句柄失效
    WindowElevated,        // 目标窗口权限高于本程序
    ElementNotFound,       // 未找到控件
    ElementAmbiguous,      // 找到多个候选，拒绝猜测
    ExpectationFailed,     // 步骤后置校验未满足
    Timeout,               // 等待超时
    Cancelled,             // 被用户取消
    InputBlocked,          // 输入被系统或目标阻止
    Unsupported,           // 目标或平台不支持该操作
    Internal               // 内部错误
};

const char* errName(ErrCode c);  // 稳定的英文标识，如 "ElementNotFound"，用于日志与测试
const char* errText(ErrCode c);  // 面向用户的中文说明（UTF-8）

struct Unit {};

template <class T>
struct Result {
    T value{};
    ErrCode code = ErrCode::Ok;
    std::string detail;

    bool ok() const { return code == ErrCode::Ok; }
    explicit operator bool() const { return ok(); }

    static Result success(T v = T{}) {
        Result r;
        r.value = std::move(v);
        return r;
    }
    static Result failure(ErrCode c, std::string d = {}) {
        Result r;
        r.code = c;
        r.detail = std::move(d);
        return r;
    }
};

using Status = Result<Unit>;

inline Status okStatus() { return Status::success(); }

// 把一个失败结果转换成另一种结果类型（保留错误码与说明）
template <class T, class U>
Result<T> failAs(const Result<U>& r) {
    return Result<T>::failure(r.code, r.detail);
}

}  // namespace wa

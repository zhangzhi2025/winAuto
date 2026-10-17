// 错误模型：ErrCode / Status / Result<T>
// 引擎的重试、失败策略、UI 提示全部基于 ErrCode，而不是字符串。
#pragma once

#include <string>
#include <utility>

namespace winauto {

enum class ErrCode {
    Ok = 0,
    WindowNotFound,     // 目标窗口不存在 / 已关闭 / 被替换
    WindowAmbiguous,    // 多个窗口同时匹配
    WindowElevated,     // 目标进程权限高于本工具（UIPI）
    ActivationFailed,   // 窗口激活失败（前台窗口规则等）
    ElementNotFound,    // 控件 / 图像 / 坐标目标未找到
    ElementAmbiguous,   // 找到多个候选，不允许任意选择
    Timeout,            // 等待超时
    Cancelled,          // 用户取消
    InputBlocked,       // 输入被系统或目标拒绝
    ExpectationFailed,  // 步骤后置校验 expect 不满足
    Unsupported,        // 当前构建 / 适配器不支持
    PromptUnavailable,  // 需要运行时输入但没有可用的提示通道
    InvalidWorkflow,    // 流程未通过校验或需要确认
    Internal            // 内部错误
};

// 稳定的机器可读名称，例如 "ElementNotFound"（用于日志、CLI、测试）
const char* toString(ErrCode code);
// 面向人的简短英文描述（GUI 层之后可按错误码映射为中文）
const char* describe(ErrCode code);
// 引擎是否允许对该错误码重试
bool isRetryable(ErrCode code);

struct Status {
    ErrCode code = ErrCode::Ok;
    std::string detail;

    bool ok() const { return code == ErrCode::Ok; }
    static Status success() { return Status{}; }
    static Status fail(ErrCode c, std::string d = {}) { return Status{c, std::move(d)}; }
};

template <class T>
struct Result {
    Status status;
    T value{};

    bool ok() const { return status.ok(); }
    ErrCode code() const { return status.code; }
    static Result success(T v) {
        Result r;
        r.value = std::move(v);
        return r;
    }
    static Result fail(ErrCode c, std::string d = {}) {
        Result r;
        r.status = Status::fail(c, std::move(d));
        return r;
    }
};

}  // namespace winauto

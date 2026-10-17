#pragma once
// 紧急停止：全局热键 -> 取消令牌。
// 工具会接管键鼠，流程一旦跑偏，用户必须有一个无需鼠标、在任何前台窗口下都有效的夺回手段。
#include <functional>

#include "core/CancelToken.h"
#include "core/ErrCode.h"
#include "core/VirtualKey.h"

namespace wa {

class IStopHotkey {
public:
    virtual ~IStopHotkey() = default;
    // 注册全局热键。onPressed 会在平台内部的线程上被调用，必须线程安全。
    // 热键已被其他程序占用时返回失败（InputBlocked）。
    virtual Status registerHotkey(const HotkeySpec& spec, std::function<void()> onPressed) = 0;
    // 注销热键。返回后保证不会再有回调被调用或正在执行。
    virtual void unregisterHotkey() = 0;
};

// 把一个全局热键绑定到取消令牌，作用域结束自动注销。
// token 必须比 guard 活得更久。
class StopHotkeyGuard {
public:
    StopHotkeyGuard(IStopHotkey& hotkey, CancelToken& token) : hotkey_(hotkey), token_(token) {}
    ~StopHotkeyGuard() { disarm(); }
    StopHotkeyGuard(const StopHotkeyGuard&) = delete;
    StopHotkeyGuard& operator=(const StopHotkeyGuard&) = delete;

    // 注册失败时返回错误，调用方应拒绝启动流程（没有紧急停止就不运行），除非用户明确选择承担风险。
    Status arm(const HotkeySpec& spec);
    void disarm();
    bool armed() const { return armed_; }
    const HotkeySpec& spec() const { return spec_; }

private:
    IStopHotkey& hotkey_;
    CancelToken& token_;
    HotkeySpec spec_;
    bool armed_ = false;
};

}  // namespace wa

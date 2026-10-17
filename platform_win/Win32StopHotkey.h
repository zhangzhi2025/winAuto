#pragma once
#include <atomic>
#include <functional>
#include <thread>

#include "core/StopHotkey.h"

namespace wa::platform {

// 全局停止热键。内部起一个专用线程，自带消息循环，调用 RegisterHotKey，
// 因此不依赖 Qt 事件循环，GUI 与命令行都能用。
// 回调在该内部线程上被调用；unregisterHotkey() 会 join 线程，返回后不会再有回调。
// 不要在回调里调用 unregisterHotkey（会自己 join 自己）。
class Win32StopHotkey : public IStopHotkey {
public:
    Win32StopHotkey() = default;
    ~Win32StopHotkey() override;
    Win32StopHotkey(const Win32StopHotkey&) = delete;
    Win32StopHotkey& operator=(const Win32StopHotkey&) = delete;

    Status registerHotkey(const HotkeySpec& spec, std::function<void()> onPressed) override;
    void unregisterHotkey() override;

private:
    std::thread thread_;
    std::atomic<unsigned long> threadId_{0};
    std::function<void()> callback_;
};

}  // namespace wa::platform

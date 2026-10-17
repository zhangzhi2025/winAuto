#include "Win32StopHotkey.h"

#include <windows.h>

#include <future>
#include <memory>

#include "Win32Util.h"

namespace wa::platform {

Win32StopHotkey::~Win32StopHotkey() { unregisterHotkey(); }

Status Win32StopHotkey::registerHotkey(const HotkeySpec& spec, std::function<void()> onPressed) {
    unregisterHotkey();  // 重复注册时先清理旧线程
    callback_ = std::move(onPressed);

    // promise 用 shared_ptr 交给线程，避免本函数返回后线程仍在访问已销毁的栈对象
    auto ready = std::make_shared<std::promise<Status>>();
    std::future<Status> result = ready->get_future();

    thread_ = std::thread([this, spec, ready] {
        // 先强制创建本线程的消息队列，之后别的线程 PostThreadMessage(WM_QUIT) 才一定投递得到
        MSG msg;
        PeekMessageW(&msg, nullptr, WM_USER, WM_USER, PM_NOREMOVE);
        threadId_.store(GetCurrentThreadId());

        constexpr int kHotkeyId = 1;
        // MOD_NOREPEAT：按住不放不会连续触发
        if (!RegisterHotKey(nullptr, kHotkeyId, spec.modifiers | MOD_NOREPEAT, spec.vk)) {
            const DWORD err = GetLastError();
            ready->set_value(Status::failure(
                err == ERROR_HOTKEY_ALREADY_REGISTERED ? ErrCode::InputBlocked : ErrCode::Internal,
                err == ERROR_HOTKEY_ALREADY_REGISTERED
                    ? "hotkey " + spec.text + " is already used by another program"
                    : "RegisterHotKey failed: " + lastErrorText(err)));
            return;
        }
        ready->set_value(okStatus());

        while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
            if (msg.message == WM_HOTKEY && msg.wParam == kHotkeyId) {
                if (callback_) callback_();
            }
        }
        UnregisterHotKey(nullptr, kHotkeyId);
    });

    Status st = result.get();
    if (!st.ok()) {
        thread_.join();
        callback_ = nullptr;
        threadId_.store(0);
    }
    return st;
}

void Win32StopHotkey::unregisterHotkey() {
    if (!thread_.joinable()) return;
    const DWORD tid = static_cast<DWORD>(threadId_.load());
    if (tid != 0) PostThreadMessageW(tid, WM_QUIT, 0, 0);
    thread_.join();
    callback_ = nullptr;
    threadId_.store(0);
}

}  // namespace wa::platform

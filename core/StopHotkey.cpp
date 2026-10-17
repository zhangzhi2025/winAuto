#include "core/StopHotkey.h"

namespace wa {

Status StopHotkeyGuard::arm(const HotkeySpec& spec) {
    if (armed_) disarm();  // 重复 arm：先注销旧的，避免泄漏注册
    CancelToken* token = &token_;
    Status st = hotkey_.registerHotkey(spec, [token] { token->cancel(); });
    if (!st.ok()) return st;
    spec_ = spec;
    armed_ = true;
    return okStatus();
}

void StopHotkeyGuard::disarm() {
    if (!armed_) return;
    hotkey_.unregisterHotkey();
    armed_ = false;
}

}  // namespace wa

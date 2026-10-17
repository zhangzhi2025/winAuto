#pragma once
// 按键名 <-> Windows 虚拟键码，以及全局热键字符串解析。
// 数值直接写成 Win32 的 VK_* / MOD_* 常量，所以这里不需要 windows.h，也能在任何平台测试。
#include <optional>
#include <string>

#include "core/ErrCode.h"

namespace wa {

constexpr unsigned kModAlt = 0x0001;      // = MOD_ALT
constexpr unsigned kModControl = 0x0002;  // = MOD_CONTROL
constexpr unsigned kModShift = 0x0004;    // = MOD_SHIFT
constexpr unsigned kModWin = 0x0008;      // = MOD_WIN

// 输入：Validator 接受的按键名（大小写不敏感）。不支持返回 nullopt。
std::optional<unsigned> virtualKeyFor(const std::string& key);

// SendInput 需要给这些键加 KEYEVENTF_EXTENDEDKEY，否则会被当成小键盘键（阶段 2 使用）
bool isExtendedKey(unsigned vk);

struct HotkeySpec {
    unsigned modifiers = 0;  // kMod* 位或
    unsigned vk = 0;
    std::string text;  // 规范化文本，如 "CTRL+ALT+F10"，用于界面显示和日志
};

// 解析 "Ctrl+Alt+F10"。要求至少一个修饰键、且恰好一个非修饰键：
// 没有修饰键的全局热键会劫持普通按键，不允许。
Result<HotkeySpec> parseHotkey(const std::string& text);

// 默认停止热键。Pause 键在很多笔记本上不存在，F12 被调试器保留，所以选 F10。
inline const char* kDefaultStopHotkey = "CTRL+ALT+F10";

}  // namespace wa

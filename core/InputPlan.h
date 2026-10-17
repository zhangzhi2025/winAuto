#pragma once
// 把"要输入什么"翻译成平台无关的事件序列，以及坐标换算。
// 平台层（SendInput）只负责把这些事件原样发出去，因此这里的每条规则都能在任何环境测试。
#include <string>
#include <vector>

#include "core/ErrCode.h"
#include "core/Model.h"

namespace wa {

// ---------------------------------------------------------------- 组合键

struct KeyEvent {
    unsigned vk = 0;
    bool down = true;
    bool extended = false;  // 需要 KEYEVENTF_EXTENDEDKEY
};

// ["CTRL","SHIFT","S"] -> 修饰键依次按下，再按主键；然后按相反顺序全部释放。
// 重复的键只按一次。不支持的键名返回 InvalidWorkflow。
Result<std::vector<KeyEvent>> buildKeySequence(const std::vector<std::string>& keys);

// ---------------------------------------------------------------- 文本

// UTF-8 -> UTF-16。非法序列（截断、过长编码、代理区、超出 U+10FFFF）一律拒绝，不做"替换字符"式的悄悄修复。
Result<std::u16string> utf8ToUtf16(const std::string& utf8);

struct TextEvent {
    enum class Kind { Unicode, Key };
    Kind kind = Kind::Unicode;
    unsigned value = 0;  // Unicode: UTF-16 代码单元；Key: 虚拟键码
};

// 每个事件代表"敲一次"（平台负责 down+up）。
// 换行（\n、\r\n、\r）变成 Enter 键，制表符变成 Tab 键——很多程序会忽略 Unicode 方式发来的 U+000A。
// 其他控制字符（含 U+0000、U+007F）被拒绝。
Result<std::vector<TextEvent>> buildTextEvents(const std::string& utf8);

// ---------------------------------------------------------------- 坐标

// 把窗口相对定位器解析成屏幕坐标。clientScreen 是窗口 client 区域在屏幕上的矩形（物理像素）。
//   top_left:     (x, y)                 距左上角
//   bottom_right: (w-1-x, h-1-y)         距右下角向内
//   ratio:        (round(x*(w-1)), round(y*(h-1)))
// 结果必须落在 client 区域内，否则返回 ElementNotFound（多半是窗口被缩小或最小化了），不会点到窗口外面。
Result<Point> resolveWindowPoint(const Rect& clientScreen, const WindowPointLocator& locator);

struct AbsoluteMouse {
    int x = 0;  // 0..65535
    int y = 0;
};
// SendInput 的 MOUSEEVENTF_ABSOLUTE | MOUSEEVENTF_VIRTUALDESK 坐标：把虚拟桌面映射到 0..65535。
AbsoluteMouse toAbsoluteMouse(Point screen, const Rect& virtualScreen);

}  // namespace wa

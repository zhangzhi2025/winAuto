#include "Win32InputController.h"

#include <windows.h>

#include <algorithm>
#include <vector>

#include "DpiHelper.h"
#include "Win32Util.h"
#include "core/InputPlan.h"

namespace wa::platform {

namespace {

constexpr size_t kChunk = 128;  // 每次 SendInput 最多发送的事件数

Status sendAll(std::vector<INPUT>& inputs) {
    size_t offset = 0;
    while (offset < inputs.size()) {
        const UINT n = static_cast<UINT>(std::min(kChunk, inputs.size() - offset));
        const UINT sent = SendInput(n, inputs.data() + offset, sizeof(INPUT));
        if (sent != n) {
            return Status::failure(ErrCode::InputBlocked,
                                   "SendInput injected " + std::to_string(sent) + " of " +
                                       std::to_string(n) + " events (" + lastErrorText(GetLastError()) + ")");
        }
        offset += n;
    }
    return okStatus();
}

INPUT keyboardEvent(WORD vk, WORD scan, DWORD flags) {
    INPUT in{};
    in.type = INPUT_KEYBOARD;
    in.ki.wVk = vk;
    in.ki.wScan = scan;
    in.ki.dwFlags = flags;
    return in;
}

INPUT mouseEvent(LONG dx, LONG dy, DWORD flags) {
    INPUT in{};
    in.type = INPUT_MOUSE;
    in.mi.dx = dx;
    in.mi.dy = dy;
    in.mi.dwFlags = flags;
    return in;
}

HWND toHwnd(std::uint64_t handle) { return reinterpret_cast<HWND>(static_cast<UINT_PTR>(handle)); }

}  // namespace

Status Win32InputController::clickPoint(Point p, std::uint64_t windowHandle) {
    if (!isPerMonitorV2Aware()) {
        return Status::failure(ErrCode::Internal,
                               "process is not Per-Monitor V2 DPI aware (Windows 10 1703+ required); "
                               "click coordinates would be wrong");
    }

    if (windowHandle != 0) {
        POINT pt{p.x, p.y};
        HWND hit = WindowFromPoint(pt);
        HWND root = hit ? GetAncestor(hit, GA_ROOT) : nullptr;
        if (root != toHwnd(windowHandle)) {
            return Status::failure(ErrCode::InputBlocked,
                                   "the point is not on the target window (covered by another window, "
                                   "a popup, or outside it); refusing to click");
        }
    }

    const Rect vs = virtualScreenRect();
    if (vs.w <= 0 || vs.h <= 0) {
        // GetSystemMetrics 失败时会返回 0；此时换算只能得到 (0,0)，宁可拒绝也不点屏幕角落
        return Status::failure(ErrCode::Internal, "cannot determine the virtual screen size");
    }
    const AbsoluteMouse m = toAbsoluteMouse(p, vs);
    constexpr DWORD kAbs = MOUSEEVENTF_ABSOLUTE | MOUSEEVENTF_VIRTUALDESK;

    // 先移动，等指针到位再按下；一并发送时部分程序会在旧位置收到点击
    std::vector<INPUT> move = {mouseEvent(m.x, m.y, MOUSEEVENTF_MOVE | kAbs)};
    Status st = sendAll(move);
    if (!st.ok()) return st;
    Sleep(30);

    std::vector<INPUT> click = {mouseEvent(m.x, m.y, MOUSEEVENTF_LEFTDOWN | kAbs),
                                mouseEvent(m.x, m.y, MOUSEEVENTF_LEFTUP | kAbs)};
    return sendAll(click);
}

Status Win32InputController::invoke(const ElementRef& element) {
    if (!uia_) return Status::failure(ErrCode::Unsupported, "InvokePattern needs a UI Automation session");
    return uia_->invoke(element.nativeHandle);
}

Status Win32InputController::setValue(const ElementRef& element, const std::string& text) {
    if (!uia_) return Status::failure(ErrCode::Unsupported, "ValuePattern needs a UI Automation session");
    return uia_->setValue(element.nativeHandle, text);
}

Status Win32InputController::typeText(const std::string& text, InputMethod method) {
    if (method == InputMethod::Clipboard) {
        return Status::failure(ErrCode::Unsupported, "clipboard input is not implemented");
    }
    Result<std::vector<TextEvent>> plan = buildTextEvents(text);
    if (!plan.ok()) return failAs<Unit>(plan);

    std::vector<INPUT> inputs;
    inputs.reserve(plan.value.size() * 2);
    for (const TextEvent& e : plan.value) {
        if (e.kind == TextEvent::Kind::Unicode) {
            const WORD unit = static_cast<WORD>(e.value);
            inputs.push_back(keyboardEvent(0, unit, KEYEVENTF_UNICODE));
            inputs.push_back(keyboardEvent(0, unit, KEYEVENTF_UNICODE | KEYEVENTF_KEYUP));
        } else {  // Enter / Tab 等：按虚拟键发送，很多程序会忽略 Unicode 方式发来的换行
            const WORD vk = static_cast<WORD>(e.value);
            inputs.push_back(keyboardEvent(vk, 0, 0));
            inputs.push_back(keyboardEvent(vk, 0, KEYEVENTF_KEYUP));
        }
    }
    return sendAll(inputs);
}

Status Win32InputController::hotkey(const std::vector<std::string>& keys) {
    Result<std::vector<KeyEvent>> plan = buildKeySequence(keys);
    if (!plan.ok()) return failAs<Unit>(plan);

    std::vector<INPUT> inputs;
    for (const KeyEvent& k : plan.value) {
        const WORD scan = static_cast<WORD>(MapVirtualKeyW(k.vk, MAPVK_VK_TO_VSC));
        DWORD flags = 0;
        if (k.extended) flags |= KEYEVENTF_EXTENDEDKEY;
        if (!k.down) flags |= KEYEVENTF_KEYUP;
        inputs.push_back(keyboardEvent(static_cast<WORD>(k.vk), scan, flags));
    }
    Status st = sendAll(inputs);
    if (!st.ok()) {
        // 发送中途失败：尽力释放所有键，避免修饰键卡在按下状态
        std::vector<INPUT> release;
        for (const KeyEvent& k : plan.value) {
            if (k.down) continue;
            release.push_back(keyboardEvent(static_cast<WORD>(k.vk), 0,
                                            KEYEVENTF_KEYUP | (k.extended ? KEYEVENTF_EXTENDEDKEY : 0)));
        }
        if (!release.empty()) SendInput(static_cast<UINT>(release.size()), release.data(), sizeof(INPUT));
    }
    return st;
}

}  // namespace wa::platform

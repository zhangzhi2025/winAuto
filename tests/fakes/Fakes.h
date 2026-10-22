#pragma once
// 可脚本化的 Fake 适配器，供引擎测试使用。
#include <algorithm>
#include <cctype>
#include <functional>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "core/Interfaces.h"
#include "core/StopHotkey.h"
#include "core/WindowMatcher.h"

namespace wa::fakes {

inline std::string lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

// 虚拟时钟：sleepFor 立即推进时间，不真正睡眠。onSleep 钩子可用于在指定时刻触发取消等。
struct FakeClock : IClock {
    std::int64_t now = 1000;
    int sleepCalls = 0;
    std::function<void(std::int64_t)> onSleep;

    std::int64_t nowMs() const override { return now; }
    bool sleepFor(int ms, const CancelToken& token) override {
        ++sleepCalls;
        if (token.isCancelled()) return false;
        now += ms;
        if (onSleep) onSleep(now);
        return !token.isCancelled();
    }
};

struct FakeWindowProvider : IWindowProvider {
    std::vector<WindowInfo> windows;
    bool selfElevated = false;
    bool foreground = true;  // isForeground 的返回值
    int notFoundFirst = 0;  // 前 N 次 findWindow 返回 WindowNotFound（模拟窗口稍后出现）
    Status activateResult = okStatus();
    std::set<std::uint64_t> invalidHandles;
    int findCalls = 0;
    std::vector<std::uint64_t> activated;

    static WindowInfo window(std::uint64_t handle, const std::string& proc,
                             const std::string& title = "", bool elevated = false) {
        WindowInfo w;
        w.handle = handle;
        w.processName = proc;
        w.title = title;
        w.visible = true;
        w.elevated = elevated;
        return w;
    }

    std::vector<WindowInfo> enumerateWindows() override { return windows; }

    Result<WindowInfo> findWindow(const WindowSpec& spec) override {
        ++findCalls;
        if (findCalls <= notFoundFirst) {
            return Result<WindowInfo>::failure(ErrCode::WindowNotFound, "not yet");
        }
        return pickUniqueWindow(windows, spec);
    }

    Status activateWindow(std::uint64_t handle) override {
        activated.push_back(handle);
        return activateResult;
    }
    bool isWindowValid(std::uint64_t handle) override { return invalidHandles.count(handle) == 0; }
    bool isForeground(std::uint64_t) override { return foreground; }
    bool isSelfElevated() override { return selfElevated; }
};

struct FakeElementLocator : IElementLocator {
    // handler 决定每次 locate 的结果；callIndex 从 0 开始，计所有调用
    std::function<Result<ElementRef>(const LeafLocator&, int callIndex)> handler;
    std::vector<std::string> calls;  // 每次调用的 strategy 名

    static ElementRef element(const std::string& name = "el", bool invoke = true, bool value = false) {
        ElementRef e;
        e.name = name;
        e.bounds = Rect{100, 200, 40, 20};
        e.supportsInvoke = invoke;
        e.supportsValue = value;
        e.nativeHandle = 77;
        return e;
    }

    Result<ElementRef> locate(std::uint64_t, const LeafLocator& loc, const CancelToken&) override {
        const int idx = static_cast<int>(calls.size());
        calls.push_back(strategyName(loc));
        if (handler) return handler(loc, idx);
        return Result<ElementRef>::failure(ErrCode::ElementNotFound);
    }
};

struct FakeInputController : IInputController {
    std::vector<std::string> actions;
    std::function<Status(const std::string& action)> hook;  // 返回失败则模拟该动作失败

    std::vector<std::string>* trace = nullptr;  // 可选：同时写入共享时间线
    Status record(const std::string& a) {
        actions.push_back(a);
        if (trace) trace->push_back(a);
        return hook ? hook(a) : okStatus();
    }
    std::uint64_t lastClickWindow = 0;
    Status clickPoint(Point p, std::uint64_t windowHandle) override {
        lastClickWindow = windowHandle;
        return record("click:" + std::to_string(p.x) + "," + std::to_string(p.y));
    }
    Status invoke(const ElementRef& e) override { return record("invoke:" + e.name); }
    Status setValue(const ElementRef& e, const std::string& text) override {
        return record("setValue:" + e.name + ":" + text);
    }
    Status typeText(const std::string& text, InputMethod m) override {
        return record(std::string("type:") + (m == InputMethod::Unicode ? "unicode:" : "clipboard:") + text);
    }
    Status hotkey(const std::vector<std::string>& keys) override {
        std::string s = "hotkey:";
        for (size_t i = 0; i < keys.size(); ++i) s += (i ? "+" : "") + keys[i];
        return record(s);
    }
};

struct FakeScreenshot : IScreenshot {
    std::vector<std::pair<std::uint64_t, std::string>> shots;
    Status result = okStatus();
    Status captureWindow(std::uint64_t h, const std::string& path) override {
        shots.emplace_back(h, path);
        return result;
    }
};

// 记录调用顺序，用来验证"替换 → 粘贴 → 还原"
struct FakeClipboard : IClipboard {
    std::vector<std::string>* trace = nullptr;  // 与 FakeInputController 共用时间线
    std::string content = "ORIGINAL";
    std::string saved;
    bool replaced = false;
    Status replaceResult = okStatus();
    Status restoreResult = okStatus();
    int replaceCalls = 0, restoreCalls = 0;

    Status replaceText(const std::string& t) override {
        ++replaceCalls;
        if (trace) trace->push_back("clip:set:" + t);
        if (!replaceResult.ok()) return replaceResult;
        saved = content;
        content = t;
        replaced = true;
        return okStatus();
    }
    Status restore() override {
        ++restoreCalls;
        if (trace) trace->push_back("clip:restore");
        if (!restoreResult.ok()) return restoreResult;
        if (replaced) content = saved;
        replaced = false;
        return okStatus();
    }
};

struct FakeStopHotkey : IStopHotkey {
    Status registerResult = okStatus();
    std::function<void()> callback;
    HotkeySpec registered;
    int registerCalls = 0;
    int unregisterCalls = 0;
    bool active = false;

    Status registerHotkey(const HotkeySpec& spec, std::function<void()> onPressed) override {
        ++registerCalls;
        if (!registerResult.ok()) return registerResult;
        registered = spec;
        callback = std::move(onPressed);
        active = true;
        return okStatus();
    }
    void unregisterHotkey() override {
        ++unregisterCalls;
        active = false;
        callback = nullptr;
    }
    void press() {  // 模拟用户按下热键
        if (active && callback) callback();
    }
};

}  // namespace wa::fakes

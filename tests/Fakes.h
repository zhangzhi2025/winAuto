// 可脚本化的 Fake 适配器：让引擎能在没有真实桌面的环境里测试
// 未找到 / 歧义 / 超时 / 提权 / 取消等所有场景。
#pragma once

#include <deque>
#include <functional>
#include <string>
#include <vector>

#include "core/Platform.h"

namespace fakes {

using namespace winauto;

// 虚拟时间：sleep 只推进时钟，不真正等待。hook 可在每次推进后触发（例如到点取消）。
class FakeClock : public IClock {
public:
    std::int64_t nowMs() override { return now_; }
    bool sleep(int ms, const CancelToken& token) override {
        if (token.isCancelled()) return false;
        now_ += ms;
        sleeps.push_back(ms);
        if (onTick) onTick(now_);
        return !token.isCancelled();
    }
    std::int64_t now_ = 1000;
    std::vector<int> sleeps;
    std::function<void(std::int64_t)> onTick;
};

class FakeWindowProvider : public IWindowProvider {
public:
    std::vector<WindowInfo> enumerateWindows() override {
        ++enumerateCalls;
        if (onEnumerate) onEnumerate(windows, enumerateCalls);
        return windows;
    }
    Status activateWindow(std::uint64_t handle) override {
        activated.push_back(handle);
        return activateResult;
    }
    bool isWindowValid(const WindowInfo& info) override {
        for (const auto& w : windows)
            if (w.handle == info.handle && w.processId == info.processId && w.processPath == info.processPath) return true;
        return false;
    }
    std::uint32_t currentProcessId() const override { return selfPid; }
    bool isSelfElevated() const override { return selfElevated; }

    static WindowInfo makeWindow(std::uint64_t handle, const std::string& process, const std::string& title,
                                 std::uint32_t pid = 100) {
        WindowInfo w;
        w.handle = handle;
        w.processId = pid;
        w.processName = process;
        w.processPath = "C:\\Apps\\" + process;
        w.title = title;
        w.bounds = {0, 0, 800, 600};
        w.visible = true;
        return w;
    }

    std::vector<WindowInfo> windows;
    Status activateResult;
    std::vector<std::uint64_t> activated;
    std::uint32_t selfPid = 9999;
    bool selfElevated = false;
    int enumerateCalls = 0;
    std::function<void(std::vector<WindowInfo>&, int)> onEnumerate;
};

// 按队列返回预设结果；队列用完后返回 defaultResult。
class FakeElementLocator : public IElementLocator {
public:
    struct Outcome {
        Status status;
        ElementRef ref;
    };
    Status locate(std::uint64_t, const Locator& leaf, ElementRef* out) override {
        calls.push_back(describe(leaf));
        Outcome o = defaultOutcome;
        if (handler) {
            o = handler(leaf, static_cast<int>(calls.size()));
        } else if (!queue.empty()) {
            o = queue.front();
            queue.pop_front();
        }
        if (o.status.ok() && out) *out = o.ref;
        return o.status;
    }
    void pushFound(const std::string& name = "elem") {
        Outcome o;
        o.ref.id = 1;
        o.ref.name = name;
        o.ref.bounds = {10, 10, 80, 20};
        queue.push_back(o);
    }
    void pushFail(ErrCode code, const std::string& detail = "") {
        Outcome o;
        o.status = Status::fail(code, detail);
        queue.push_back(o);
    }

    std::deque<Outcome> queue;
    Outcome defaultOutcome = [] {
        Outcome o;
        o.status = Status::fail(ErrCode::ElementNotFound, "default");
        return o;
    }();
    std::function<Outcome(const Locator&, int callNo)> handler;
    std::vector<std::string> calls;
};

class FakeInputController : public IInputController {
public:
    Status clickElement(std::uint64_t, const ElementRef& e, ClickMode m) override {
        actions.push_back(std::string("click:") + e.name + ":" + toString(m));
        return next("click");
    }
    Status typeText(std::uint64_t, const std::string& text, InputMethod m) override {
        typed.push_back(text);
        actions.push_back(std::string("type:") + toString(m));
        Status st = next("type");
        if (!st.ok() && echoTextInErrors) st.detail += " while typing '" + text + "'";
        return st;
    }
    Status setValue(std::uint64_t, const ElementRef& e, const std::string& text) override {
        typed.push_back(text);
        actions.push_back("setvalue:" + e.name);
        Status st = next("setvalue");
        if (!st.ok() && echoTextInErrors) st.detail += " while setting '" + text + "'";
        return st;
    }
    Status sendHotkey(std::uint64_t, const std::vector<std::string>& keys) override {
        std::string joined;
        for (const auto& k : keys) joined += (joined.empty() ? "" : "+") + k;
        actions.push_back("hotkey:" + joined);
        return next("hotkey");
    }

    // 为下一次调用预设失败
    void failNext(ErrCode code, const std::string& detail = "") { failures.push_back(Status::fail(code, detail)); }

    std::vector<std::string> actions;
    std::vector<std::string> typed;
    bool echoTextInErrors = false;

private:
    Status next(const char*) {
        if (failures.empty()) return Status::success();
        Status st = failures.front();
        failures.pop_front();
        return st;
    }
    std::deque<Status> failures;
};

class FakePrompt : public IPromptProvider {
public:
    Result<std::string> promptSecret(const std::string& stepId, const std::string&) override {
        asked.push_back(stepId);
        if (!available) return Result<std::string>::fail(ErrCode::PromptUnavailable, "no prompt");
        return Result<std::string>::success(secret);
    }
    bool available = true;
    std::string secret = "S3cr3t-Value";
    std::vector<std::string> asked;
};

}  // namespace fakes

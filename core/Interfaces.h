#pragma once
// core 与平台之间的 4 个接口。只在需要替身（Fake）的地方设接口：
//   IWindowProvider / IElementLocator / IInputController / IClock
// 另有两个可选的小接口：IScreenshot（失败截图）、IClipboard（剪贴板输入）。
// 这些接口的真实实现位于 platform_win/。
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>

#include "core/CancelToken.h"
#include "core/ErrCode.h"
#include "core/Model.h"

namespace wa {

struct WindowInfo {
    std::uint64_t handle = 0;  // HWND 的数值，仅在平台层转换
    std::uint32_t processId = 0;
    std::string processName;
    std::string processPath;
    std::string title;
    Rect bounds;
    bool visible = false;
    bool elevated = false;  // 目标进程是否以管理员权限运行
};

class IWindowProvider {
public:
    virtual ~IWindowProvider() = default;
    virtual std::vector<WindowInfo> enumerateWindows() = 0;
    // 按条件查找唯一窗口。不存在 → WindowNotFound；多个候选同样返回 WindowNotFound，
    // 并在 detail 里说明"不唯一"（目标不明确时宁可失败，也不猜）。
    virtual Result<WindowInfo> findWindow(const WindowSpec& spec) = 0;
    virtual Status activateWindow(std::uint64_t handle) = 0;
    virtual bool isWindowValid(std::uint64_t handle) = 0;
    // 目标窗口（或其子窗口）是否在前台。键盘输入只会发给前台窗口，引擎在输入前必须确认。
    virtual bool isForeground(std::uint64_t handle) = 0;
    virtual bool isSelfElevated() = 0;
};

struct ElementRef {
    Rect bounds;  // 屏幕物理像素
    std::string name;
    std::string controlType;
    std::uint64_t nativeHandle = 0;  // 平台侧不透明句柄；runtimeId 会失效，不能当引用
    bool supportsInvoke = false;
    bool supportsValue = false;
    bool enabled = true;  // IsEnabled；禁用的控件不能点击或写入
};

class IElementLocator {
public:
    virtual ~IElementLocator() = default;
    // 单次定位，不做等待。0 个候选 → ElementNotFound；多个候选且 ambiguity=error → ElementAmbiguous。
    virtual Result<ElementRef> locate(std::uint64_t windowHandle, const LeafLocator& locator,
                                      const CancelToken& token) = 0;
};

class IInputController {
public:
    virtual ~IInputController() = default;
    // windowHandle：点击必须落在该窗口上。实现需确认落点处最上层的窗口属于它（防止被其他窗口遮挡时误点）；
    // 为 0 表示不检查。
    virtual Status clickPoint(Point screenPoint, std::uint64_t windowHandle) = 0;
    // Moves the pointer only; when windowHandle is nonzero, the point must belong to that window.
    virtual Status movePoint(Point screenPoint, std::uint64_t windowHandle) = 0;
    virtual Status invoke(const ElementRef& element) = 0;
    virtual Status setValue(const ElementRef& element, const std::string& text) = 0;
    virtual Status typeText(const std::string& text, InputMethod method) = 0;
    virtual Status hotkey(const std::vector<std::string>& normalizedKeys) = 0;
};

// 失败截图。只截目标窗口所在的屏幕区域（所见即所得，能拍到盖在上面的弹窗），不截整个桌面。
class IScreenshot {
public:
    virtual ~IScreenshot() = default;
    // 把窗口所在的屏幕区域存为 PNG；目录不存在则创建。失败不影响流程结果，调用方只记日志。
    virtual Status captureWindow(std::uint64_t windowHandle, const std::string& pngPath) = 0;
};

// 剪贴板输入用：把剪贴板临时换成要输入的文本，粘贴后还原。
class IClipboard {
public:
    virtual ~IClipboard() = default;
    // 保存当前剪贴板内容，再写入 utf8 文本。实现应标记为"不进入剪贴板历史、不同步到云"。
    // 同一时间只允许一次进行中的替换（再次调用前必须 restore）。
    virtual Status replaceText(const std::string& utf8) = 0;
    // 还原 replaceText 之前的内容。若剪贴板此后已被别的程序/用户改动，则不覆盖，返回 Ok 并在 detail 里说明。
    virtual Status restore() = 0;
};

class IClock {
public:
    virtual ~IClock() = default;
    virtual std::int64_t nowMs() const = 0;  // 单调时钟
    // 可被取消打断的睡眠；返回 false 表示被取消。
    virtual bool sleepFor(int ms, const CancelToken& token) = 0;
};

struct ScriptHostRequest {
    std::uint64_t requestId = 0;
    std::string operation;
    std::string argumentsJson;
};

struct ScriptHostResponse {
    std::string resultJson;
};

using ScriptHostHandler = std::function<Result<ScriptHostResponse>(const ScriptHostRequest&)>;

struct ScriptRequest {
    std::string stepId;
    ScriptLanguage language = ScriptLanguage::Python;
    std::string code;
    int timeoutMs = 0;
    std::string workflowDataJson;
};

struct ScriptResult {
    std::optional<std::string> resultJson;
    std::string output;
};

class IScriptRunner {
public:
    virtual ~IScriptRunner() = default;
    virtual Result<ScriptResult> run(const ScriptRequest& request, const CancelToken& token,
                                     const ScriptHostHandler& host) = 0;
};

}  // namespace wa

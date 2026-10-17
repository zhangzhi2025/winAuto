// 平台抽象接口。core 只依赖这些接口，Win32 / UIA / SendInput 的真实实现放在 platform 层，
// 测试用 Fake 实现替代（见 tests/Fakes.h）。
//
// 只保留引擎测试真正需要替身的接口：窗口、定位、输入、时钟、运行时提示。
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "core/CancelToken.h"
#include "core/Error.h"
#include "core/Workflow.h"

namespace winauto {

struct Rect {
    int x = 0;
    int y = 0;
    int width = 0;
    int height = 0;
};

struct WindowInfo {
    std::uint64_t handle = 0;
    std::uint32_t processId = 0;
    std::string processName;  // 例如 notepad.exe
    std::string processPath;  // 完整路径，用于校验窗口没有被替换
    std::string title;
    Rect bounds;              // 物理像素
    bool visible = false;
    bool elevated = false;    // 目标进程是否以更高权限运行
};

class IWindowProvider {
public:
    virtual ~IWindowProvider() = default;
    virtual std::vector<WindowInfo> enumerateWindows() = 0;
    virtual Status activateWindow(std::uint64_t handle) = 0;
    // 句柄仍然存在，且 PID 与进程路径与 info 一致
    virtual bool isWindowValid(const WindowInfo& info) = 0;
    virtual std::uint32_t currentProcessId() const = 0;  // 用于把自身窗口排除出目标
    virtual bool isSelfElevated() const = 0;
};

// 控件引用：id 是适配器管理的不透明句柄（不能跨线程使用，不要当持久引用）
struct ElementRef {
    std::uint64_t id = 0;
    Rect bounds;
    std::string name;
    std::string controlType;
    std::vector<std::string> patterns;  // 例如 Invoke、Value
};

class IElementLocator {
public:
    virtual ~IElementLocator() = default;
    // leaf 永远不是 fallback（回退由引擎处理）。
    // 未找到 -> ElementNotFound；多个候选 -> ElementAmbiguous；不支持 -> Unsupported。
    virtual Status locate(std::uint64_t windowHandle, const Locator& leaf, ElementRef* out) = 0;
};

class IInputController {
public:
    virtual ~IInputController() = default;
    virtual Status clickElement(std::uint64_t windowHandle, const ElementRef& element, ClickMode mode) = 0;
    virtual Status typeText(std::uint64_t windowHandle, const std::string& utf8Text, InputMethod method) = 0;
    virtual Status setValue(std::uint64_t windowHandle, const ElementRef& element, const std::string& utf8Text) = 0;
    // keys 已规范化为大写，例如 {"CTRL","S"}
    virtual Status sendHotkey(std::uint64_t windowHandle, const std::vector<std::string>& keys) = 0;
};

class IClock {
public:
    virtual ~IClock() = default;
    virtual std::int64_t nowMs() = 0;  // 单调时钟
    // 可被取消打断的睡眠。返回 true 表示睡满，false 表示被取消。
    virtual bool sleep(int ms, const CancelToken& token) = 0;
};

// 运行时敏感输入（textFrom=prompt）。GUI 弹遮罩输入框，CLI 无回显读取。
class IPromptProvider {
public:
    virtual ~IPromptProvider() = default;
    virtual Result<std::string> promptSecret(const std::string& stepId, const std::string& label) = 0;
};

}  // namespace winauto

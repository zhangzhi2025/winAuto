#pragma once
// core 与平台之间的 4 个接口。只在需要替身（Fake）的地方设接口：
//   IWindowProvider / IElementLocator / IInputController / IClock
// 这些接口的真实实现位于 platform_win/（阶段 1 起）。
#include <cstdint>
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
    virtual bool isSelfElevated() = 0;
};

struct ElementRef {
    Rect bounds;  // 屏幕物理像素
    std::string name;
    std::string controlType;
    std::uint64_t nativeHandle = 0;  // 平台侧不透明句柄；runtimeId 会失效，不能当引用
    bool supportsInvoke = false;
    bool supportsValue = false;
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
    virtual Status clickPoint(Point screenPoint) = 0;
    virtual Status invoke(const ElementRef& element) = 0;
    virtual Status setValue(const ElementRef& element, const std::string& text) = 0;
    virtual Status typeText(const std::string& text, InputMethod method) = 0;
    virtual Status hotkey(const std::vector<std::string>& normalizedKeys) = 0;
};

class IClock {
public:
    virtual ~IClock() = default;
    virtual std::int64_t nowMs() const = 0;  // 单调时钟
    // 可被取消打断的睡眠；返回 false 表示被取消。
    virtual bool sleepFor(int ms, const CancelToken& token) = 0;
};

}  // namespace wa

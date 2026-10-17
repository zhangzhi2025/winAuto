#pragma once
#include "core/Interfaces.h"

namespace wa::platform {

// IInputController 的 SendInput 实现。要点：
//  - 这里只"原样发送"core 的输入计划（InputPlan），计划本身已有单元测试。
//  - 点击前确认落点最上层窗口属于目标窗口，被遮挡则拒绝，不盲点。
//  - 要求进程为 Per-Monitor V2 DPI 感知，否则拒绝点击（坐标会被系统缩放而偏位）。
//  - UIPI：向更高权限窗口发送输入会被系统静默丢弃且不报错，所以必须依赖引擎的提权预检。
//  - invoke / setValue 属于 UIA，阶段 3 实现；剪贴板输入暂不支持。
//  - typeText 一次发送全部按键，期间无法取消；请保持文本较短。
class Win32InputController : public IInputController {
public:
    Status clickPoint(Point screenPoint, std::uint64_t windowHandle) override;
    Status invoke(const ElementRef& element) override;
    Status setValue(const ElementRef& element, const std::string& text) override;
    Status typeText(const std::string& text, InputMethod method) override;
    Status hotkey(const std::vector<std::string>& normalizedKeys) override;
};

}  // namespace wa::platform

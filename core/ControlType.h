#pragma once
// UI Automation 控件类型：ID <-> 规范名。ID 为 UIA_*ControlTypeId 的数值（50000..50040）。
#include <string>
#include <vector>

namespace wa {

// 未知 ID 返回空串
std::string controlTypeName(int uiaControlTypeId);

// 忽略大小写，返回规范名（如 "button" -> "Button"）；不是合法控件类型返回空串
std::string canonicalControlType(const std::string& name);

// 全部 41 个规范名，供校验提示使用
const std::vector<std::string>& allControlTypes();

}  // namespace wa

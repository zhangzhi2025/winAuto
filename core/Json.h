#pragma once
// 流程 JSON 读写。读取器只负责"能不能表示成模型"：
//   - JSON 语法错误、字段类型错误、未知 strategy、嵌套 fallback → 失败，detail 带路径（如 steps[2].locator）
//   - 缺字段、未知 type、未知字段 → 照常读入，交给 WorkflowValidator 报告（唯一权威）
#include <string>

#include "core/ErrCode.h"
#include "core/Model.h"

namespace wa {

Result<Workflow> readWorkflow(const std::string& jsonText);

// 输出带缩进的 UTF-8 JSON；字段顺序固定，便于版本管理时对比。
std::string writeWorkflow(const Workflow& wf, int indent = 2);

// 把单个定位器输出为 JSON 片段（用于 pick 命令和以后的流程编辑器）；可直接粘贴进流程的 locator 字段。
// 读取单个定位器 JSON（界面的定位器编辑框使用）。语法/字段错误 → InvalidWorkflow，detail 带路径；语义问题交给校验器。
Result<Locator> readLocatorJson(const std::string& jsonText);

std::string locatorToJson(const Locator& locator, int indent = 2);

}  // namespace wa

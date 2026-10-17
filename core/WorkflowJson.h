// JSON <-> Workflow。
// 读取阶段负责"结构"问题：缺少字段、类型错误、未知字段、未知枚举值，
// 问题路径形如 steps[2].locator.strategy。语义问题由 WorkflowValidator 负责。
#pragma once

#include "core/Issue.h"
#include "core/Json.h"
#include "core/Workflow.h"

namespace winauto {

// 返回 true 表示没有结构错误（out 完整可用）。issues 只追加，不清空。
bool readWorkflow(const JsonValue& root, Workflow* out, std::vector<Issue>* issues);

// 规范化输出：字段顺序固定，只输出已设置的字段。
JsonValue writeWorkflow(const Workflow& workflow);

}  // namespace winauto

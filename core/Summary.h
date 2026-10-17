#pragma once
// 流程信任摘要：运行前展示给用户确认。别人分享的流程文件本质上是一段可执行的键鼠脚本，
// 用户必须能在运行前看清"它会操作哪些程序、会输入什么、会按哪些键"。
#include <string>

#include "core/Model.h"
#include "core/Validator.h"

namespace wa {

// 返回多行 UTF-8 文本。规则：
//   - sensitive 文本和运行时询问的文本绝不出现，只显示"已隐藏"/"运行时询问"
//   - 普通文本原样显示（超过 40 个字符截断），让用户看清将被输入的内容
//   - 高风险项（report 中 requiresConfirmation）和警告单独列出
std::string summarizeWorkflow(const Workflow& wf, const ValidationReport& report);

}  // namespace wa

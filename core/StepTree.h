#pragma once
// 步骤树：if / repeat / while 带子步骤，流程不再是平的。
// "行"（row）= 先序遍历的步骤序号：父步骤在前，then/body 分支的子步骤接着，然后是 else 分支。
// 引擎的调试闸门、界面的步骤列表、运行范围（只运行选中步骤）都用同一套行号，保证三者一致。
#include <string>
#include <vector>

#include "core/Model.h"

namespace wa {

struct StepRow {
    const Step* step = nullptr;
    int depth = 0;                  // 0 = 顶层
    std::string path;               // 校验器使用的路径，如 "steps[1].then[0]"
    size_t parent = static_cast<size_t>(-1);  // 父步骤的行号；顶层为 npos
    size_t subtreeEnd = 0;          // 本步骤及其所有子孙占用的最后一行（含）
    bool inElse = false;            // 位于父 if 的 else 分支
};

inline constexpr size_t kNoRow = static_cast<size_t>(-1);

// 先序展开。同一个 Workflow 的行号是稳定的（只取决于结构）。
std::vector<StepRow> flattenSteps(const Workflow& wf);

// 子步骤列表在 JSON 里的字段名：If 的 then 分支 "then"、else 分支 "else"，循环体 "body"
const char* branchName(StepType parent, bool elseBranch);

// 整棵树的步骤总数
size_t countSteps(const std::vector<Step>& steps);

// 某个步骤自身的 issue（不含子步骤的）：path 恰为 stepPath，或以 stepPath + "." / "[" 开头，但不进入子步骤列表
bool pathBelongsToStepOnly(const std::string& issuePath, const std::string& stepPath);

}  // namespace wa

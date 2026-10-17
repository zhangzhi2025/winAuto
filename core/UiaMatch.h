#pragma once
// 在 UI 树快照上做匹配与判定。平台层的 UIA 定位器只负责"读出快照"，规则全在这里。
#include <string>

#include "core/ErrCode.h"
#include "core/Model.h"
#include "core/UiaSnapshot.h"

namespace wa {

// 单个节点是否满足定位条件。给出的条件必须全部满足：
//   automationId  精确、区分大小写（它是程序员定义的标识）
//   controlType   忽略大小写，按规范名比较
//   name          按 locator.match：Exact 精确；Contains 为忽略 ASCII 大小写的子串
// 三个条件都为空时不匹配任何节点（拒绝"匹配所有控件"）。
bool nodeMatches(const UiaNode& node, const UiaLocator& locator);

// 在快照里选出目标节点，返回其下标。
//   - 不可见（offscreen）或零尺寸的节点不参与：它们点不到
//   - 0 个 → ElementNotFound，detail 带诊断信息
//   - 多个且 ambiguity=Error → ElementAmbiguous，detail 列出候选，绝不默认取第一个
//   - 多个且 ambiguity=First → 取文档顺序的第一个
Result<size_t> pickNode(const UiaSnapshot& snapshot, const UiaLocator& locator);

// 为什么没找到：给出能帮用户修正定位条件的线索（automationId 对但类型不对、名字相近、该类型有哪些控件……）
std::string diagnoseNoMatch(const UiaSnapshot& snapshot, const UiaLocator& locator);

// 缩进的树形文本，用于 inspect。超过 maxLines 行时截断并注明。
std::string dumpSnapshot(const UiaSnapshot& snapshot, size_t maxLines = 400);

struct LocatorSuggestion {
    UiaLocator locator;
    std::vector<std::string> warnings;
};

// 为快照里的某个节点生成最稳的定位条件：
//   1. automationId + controlType（若在快照里唯一）
//   2. name + controlType（若唯一；会警告名称随语言和版本变化）
//   3. 都不唯一 → 失败，建议改用 window_point
Result<LocatorSuggestion> suggestLocator(const UiaSnapshot& snapshot, size_t nodeIndex);

}  // namespace wa

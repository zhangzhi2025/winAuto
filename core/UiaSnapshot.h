#pragma once
// UI Automation 树的"快照"：纯数据，不含任何 COM 类型。
// 平台层把 UIA 元素读成快照，匹配、唯一性判定、失败诊断、定位建议都在 core 里完成并有单元测试。
#include <cstdint>
#include <string>
#include <vector>

#include "core/Model.h"

namespace wa {

struct UiaNode {
    int parent = -1;  // 父节点下标；根或未知为 -1
    int depth = 0;
    std::string runtimeId;  // 仅用于同一次会话内判断"是不是同一个元素"，不能写进流程
    std::string automationId;
    std::string name;
    std::string controlType;  // 规范名，如 "Button"、"Edit"
    std::string className;
    Rect bounds;  // 屏幕物理像素
    bool enabled = true;
    bool offscreen = false;
    bool supportsInvoke = false;
    bool supportsValue = false;
    std::uint64_t nativeHandle = 0;  // 平台侧不透明句柄（指向会话内保存的元素）
};

struct UiaSnapshot {
    std::vector<UiaNode> nodes;  // 按文档顺序（深度优先）
    bool truncated = false;      // 因数量上限被截断
};

}  // namespace wa

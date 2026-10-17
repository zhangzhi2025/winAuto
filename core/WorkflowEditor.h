#pragma once
// 流程编辑器的"视图模型"：所有编辑操作、撤销/重做、校验定位都在这里，UI 只负责显示和转发。
// 纯标准 C++，有单元测试；这样 Qt 层可以很薄（Qt 在容器里无法编译，逻辑不能放在那边）。
#include <string>
#include <vector>

#include "core/ErrCode.h"
#include "core/Model.h"
#include "core/Validator.h"

namespace wa {

class WorkflowEditor {
public:
    WorkflowEditor();  // 空白新流程

    // ---- 载入与保存状态
    void load(Workflow wf);  // 清空撤销历史，标记为未修改
    const Workflow& workflow() const { return wf_; }
    bool dirty() const { return dirty_; }
    void markSaved() { dirty_ = false; }

    // ---- 步骤编辑（返回的下标是操作后受影响步骤的位置）
    // 在 index 处插入默认步骤，index 超出范围则追加。窗口别名取第一个已有别名（没有则为空）。
    size_t addStep(size_t index, StepType type);
    Status removeStep(size_t index);
    // 把 from 处的步骤移到 to（移动后它在 to 位置）
    Status moveStep(size_t from, size_t to);
    // 复制并紧随其后插入，id 重新生成
    Result<size_t> duplicateStep(size_t index);
    // 整步替换（属性面板提交）。id 必须在流程内唯一。
    Status updateStep(size_t index, Step step);

    // ---- 窗口别名
    Status setWindow(const std::string& alias, WindowSpec spec);  // 新增或更新
    // 重命名并同步更新所有引用它的步骤；新名字已存在则拒绝
    Status renameWindow(const std::string& from, const std::string& to);
    // 仍被步骤引用则拒绝
    Status removeWindow(const std::string& alias);

    // ---- 流程属性
    void setName(const std::string& name);
    void setExecution(const ExecutionOptions& options);

    // ---- 撤销/重做（快照式，最多 100 步）
    bool canUndo() const { return !undo_.empty(); }
    bool canRedo() const { return !redo_.empty(); }
    void undo();
    void redo();

    // ---- 校验
    // 每次修改后失效，调用时重算。
    const ValidationReport& validation();
    // 路径以 "steps[index]" 开头的问题
    std::vector<Issue> issuesForStep(size_t index);
    // 不属于任何步骤的问题（windows / execution / 顶层）
    std::vector<Issue> issuesOutsideSteps();

    // 唯一的新步骤 id：S1、S2…
    std::string newStepId() const;

    static Step defaultStep(StepType type, const std::string& id, const std::string& window);

private:
    void snapshot();  // 修改前调用
    void changed();   // 修改后调用
    bool idUsed(const std::string& id, size_t except) const;

    Workflow wf_;
    bool dirty_ = false;
    std::vector<Workflow> undo_;
    std::vector<Workflow> redo_;
    bool validationValid_ = false;
    ValidationReport report_;
};

}  // namespace wa

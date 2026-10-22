#pragma once
// 流程编辑器的"视图模型"：所有编辑操作、撤销/重做、校验定位都在这里，UI 只负责显示和转发。
// 纯标准 C++，有单元测试；这样 Qt 层可以很薄（Qt 在容器里无法编译，逻辑不能放在那边）。
#include <string>
#include <vector>

#include "core/ErrCode.h"
#include "core/Model.h"
#include "core/StepTree.h"
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

    // ---- 步骤编辑
    // 所有 row 都是先序行号（见 StepTree.h）：父步骤、它的 then/body 子步骤、else 子步骤，依次排列。
    // 没有 if / 循环时，行号就是步骤下标。返回值是操作后受影响步骤的行号。
    size_t rowCount() const;
    const Step* stepAtRow(size_t row) const;
    std::vector<StepRow> rows() const;

    // 在 row 之前插入默认步骤（与 row 同级）；row 超出范围则追加到顶层末尾。窗口别名取第一个已有别名。
    size_t addStep(size_t row, StepType type);
    // 插到 row 的同级之后（row 是块语句时也在整块之后，而不是块里面）
    size_t addStepAfter(size_t row, StepType type);
    // 追加到块语句（if / repeat / while）的末尾；elseBranch 只对 if 有效
    Result<size_t> addStepInto(size_t blockRow, StepType type, bool elseBranch = false);
    // 删除步骤及其全部子步骤
    Status removeStep(size_t row);
    // 在同一个列表里上移（delta<0）或下移（delta>0）；到头则拒绝
    Status moveStep(size_t row, int delta);
    // 缩进：移到上一个同级块语句的末尾（intoElse 时进入 if 的 else 分支）；上一个同级不是块则拒绝
    Result<size_t> indentStep(size_t row, bool intoElse = false);
    // 取消缩进：移到父块语句之后；已在顶层则拒绝
    Result<size_t> outdentStep(size_t row);
    // 深拷贝并紧随其后插入；它和全部子步骤的 id 都会重新生成
    Result<size_t> duplicateStep(size_t row);
    // 整步替换（属性面板提交，含子步骤）。id 必须在整个流程内唯一。
    Status updateStep(size_t row, Step step);

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
    // 属于这一步自身的问题（不含它的子步骤的）
    std::vector<Issue> issuesForStep(size_t row);
    // 不属于任何步骤的问题（windows / execution / 顶层）
    std::vector<Issue> issuesOutsideSteps();

    // 唯一的新步骤 id：S1、S2…
    std::string newStepId() const;

    static Step defaultStep(StepType type, const std::string& id, const std::string& window);

private:
    void snapshot();  // 修改前调用
    void changed();   // 修改后调用
    bool idUsed(const std::string& id, size_t exceptRow) const;
    void commit(Workflow&& next);  // 快照 + 替换 + 标记已修改

    Workflow wf_;
    bool dirty_ = false;
    std::vector<Workflow> undo_;
    std::vector<Workflow> redo_;
    bool validationValid_ = false;
    ValidationReport report_;
};

}  // namespace wa

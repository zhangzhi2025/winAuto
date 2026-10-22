#include "TestUtil.h"
#include "core/WorkflowEditor.h"
#include "testing.h"

using namespace wa;
using namespace testutil;

namespace {
WorkflowEditor loaded() {
    WorkflowEditor e;
    e.load(minimalWorkflow());
    return e;
}
}  // namespace

TEST(new_editor_is_clean_and_unmodified) {
    WorkflowEditor e;
    CHECK(!e.dirty());
    CHECK(!e.canUndo());
    CHECK(e.workflow().steps.empty());
}

TEST(add_step_uses_unique_ids_and_first_window) {
    WorkflowEditor e = loaded();
    const size_t i = e.addStep(99, StepType::Click);  // 越界 → 追加
    CHECK_EQ(i, static_cast<size_t>(1));
    CHECK_EQ(e.workflow().steps[1].id, std::string("S2"));
    CHECK_EQ(e.workflow().steps[1].window, std::string("main"));
    CHECK(e.dirty());
    e.removeStep(0);  // 删掉 S1 后再加，id 不能和已有的重复
    e.addStep(0, StepType::Click);
    CHECK(e.workflow().steps[0].id != e.workflow().steps[1].id);
}

TEST(default_steps_are_sensible) {
    Step w = WorkflowEditor::defaultStep(StepType::Wait, "S1", "main");
    CHECK(w.window.empty());
    CHECK_EQ(w.durationMs.value_or(0), 1000);
    Step h = WorkflowEditor::defaultStep(StepType::Hotkey, "S1", "main");
    CHECK(!h.keys.empty());
}

TEST(move_and_duplicate_and_remove) {
    WorkflowEditor e = loaded();
    e.addStep(1, StepType::Click);
    e.addStep(2, StepType::Hotkey);
    CHECK(e.moveStep(2, -2).ok());
    CHECK(e.workflow().steps[0].type == StepType::Hotkey);
    CHECK(!e.moveStep(0, 9).ok());
    CHECK(!e.moveStep(0, -1).ok());
    auto d = e.duplicateStep(0);
    CHECK(d.ok());
    CHECK_EQ(d.value, static_cast<size_t>(1));
    CHECK(e.workflow().steps[1].type == StepType::Hotkey);
    CHECK(e.workflow().steps[0].id != e.workflow().steps[1].id);
    CHECK(e.removeStep(1).ok());
    CHECK(!e.removeStep(50).ok());
    CHECK_EQ(e.workflow().steps.size(), static_cast<size_t>(3));
}

TEST(update_step_rejects_duplicate_or_empty_id) {
    WorkflowEditor e = loaded();
    e.addStep(1, StepType::Click);
    Step s = e.workflow().steps[1];
    s.id = "S1";
    CHECK(!e.updateStep(1, s).ok());
    s.id = "";
    CHECK(!e.updateStep(1, s).ok());
    s.id = "Sx";
    CHECK(e.updateStep(1, s).ok());
    CHECK_EQ(e.workflow().steps[1].id, std::string("Sx"));
    // 同一步骤保留自己的 id 不算重复
    CHECK(e.updateStep(1, e.workflow().steps[1]).ok());
}

TEST(rename_window_updates_references) {
    WorkflowEditor e = loaded();
    e.addStep(1, StepType::Click);
    CHECK(e.renameWindow("main", "notepad").ok());
    CHECK_EQ(e.workflow().steps[0].window, std::string("notepad"));
    CHECK_EQ(e.workflow().steps[1].window, std::string("notepad"));
    CHECK(e.workflow().windows.count("notepad") == 1);
    CHECK(e.workflow().windows.count("main") == 0);
    e.setWindow("other", WindowSpec{"calc.exe", "", 1000});
    CHECK(!e.renameWindow("notepad", "other").ok());  // 已存在
    CHECK(!e.renameWindow("nope", "x").ok());
}

TEST(remove_window_refused_while_referenced) {
    WorkflowEditor e = loaded();
    auto st = e.removeWindow("main");
    CHECK(!st.ok());
    CHECK_CONTAINS(st.detail, "S1");
    e.setWindow("spare", WindowSpec{"a.exe", "", 1000});
    CHECK(e.removeWindow("spare").ok());
}

TEST(undo_redo_restore_exact_state) {
    WorkflowEditor e = loaded();
    e.addStep(1, StepType::Click);
    e.setName("changed");
    CHECK(e.canUndo());
    e.undo();
    CHECK_EQ(e.workflow().name, std::string("test"));
    CHECK_EQ(e.workflow().steps.size(), static_cast<size_t>(2));
    e.undo();
    CHECK_EQ(e.workflow().steps.size(), static_cast<size_t>(1));
    CHECK(!e.canUndo());
    CHECK(e.canRedo());
    e.redo();
    e.redo();
    CHECK_EQ(e.workflow().name, std::string("changed"));
    CHECK(!e.canRedo());
    e.undo();
    e.addStep(0, StepType::Wait);  // 新修改清空重做栈
    CHECK(!e.canRedo());
}

TEST(load_resets_history_and_dirty_flag) {
    WorkflowEditor e = loaded();
    e.addStep(1, StepType::Click);
    e.load(minimalWorkflow());
    CHECK(!e.dirty());
    CHECK(!e.canUndo());
    e.addStep(1, StepType::Click);
    CHECK(e.dirty());
    e.markSaved();
    CHECK(!e.dirty());
    e.undo();
    CHECK(e.dirty());  // 撤销之后与已保存内容不同
}

TEST(validation_issues_are_attributed_to_steps) {
    WorkflowEditor e = loaded();
    e.addStep(1, StepType::Click);  // 没有 locator → 错误
    CHECK(!e.validation().ok());
    CHECK(e.issuesForStep(0).empty());
    CHECK(!e.issuesForStep(1).empty());
    for (const auto& i : e.issuesForStep(1)) CHECK(i.path.compare(0, 8, "steps[1]") == 0);
    // steps[1] 不能误匹配 steps[10]
    for (int k = 0; k < 10; ++k) e.addStep(99, StepType::Click);
    for (const auto& i : e.issuesForStep(1)) CHECK(i.path.compare(0, 9, "steps[10]") != 0);
    // 修改后校验结果会刷新
    Step s = e.workflow().steps[1];
    s.locator = Locator{uia("btn", "Button")};
    e.updateStep(1, s);
    CHECK(e.issuesForStep(1).empty());
}

TEST(issues_outside_steps_are_reported_separately) {
    WorkflowEditor e = loaded();
    e.setWindow("main", WindowSpec{"", "", 5000});  // 缺进程名
    const auto outside = e.issuesOutsideSteps();
    CHECK(!outside.empty());
    for (const auto& i : outside) CHECK(i.path.compare(0, 6, "steps[") != 0);
}

TEST(edited_workflow_roundtrips_through_json) {
    WorkflowEditor e = loaded();
    e.addStep(1, StepType::Hotkey);
    auto back = readWorkflow(writeWorkflow(e.workflow()));
    CHECK(back.ok());
    CHECK_EQ(back.value.steps.size(), static_cast<size_t>(2));
    CHECK_EQ(back.value.steps[1].keys.size(), static_cast<size_t>(2));
}

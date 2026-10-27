#include "core/WorkflowEditor.h"

#include <algorithm>
#include <functional>
#include <set>

namespace wa {

namespace {
constexpr size_t kMaxUndo = 100;

struct Slot {
    std::vector<Step>* list = nullptr;
    size_t index = 0;
};

bool findSlot(std::vector<Step>& list, size_t& counter, size_t target, Slot& out) {
    for (size_t i = 0; i < list.size(); ++i) {
        if (counter == target) {
            out = Slot{&list, i};
            return true;
        }
        ++counter;
        if (findSlot(list[i].body, counter, target, out)) return true;
        if (findSlot(list[i].elseBody, counter, target, out)) return true;
    }
    return false;
}

bool slotAt(Workflow& wf, size_t row, Slot& out) {
    size_t counter = 0;
    return findSlot(wf.steps, counter, row, out);
}

size_t rowOfStep(const Workflow& wf, const Step* step) {
    const auto rows = flattenSteps(wf);
    for (size_t i = 0; i < rows.size(); ++i) {
        if (rows[i].step == step) return i;
    }
    return kNoRow;
}

bool tooDeep(const Workflow& wf) {
    for (const StepRow& r : flattenSteps(wf)) {
        if (isBlockType(r.step->type) && r.depth >= kMaxNesting) return true;
    }
    return false;
}

void collectIds(const std::vector<Step>& list, std::set<std::string>& ids) {
    for (const Step& s : list) {
        ids.insert(s.id);
        collectIds(s.body, ids);
        collectIds(s.elseBody, ids);
    }
}

const Status kOutOfRange = Status::failure(ErrCode::InvalidWorkflow, "step index out of range");
const char* const kTooDeepText = "blocks would be nested too deeply";
}  // namespace

WorkflowEditor::WorkflowEditor() {
    wf_.schemaVersion = "1.0";
    wf_.id = "WF-NEW";
    wf_.name = "新流程";
}

void WorkflowEditor::load(Workflow wf) {
    wf_ = std::move(wf);
    undo_.clear();
    redo_.clear();
    dirty_ = false;
    validationValid_ = false;
}

void WorkflowEditor::snapshot() {
    undo_.push_back(wf_);
    if (undo_.size() > kMaxUndo) undo_.erase(undo_.begin());
    redo_.clear();
}

void WorkflowEditor::changed() {
    dirty_ = true;
    validationValid_ = false;
}

bool WorkflowEditor::idUsed(const std::string& id, size_t exceptRow) const {
    const auto rows = flattenSteps(wf_);
    for (size_t i = 0; i < rows.size(); ++i) {
        if (i != exceptRow && rows[i].step->id == id) return true;
    }
    return false;
}

std::string WorkflowEditor::newStepId() const {
    std::set<std::string> ids;
    collectIds(wf_.steps, ids);
    for (size_t n = ids.size() + 1;; ++n) {
        const std::string id = "S" + std::to_string(n);
        if (!ids.count(id)) return id;
    }
}

Step WorkflowEditor::defaultStep(StepType type, const std::string& id, const std::string& window) {
    Step s;
    s.id = id;
    s.type = type;
    s.rawType = toString(type);
    s.window = window;
    switch (type) {
        case StepType::Hotkey:
            s.keys = {"CTRL", "A"};
            break;
        case StepType::Wait:
            s.window.clear();
            s.durationMs = 1000;
            break;
        case StepType::InputText:
        case StepType::SetValue:
            s.text = "";
            break;
        case StepType::If:
        case StepType::While: {
            // 条件的定位器留空（uia 且没有任何条件）：校验器会提示"需要拾取控件"，不会悄悄用一个猜出来的值
            Expect c;
            c.type = "element_exists";
            c.locator = Locator{UiaLocator{}};
            s.condition = c;
            if (type == StepType::While) s.maxIterations = 10;
            break;
        }
        case StepType::Repeat:
            s.window.clear();
            s.times = 3;
            break;
        default:
            break;
    }
    return s;
}

void WorkflowEditor::commit(Workflow&& next) {
    snapshot();
    wf_ = std::move(next);
    changed();
}

size_t WorkflowEditor::rowCount() const { return countSteps(wf_.steps); }
std::vector<StepRow> WorkflowEditor::rows() const { return flattenSteps(wf_); }

const Step* WorkflowEditor::stepAtRow(size_t row) const {
    const auto r = flattenSteps(wf_);
    return row < r.size() ? r[row].step : nullptr;
}

size_t WorkflowEditor::addStep(size_t row, StepType type) {
    Workflow next = wf_;
    const std::string window = next.windows.empty() ? "" : next.windows.begin()->first;
    Step s = defaultStep(type, newStepId(), window);
    Slot slot;
    std::vector<Step>* list = &next.steps;
    size_t at = list->size();
    if (slotAt(next, row, slot)) {
        list = slot.list;
        at = slot.index;
    }
    list->insert(list->begin() + static_cast<std::ptrdiff_t>(at), std::move(s));
    if (tooDeep(next)) return kNoRow;  // 只会发生在已经很深的块里再加块；调用方按"没有新增"处理
    const size_t newRow = rowOfStep(next, &(*list)[at]);
    commit(std::move(next));
    return newRow;
}

size_t WorkflowEditor::addStepAfter(size_t row, StepType type) {
    Workflow next = wf_;
    const std::string window = next.windows.begin() == next.windows.end() ? "" : next.windows.begin()->first;
    Step s = defaultStep(type, newStepId(), window);
    Slot slot;
    std::vector<Step>* list = &next.steps;
    size_t at = list->size();
    if (slotAt(next, row, slot)) {
        list = slot.list;
        at = slot.index + 1;
    }
    list->insert(list->begin() + static_cast<std::ptrdiff_t>(at), std::move(s));
    if (tooDeep(next)) return kNoRow;
    const size_t newRow = rowOfStep(next, &(*list)[at]);
    commit(std::move(next));
    return newRow;
}

Result<size_t> WorkflowEditor::addStepInto(size_t blockRow, StepType type, bool elseBranch) {
    using R = Result<size_t>;
    Workflow next = wf_;
    Slot slot;
    if (!slotAt(next, blockRow, slot)) return R::failure(ErrCode::InvalidWorkflow, "step index out of range");
    Step& block = (*slot.list)[slot.index];
    if (!isBlockType(block.type)) return R::failure(ErrCode::InvalidWorkflow, "step " + block.id + " is not a block");
    if (elseBranch && block.type != StepType::If) {
        return R::failure(ErrCode::InvalidWorkflow, "only if has an else branch");
    }
    const std::string window = next.windows.empty() ? "" : next.windows.begin()->first;
    std::vector<Step>& target = elseBranch ? block.elseBody : block.body;
    target.push_back(defaultStep(type, newStepId(), window));
    if (tooDeep(next)) return R::failure(ErrCode::InvalidWorkflow, kTooDeepText);
    const size_t newRow = rowOfStep(next, &target.back());
    commit(std::move(next));
    return R::success(newRow);
}

Status WorkflowEditor::removeStep(size_t row) {
    Workflow next = wf_;
    Slot slot;
    if (!slotAt(next, row, slot)) return kOutOfRange;
    slot.list->erase(slot.list->begin() + static_cast<std::ptrdiff_t>(slot.index));
    commit(std::move(next));
    return Status::success();
}

Status WorkflowEditor::moveStep(size_t row, int delta) {
    Workflow next = wf_;
    Slot slot;
    if (!slotAt(next, row, slot)) return kOutOfRange;
    const long long to = static_cast<long long>(slot.index) + delta;
    if (delta == 0) return Status::success();
    if (to < 0 || to >= static_cast<long long>(slot.list->size())) {
        return Status::failure(ErrCode::InvalidWorkflow, "cannot move beyond the start or end of this list");
    }
    Step moved = std::move((*slot.list)[slot.index]);
    slot.list->erase(slot.list->begin() + static_cast<std::ptrdiff_t>(slot.index));
    slot.list->insert(slot.list->begin() + static_cast<std::ptrdiff_t>(to), std::move(moved));
    commit(std::move(next));
    return Status::success();
}

Result<size_t> WorkflowEditor::indentStep(size_t row, bool intoElse) {
    using R = Result<size_t>;
    Workflow next = wf_;
    Slot slot;
    if (!slotAt(next, row, slot)) return R::failure(ErrCode::InvalidWorkflow, "step index out of range");
    if (slot.index == 0) return R::failure(ErrCode::InvalidWorkflow, "there is no block above to move into");
    Step& prev = (*slot.list)[slot.index - 1];
    if (!isBlockType(prev.type)) {
        return R::failure(ErrCode::InvalidWorkflow, "the step above (" + prev.id + ") is not an if / repeat / while block");
    }
    if (intoElse && prev.type != StepType::If) {
        return R::failure(ErrCode::InvalidWorkflow, "only if has an else branch");
    }
    Step moved = std::move((*slot.list)[slot.index]);
    slot.list->erase(slot.list->begin() + static_cast<std::ptrdiff_t>(slot.index));
    // 删除的是 prev 之后的元素，prev 的地址不变
    std::vector<Step>& target = intoElse ? prev.elseBody : prev.body;
    target.push_back(std::move(moved));
    if (tooDeep(next)) return R::failure(ErrCode::InvalidWorkflow, kTooDeepText);
    const size_t newRow = rowOfStep(next, &target.back());
    commit(std::move(next));
    return R::success(newRow);
}

Result<size_t> WorkflowEditor::outdentStep(size_t row) {
    using R = Result<size_t>;
    Workflow next = wf_;
    const auto rows = flattenSteps(next);
    if (row >= rows.size()) return R::failure(ErrCode::InvalidWorkflow, "step index out of range");
    if (rows[row].parent == kNoRow) return R::failure(ErrCode::InvalidWorkflow, "the step is already at the top level");
    Slot self, parent;
    if (!slotAt(next, row, self) || !slotAt(next, rows[row].parent, parent)) return R::failure(ErrCode::InvalidWorkflow, "step index out of range");
    Step moved = std::move((*self.list)[self.index]);
    self.list->erase(self.list->begin() + static_cast<std::ptrdiff_t>(self.index));
    // 此时 self.list 属于 parent 步骤，erase 不会移动 parent 在它自己所在列表里的位置
    const size_t at = parent.index + 1;
    parent.list->insert(parent.list->begin() + static_cast<std::ptrdiff_t>(at), std::move(moved));
    const size_t newRow = rowOfStep(next, &(*parent.list)[at]);
    commit(std::move(next));
    return R::success(newRow);
}

namespace {
void renewIds(Step& s, const std::function<std::string()>& next) {
    s.id = next();
    for (Step& c : s.body) renewIds(c, next);
    for (Step& c : s.elseBody) renewIds(c, next);
}
}  // namespace

Result<size_t> WorkflowEditor::duplicateStep(size_t row) {
    using R = Result<size_t>;
    Workflow next = wf_;
    Slot slot;
    if (!slotAt(next, row, slot)) return R::failure(ErrCode::InvalidWorkflow, "step index out of range");
    Step copy = (*slot.list)[slot.index];
    // 逐个生成新 id：已用的集合要把刚分配的也算进去
    std::set<std::string> used;
    collectIds(next.steps, used);
    size_t n = used.size();
    renewIds(copy, [&]() {
        for (;;) {
            const std::string id = "S" + std::to_string(++n);
            if (used.insert(id).second) return id;
        }
    });
    const size_t at = slot.index + 1;
    slot.list->insert(slot.list->begin() + static_cast<std::ptrdiff_t>(at), std::move(copy));
    const size_t newRow = rowOfStep(next, &(*slot.list)[at]);
    commit(std::move(next));
    return R::success(newRow);
}

Status WorkflowEditor::updateStep(size_t row, Step step) {
    Workflow next = wf_;
    Slot slot;
    if (!slotAt(next, row, slot)) return kOutOfRange;
    if (step.id.empty()) return Status::failure(ErrCode::InvalidWorkflow, "step id must not be empty");
    if (idUsed(step.id, row)) {
        return Status::failure(ErrCode::InvalidWorkflow, "step id \"" + step.id + "\" is already used");
    }
    step.rawType = toString(step.type);
    (*slot.list)[slot.index] = std::move(step);
    if (tooDeep(next)) return Status::failure(ErrCode::InvalidWorkflow, kTooDeepText);
    commit(std::move(next));
    return Status::success();
}
Status WorkflowEditor::setWindow(
    const std::string& alias,
    WindowSpec spec)
{
    if (alias.empty()) {
        return Status::failure(
            ErrCode::InvalidWorkflow,
            "window alias must not be empty");
    }
    snapshot();
    wf_.windows[alias] = std::move(spec);
    changed();
    return Status::success();
}

Status WorkflowEditor::renameWindow(const std::string& from, const std::string& to) {
    auto it = wf_.windows.find(from);
    if (it == wf_.windows.end()) return Status::failure(ErrCode::InvalidWorkflow, "no such window alias");
    if (to.empty()) return Status::failure(ErrCode::InvalidWorkflow, "window alias must not be empty");
    if (from == to) return Status::success();
    if (wf_.windows.count(to)) {
        return Status::failure(ErrCode::InvalidWorkflow, "window alias \"" + to + "\" already exists");
    }
    snapshot();
    WindowSpec spec = it->second;
    wf_.windows.erase(it);
    wf_.windows[to] = std::move(spec);
    std::function<void(std::vector<Step>&)> rename = [&](std::vector<Step>& list) {
        for (Step& s : list) {
            if (s.window == from) s.window = to;
            rename(s.body);
            rename(s.elseBody);
        }
    };
    rename(wf_.steps);
    changed();
    return Status::success();
}

Status WorkflowEditor::removeWindow(const std::string& alias) {
    if (!wf_.windows.count(alias)) return Status::failure(ErrCode::InvalidWorkflow, "no such window alias");
    for (const StepRow& r : flattenSteps(wf_)) {
        if (r.step->window == alias) {
            return Status::failure(ErrCode::InvalidWorkflow,
                                   "window \"" + alias + "\" is still used by step " + r.step->id);
        }
    }
    snapshot();
    wf_.windows.erase(alias);
    changed();
    return Status::success();
}

void WorkflowEditor::setName(const std::string& name) {
    if (name == wf_.name) return;
    snapshot();
    wf_.name = name;
    changed();
}

void WorkflowEditor::setExecution(const ExecutionOptions& options) {
    snapshot();
    wf_.execution = options;
    changed();
}

void WorkflowEditor::undo() {
    if (undo_.empty()) return;
    redo_.push_back(wf_);
    wf_ = std::move(undo_.back());
    undo_.pop_back();
    changed();
}

void WorkflowEditor::redo() {
    if (redo_.empty()) return;
    undo_.push_back(wf_);
    wf_ = std::move(redo_.back());
    redo_.pop_back();
    changed();
}

const ValidationReport& WorkflowEditor::validation() {
    if (!validationValid_) {
        report_ = validateWorkflow(wf_);
        validationValid_ = true;
    }
    return report_;
}

std::vector<Issue> WorkflowEditor::issuesForStep(size_t row) {
    std::vector<Issue> out;
    const auto r = flattenSteps(wf_);
    if (row >= r.size()) return out;
    for (const Issue& i : validation().issues) {
        if (pathBelongsToStepOnly(i.path, r[row].path)) out.push_back(i);
    }
    return out;
}

std::vector<Issue> WorkflowEditor::issuesOutsideSteps() {
    std::vector<Issue> out;
    for (const Issue& i : validation().issues) {
        if (i.path.compare(0, 6, "steps[") != 0) out.push_back(i);
    }
    return out;
}

}  // namespace wa

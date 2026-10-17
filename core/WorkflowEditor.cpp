#include "core/WorkflowEditor.h"

#include <algorithm>
#include <set>

namespace wa {

namespace {
constexpr size_t kMaxUndo = 100;

std::string stepPrefix(size_t i) { return "steps[" + std::to_string(i) + "]"; }

bool startsWithStep(const std::string& path, size_t i) {
    const std::string p = stepPrefix(i);
    if (path.compare(0, p.size(), p) != 0) return false;
    return path.size() == p.size() || path[p.size()] == '.' || path[p.size()] == '[';
}
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

bool WorkflowEditor::idUsed(const std::string& id, size_t except) const {
    for (size_t i = 0; i < wf_.steps.size(); ++i) {
        if (i != except && wf_.steps[i].id == id) return true;
    }
    return false;
}

std::string WorkflowEditor::newStepId() const {
    for (size_t n = wf_.steps.size() + 1;; ++n) {
        const std::string id = "S" + std::to_string(n);
        if (!idUsed(id, static_cast<size_t>(-1))) return id;
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
        default:
            break;
    }
    return s;
}

size_t WorkflowEditor::addStep(size_t index, StepType type) {
    snapshot();
    const std::string window = wf_.windows.empty() ? "" : wf_.windows.begin()->first;
    Step s = defaultStep(type, newStepId(), window);
    if (index > wf_.steps.size()) index = wf_.steps.size();
    wf_.steps.insert(wf_.steps.begin() + static_cast<std::ptrdiff_t>(index), std::move(s));
    changed();
    return index;
}

Status WorkflowEditor::removeStep(size_t index) {
    if (index >= wf_.steps.size()) return Status::failure(ErrCode::InvalidWorkflow, "step index out of range");
    snapshot();
    wf_.steps.erase(wf_.steps.begin() + static_cast<std::ptrdiff_t>(index));
    changed();
    return Status::success();
}

Status WorkflowEditor::moveStep(size_t from, size_t to) {
    if (from >= wf_.steps.size() || to >= wf_.steps.size()) {
        return Status::failure(ErrCode::InvalidWorkflow, "step index out of range");
    }
    if (from == to) return Status::success();
    snapshot();
    Step s = std::move(wf_.steps[from]);
    wf_.steps.erase(wf_.steps.begin() + static_cast<std::ptrdiff_t>(from));
    wf_.steps.insert(wf_.steps.begin() + static_cast<std::ptrdiff_t>(to), std::move(s));
    changed();
    return Status::success();
}

Result<size_t> WorkflowEditor::duplicateStep(size_t index) {
    if (index >= wf_.steps.size()) {
        return Result<size_t>::failure(ErrCode::InvalidWorkflow, "step index out of range");
    }
    snapshot();
    Step copy = wf_.steps[index];
    copy.id = newStepId();
    wf_.steps.insert(wf_.steps.begin() + static_cast<std::ptrdiff_t>(index) + 1, std::move(copy));
    changed();
    return Result<size_t>::success(index + 1);
}

Status WorkflowEditor::updateStep(size_t index, Step step) {
    if (index >= wf_.steps.size()) return Status::failure(ErrCode::InvalidWorkflow, "step index out of range");
    if (step.id.empty()) return Status::failure(ErrCode::InvalidWorkflow, "step id must not be empty");
    if (idUsed(step.id, index)) {
        return Status::failure(ErrCode::InvalidWorkflow, "step id \"" + step.id + "\" is already used");
    }
    snapshot();
    step.rawType = toString(step.type);
    wf_.steps[index] = std::move(step);
    changed();
    return Status::success();
}

Status WorkflowEditor::setWindow(const std::string& alias, WindowSpec spec) {
    if (alias.empty()) return Status::failure(ErrCode::InvalidWorkflow, "window alias must not be empty");
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
    for (Step& s : wf_.steps) {
        if (s.window == from) s.window = to;
    }
    changed();
    return Status::success();
}

Status WorkflowEditor::removeWindow(const std::string& alias) {
    if (!wf_.windows.count(alias)) return Status::failure(ErrCode::InvalidWorkflow, "no such window alias");
    for (size_t i = 0; i < wf_.steps.size(); ++i) {
        if (wf_.steps[i].window == alias) {
            return Status::failure(ErrCode::InvalidWorkflow,
                                   "window \"" + alias + "\" is still used by step " + wf_.steps[i].id);
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

std::vector<Issue> WorkflowEditor::issuesForStep(size_t index) {
    std::vector<Issue> out;
    for (const Issue& i : validation().issues) {
        if (startsWithStep(i.path, index)) out.push_back(i);
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

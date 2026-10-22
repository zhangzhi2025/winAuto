#include "core/StepTree.h"

namespace wa {

namespace {

void walk(const std::vector<Step>& list, const std::string& listPath, int depth, size_t parent, bool inElse,
          std::vector<StepRow>& out) {
    for (size_t i = 0; i < list.size(); ++i) {
        const Step& s = list[i];
        const size_t row = out.size();
        StepRow r;
        r.step = &s;
        r.depth = depth;
        r.path = listPath + "[" + std::to_string(i) + "]";
        r.parent = parent;
        r.inElse = inElse;
        out.push_back(r);
        walk(s.body, out[row].path + "." + branchName(s.type, false), depth + 1, row, false, out);
        walk(s.elseBody, out[row].path + "." + branchName(s.type, true), depth + 1, row, true, out);
        out[row].subtreeEnd = out.size() - 1;
    }
}

}  // namespace

std::vector<StepRow> flattenSteps(const Workflow& wf) {
    std::vector<StepRow> out;
    walk(wf.steps, "steps", 0, kNoRow, false, out);
    return out;
}

const char* branchName(StepType parent, bool elseBranch) {
    if (parent == StepType::If) return elseBranch ? "else" : "then";
    return elseBranch ? "else" : "body";
}

size_t countSteps(const std::vector<Step>& steps) {
    size_t n = steps.size();
    for (const Step& s : steps) n += countSteps(s.body) + countSteps(s.elseBody);
    return n;
}

bool pathBelongsToStepOnly(const std::string& issuePath, const std::string& stepPath) {
    if (issuePath.compare(0, stepPath.size(), stepPath) != 0) return false;
    if (issuePath.size() == stepPath.size()) return true;
    const char c = issuePath[stepPath.size()];
    if (c != '.' && c != '[') return false;  // steps[1] 不能匹配 steps[10]
    // 去掉前缀后的第一段若是子步骤列表（then / else / body），说明属于子步骤
    const std::string rest = issuePath.substr(stepPath.size() + (c == '.' ? 1 : 0));
    for (const char* b : {"then[", "else[", "body["}) {
        if (rest.compare(0, 5, b) == 0) return false;
    }
    return true;
}

}  // namespace wa

#pragma once
// WorkflowValidator：流程语义校验的唯一权威。
// schemas/workflow.schema.json 只用于文档和编辑器提示，规则以这里为准。
#include <string>
#include <vector>

#include "core/Model.h"

namespace wa {

enum class Severity { Error, Warning };

struct Issue {
    Severity severity = Severity::Error;
    std::string path;     // 如 "steps[2].locator"
    std::string code;     // 稳定的机器可读标识，如 "duplicate_step_id"
    std::string message;  // 面向用户的说明
    bool requiresConfirmation = false;  // 高风险操作：用户确认后才允许运行
};

struct ValidationReport {
    std::vector<Issue> issues;

    bool ok() const;  // 没有 Error
    size_t errorCount() const;
    size_t warningCount() const;
    bool needsConfirmation() const;
    bool has(const std::string& code) const;
    const Issue* find(const std::string& code, const std::string& path = {}) const;
};

ValidationReport validateWorkflow(const Workflow& wf);

// 按键名工具：大小写不敏感，统一返回大写
std::string normalizeKey(const std::string& key);
bool isSupportedKey(const std::string& key);
bool isModifierKey(const std::string& key);

}  // namespace wa

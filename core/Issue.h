// 校验 / 读取过程中产生的问题。path 形如 steps[2].locator.strategy。
#pragma once

#include <cstddef>
#include <string>
#include <vector>

namespace winauto {

enum class Severity { Error, Warning };

struct Issue {
    Severity severity = Severity::Error;
    std::string code;     // 稳定的机器码，例如 E_DUPLICATE_STEP_ID / W_UIA_NAME_ONLY
    std::string path;     // JSON 路径，空表示整个文档
    std::string message;  // 英文说明；GUI 层后续按 code 映射为中文
    bool requiresConfirmation = false;  // 高风险操作：运行前需用户确认
};

struct ValidationReport {
    std::vector<Issue> issues;

    bool hasErrors() const {
        for (const auto& i : issues)
            if (i.severity == Severity::Error) return true;
        return false;
    }
    std::size_t errorCount() const {
        std::size_t n = 0;
        for (const auto& i : issues)
            if (i.severity == Severity::Error) ++n;
        return n;
    }
    std::size_t warningCount() const { return issues.size() - errorCount(); }
    bool needsConfirmation() const {
        for (const auto& i : issues)
            if (i.requiresConfirmation) return true;
        return false;
    }
    bool hasCode(const std::string& code) const {
        for (const auto& i : issues)
            if (i.code == code) return true;
        return false;
    }
    const Issue* find(const std::string& code) const {
        for (const auto& i : issues)
            if (i.code == code) return &i;
        return nullptr;
    }
};

// 每个问题一行："error E_CODE steps[1].id: message"
std::string formatReport(const ValidationReport& report);

}  // namespace winauto

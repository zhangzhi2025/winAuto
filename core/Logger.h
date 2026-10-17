#pragma once
// 执行日志与脱敏。凡是进入日志或报告的文本，都先经过 scrub()：
// 已登记的敏感字符串（sensitive 文本、运行时询问的内容）会被替换为 ***。
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace wa {

enum class LogLevel { Debug, Info, Warn, Error };
const char* toString(LogLevel l);

struct LogEntry {
    std::int64_t tMs = 0;  // 相对本次运行开始
    LogLevel level = LogLevel::Info;
    std::string stepId;  // 空表示流程级日志
    std::string message;
};

class ExecutionLogger {
public:
    void addSecret(const std::string& secret);
    std::string scrub(std::string text) const;

    // message 会被自动脱敏
    void log(LogLevel level, std::int64_t tMs, const std::string& stepId,
             const std::string& message);

    const std::vector<LogEntry>& entries() const { return entries_; }
    void setSink(std::function<void(const LogEntry&)> sink) { sink_ = std::move(sink); }

private:
    std::vector<std::string> secrets_;  // 按长度降序，避免短串先替换导致残留
    std::vector<LogEntry> entries_;
    std::function<void(const LogEntry&)> sink_;
};

}  // namespace wa

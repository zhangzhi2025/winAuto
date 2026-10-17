#include "core/Logger.h"

#include <algorithm>

namespace wa {

const char* toString(LogLevel l) {
    switch (l) {
        case LogLevel::Debug: return "DEBUG";
        case LogLevel::Info: return "INFO";
        case LogLevel::Warn: return "WARN";
        case LogLevel::Error: return "ERROR";
    }
    return "INFO";
}

void ExecutionLogger::addSecret(const std::string& secret) {
    if (secret.empty()) return;
    if (std::find(secrets_.begin(), secrets_.end(), secret) != secrets_.end()) return;
    secrets_.push_back(secret);
    std::sort(secrets_.begin(), secrets_.end(),
              [](const std::string& a, const std::string& b) { return a.size() > b.size(); });
}

std::string ExecutionLogger::scrub(std::string text) const {
    for (const auto& secret : secrets_) {
        size_t pos = 0;
        while ((pos = text.find(secret, pos)) != std::string::npos) {
            text.replace(pos, secret.size(), "***");
            pos += 3;
        }
    }
    return text;
}

void ExecutionLogger::log(LogLevel level, std::int64_t tMs, const std::string& stepId,
                          const std::string& message) {
    LogEntry e;
    e.tMs = tMs;
    e.level = level;
    e.stepId = stepId;
    e.message = scrub(message);
    entries_.push_back(e);
    if (sink_) sink_(entries_.back());
}

}  // namespace wa

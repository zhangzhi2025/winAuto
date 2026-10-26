#include "core/WindowMatcher.h"

#include <algorithm>
#include <cctype>

namespace wa {

namespace {
std::string asciiLower(std::string s) {
    for (char& c : s) {
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    }
    return s;
}
constexpr size_t kMaxListed = 5;
}  // namespace

std::string normalizeProcessName(const std::string& name) {
    std::string n = asciiLower(name);
    if (n.size() > 4 && n.compare(n.size() - 4, 4, ".exe") == 0) n.resize(n.size() - 4);
    return n;
}

std::vector<WindowInfo> matchWindows(const std::vector<WindowInfo>& all, const WindowSpec& spec,
                                     const MatchOptions& options) {
    std::vector<WindowInfo> hits;
    if (spec.processName.empty() && spec.titleContains.empty() && !spec.processId) return hits;

    const std::string wantProc = normalizeProcessName(spec.processName);
    const std::string wantTitle = asciiLower(spec.titleContains);

    for (const WindowInfo& w : all) {
        if (options.requireVisible && !w.visible) continue;
        if (options.excludePid != 0 && w.processId == options.excludePid) continue;
        if (spec.processId && w.processId != *spec.processId) continue;
        if (!wantProc.empty() && normalizeProcessName(w.processName) != wantProc) continue;
        if (!wantTitle.empty() && asciiLower(w.title).find(wantTitle) == std::string::npos) continue;
        if (spec.width && w.bounds.w != *spec.width) continue;
        if (spec.height && w.bounds.h != *spec.height) continue;
        hits.push_back(w);
    }
    return hits;
}

Result<WindowInfo> pickUniqueWindow(const std::vector<WindowInfo>& all, const WindowSpec& spec,
                                    const MatchOptions& options) {
    const std::vector<WindowInfo> hits = matchWindows(all, spec, options);
    if (hits.empty()) {
        std::string what = spec.processName.empty() ? "" : "process \"" + spec.processName + "\"";
        if (!spec.titleContains.empty()) {
            what += (what.empty() ? "" : " ") + std::string("title containing \"") + spec.titleContains + "\"";
        }
        if (spec.processId) {
            what += (what.empty() ? "" : " ") + std::string("pid ") + std::to_string(*spec.processId);
        }
        return Result<WindowInfo>::failure(ErrCode::WindowNotFound, "no window matches " + what);
    }
    if (hits.size() > 1) {
        std::string d = "window match is not unique: " + std::to_string(hits.size()) + " candidates (";
        for (size_t i = 0; i < hits.size() && i < kMaxListed; ++i) {
            if (i) d += ", ";
            d += "pid " + std::to_string(hits[i].processId) + " \"" + hits[i].title + "\"";
        }
        if (hits.size() > kMaxListed) d += ", ...";
        d += "); add titleContains, processId, or another condition to narrow it down";
        return Result<WindowInfo>::failure(ErrCode::WindowNotFound, d);
    }
    return Result<WindowInfo>::success(hits.front());
}

}  // namespace wa

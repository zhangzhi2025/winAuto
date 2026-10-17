#include "core/UiaMatch.h"

#include <algorithm>
#include <sstream>

#include "core/ControlType.h"

namespace wa {

namespace {

constexpr size_t kListed = 5;

std::string lowerAscii(std::string s) {
    for (char& c : s) {
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    }
    return s;
}

bool containsIgnoreCase(const std::string& hay, const std::string& needle) {
    return lowerAscii(hay).find(lowerAscii(needle)) != std::string::npos;
}

bool typeMatches(const UiaNode& n, const UiaLocator& l) {
    if (l.controlType.empty()) return true;
    const std::string canon = canonicalControlType(l.controlType);
    return !canon.empty() && n.controlType == canon;
}

bool idMatches(const UiaNode& n, const UiaLocator& l) {
    return l.automationId.empty() || n.automationId == l.automationId;
}

bool nameMatches(const UiaNode& n, const UiaLocator& l) {
    if (l.name.empty()) return true;
    return l.match == Match::Exact ? n.name == l.name : containsIgnoreCase(n.name, l.name);
}

bool hasCondition(const UiaLocator& l) {
    return !l.automationId.empty() || !l.name.empty() || !l.controlType.empty();
}

bool clickable(const UiaNode& n) { return !n.offscreen && n.bounds.w > 0 && n.bounds.h > 0; }

std::string quote(const std::string& s) { return "\"" + s + "\""; }

std::string describeNode(const UiaNode& n) {
    std::ostringstream os;
    os << "[" << (n.controlType.empty() ? "?" : n.controlType) << "]";
    if (!n.automationId.empty()) os << " id=" << quote(n.automationId);
    if (!n.name.empty()) os << " name=" << quote(n.name);
    os << " " << n.bounds.w << "x" << n.bounds.h << "@(" << n.bounds.x << "," << n.bounds.y << ")";
    if (n.offscreen) os << " offscreen";
    if (!n.enabled) os << " disabled";
    return os.str();
}

}  // namespace

bool nodeMatches(const UiaNode& node, const UiaLocator& locator) {
    if (!hasCondition(locator)) return false;
    return idMatches(node, locator) && typeMatches(node, locator) && nameMatches(node, locator);
}

std::string diagnoseNoMatch(const UiaSnapshot& s, const UiaLocator& l) {
    std::ostringstream os;
    if (s.nodes.empty()) {
        return "the window exposes no UI Automation elements (custom-drawn or not accessible); "
               "use a window_point locator instead";
    }
    os << "searched " << s.nodes.size() << " elements" << (s.truncated ? " (search was truncated)" : "");

    // 1) automationId 对，但其他条件不符
    if (!l.automationId.empty()) {
        std::vector<const UiaNode*> same;
        for (const auto& n : s.nodes) {
            if (n.automationId == l.automationId) same.push_back(&n);
        }
        if (!same.empty()) {
            os << "; automationId " << quote(l.automationId) << " exists but the other conditions differ: ";
            for (size_t i = 0; i < same.size() && i < kListed; ++i) os << (i ? "; " : "") << describeNode(*same[i]);
        } else {
            os << "; no element has automationId " << quote(l.automationId);
        }
    }

    // 2) 名字相近
    if (!l.name.empty()) {
        std::vector<const UiaNode*> similar;
        for (const auto& n : s.nodes) {
            if (!n.name.empty() && containsIgnoreCase(n.name, l.name)) similar.push_back(&n);
        }
        if (!similar.empty() && l.match == Match::Exact) {
            os << "; elements with a similar name (set match=contains?): ";
            for (size_t i = 0; i < similar.size() && i < kListed; ++i) os << (i ? "; " : "") << describeNode(*similar[i]);
        } else if (similar.empty()) {
            os << "; no element name contains " << quote(l.name);
        }
    }

    // 3) 该类型的控件有哪些
    const std::string canon = canonicalControlType(l.controlType);
    if (!l.controlType.empty() && canon.empty()) {
        os << "; unknown controlType " << quote(l.controlType);
    } else if (!canon.empty()) {
        size_t count = 0;
        std::string names;
        for (const auto& n : s.nodes) {
            if (n.controlType != canon) continue;
            if (count < kListed) names += (count ? ", " : "") + (n.name.empty() ? "(no name)" : quote(n.name));
            ++count;
        }
        os << "; " << count << " " << canon << " element(s)" << (count ? ": " + names : "");
    }
    return os.str();
}

Result<size_t> pickNode(const UiaSnapshot& s, const UiaLocator& l) {
    using R = Result<size_t>;
    std::vector<size_t> all, visible;
    for (size_t i = 0; i < s.nodes.size(); ++i) {
        if (!nodeMatches(s.nodes[i], l)) continue;
        all.push_back(i);
        if (clickable(s.nodes[i])) visible.push_back(i);
    }

    if (visible.empty()) {
        if (!all.empty()) {
            std::string d = std::to_string(all.size()) +
                            " matching element(s) found, but none is on screen (offscreen or zero size): ";
            for (size_t i = 0; i < all.size() && i < kListed; ++i) d += (i ? "; " : "") + describeNode(s.nodes[all[i]]);
            return R::failure(ErrCode::ElementNotFound, d);
        }
        return R::failure(ErrCode::ElementNotFound, diagnoseNoMatch(s, l));
    }
    if (visible.size() == 1 || l.ambiguity == Ambiguity::First) return R::success(visible.front());

    std::string d = std::to_string(visible.size()) +
                    " elements match; refusing to guess (add automationId or controlType, or set ambiguity=first): ";
    for (size_t i = 0; i < visible.size() && i < kListed; ++i) d += (i ? "; " : "") + describeNode(s.nodes[visible[i]]);
    if (visible.size() > kListed) d += "; ...";
    return R::failure(ErrCode::ElementAmbiguous, d);
}

std::string dumpSnapshot(const UiaSnapshot& s, size_t maxLines) {
    std::ostringstream os;
    size_t lines = 0;
    for (const auto& n : s.nodes) {
        if (lines >= maxLines) {
            os << "... (" << (s.nodes.size() - lines) << " more elements not shown)\n";
            return os.str();
        }
        os << std::string(static_cast<size_t>(std::max(0, n.depth)) * 2, ' ') << describeNode(n);
        if (n.supportsInvoke) os << " invoke";
        if (n.supportsValue) os << " value";
        os << "\n";
        ++lines;
    }
    if (s.truncated) os << "... (tree truncated by the element limit)\n";
    return os.str();
}

Result<LocatorSuggestion> suggestLocator(const UiaSnapshot& s, size_t index) {
    using R = Result<LocatorSuggestion>;
    if (index >= s.nodes.size()) return R::failure(ErrCode::Internal, "node index out of range");
    const UiaNode& n = s.nodes[index];

    auto countMatches = [&](const UiaLocator& l) {
        size_t c = 0;
        for (const auto& other : s.nodes) c += nodeMatches(other, l) ? 1 : 0;
        return c;
    };

    LocatorSuggestion out;
    if (!n.automationId.empty()) {
        UiaLocator l;
        l.automationId = n.automationId;
        l.controlType = n.controlType;
        const size_t c = countMatches(l);
        if (c == 1) {
            out.locator = l;
            return R::success(out);
        }
        out.warnings.push_back("automationId " + quote(n.automationId) + " is shared by " + std::to_string(c) +
                               " elements, so it cannot identify this one");
    }
    if (!n.name.empty()) {
        UiaLocator l;
        l.name = n.name;
        l.controlType = n.controlType;
        if (countMatches(l) == 1) {
            out.locator = l;
            out.warnings.push_back("locating by name is fragile: names change with the system language and app version");
            return R::success(out);
        }
        out.warnings.push_back("name " + quote(n.name) + " is not unique among " + n.controlType + " elements");
    }
    std::string why;
    for (const auto& w : out.warnings) why += (why.empty() ? "" : "; ") + w;
    return R::failure(ErrCode::ElementAmbiguous,
                      "no unique locator for this element" + (why.empty() ? "" : " (" + why + ")") +
                          "; use a window_point locator instead");
}

}  // namespace wa

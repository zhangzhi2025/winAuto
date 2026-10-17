#include "core/Validator.h"

#include "core/ControlType.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <set>

namespace wa {

namespace {

constexpr int kMaxRetryTimes = 10;
constexpr int kMaxCoord = 32767;

std::string idx(const std::string& p, size_t i) { return p + "[" + std::to_string(i) + "]"; }
std::string sub(const std::string& p, const std::string& k) { return p.empty() ? k : p + "." + k; }

class Checker {
public:
    explicit Checker(ValidationReport& r) : r_(r) {}

    void error(const std::string& path, const std::string& code, const std::string& msg) {
        r_.issues.push_back(Issue{Severity::Error, path, code, msg, false});
    }
    void warn(const std::string& path, const std::string& code, const std::string& msg,
              bool confirm = false) {
        r_.issues.push_back(Issue{Severity::Warning, path, code, msg, confirm});
    }

    void range(int v, int lo, int hi, const std::string& path, const std::string& what) {
        if (v < lo || v > hi) {
            error(path, "out_of_range",
                  what + " must be between " + std::to_string(lo) + " and " + std::to_string(hi));
        }
    }

    void checkRetry(const RetryPolicy& rp, const std::string& path) {
        range(rp.times, 0, kMaxRetryTimes, sub(path, "times"), "retry.times");
        range(rp.intervalMs, 0, kMaxTimeoutMs, sub(path, "intervalMs"), "retry.intervalMs");
    }

    void checkLeaf(const LeafLocator& loc, const std::string& path) {
        if (const auto* u = std::get_if<UiaLocator>(&loc)) {
            if (u->automationId.empty() && u->name.empty() && u->controlType.empty()) {
                error(path, "uia_no_condition",
                      "uia locator needs at least one of automationId, name, controlType");
            } else if (!u->controlType.empty() && canonicalControlType(u->controlType).empty()) {
                error(sub(path, "controlType"), "unknown_control_type",
                      "unknown controlType \"" + u->controlType + "\"; valid values are UI Automation control "
                      "type names such as Button, Edit, CheckBox, ComboBox, MenuItem");
            } else if (u->automationId.empty()) {
                warn(path, "uia_no_automation_id",
                     "locating without automationId is fragile (names change with system language "
                     "and app version)");
            }
            if (u->ambiguity == Ambiguity::First) {
                warn(path, "ambiguity_first",
                     "ambiguity=first picks an arbitrary match when several controls match");
            }
        } else if (const auto* w = std::get_if<WindowPointLocator>(&loc)) {
            if (!std::isfinite(w->x) || !std::isfinite(w->y)) {
                error(path, "out_of_range", "window_point coordinates must be finite numbers");
            } else if (w->anchor == Anchor::Ratio) {
                if (w->x < 0 || w->x > 1 || w->y < 0 || w->y > 1) {
                    error(path, "out_of_range", "ratio coordinates must be between 0 and 1");
                }
            } else {
                if (w->x < 0 || w->y < 0 || w->x > kMaxCoord || w->y > kMaxCoord) {
                    error(path, "out_of_range", "window_point coordinates must be 0..32767");
                } else if (w->x != std::floor(w->x) || w->y != std::floor(w->y)) {
                    error(path, "out_of_range",
                          "window_point coordinates must be whole pixels (use anchor=ratio for "
                          "fractions)");
                }
            }
        } else if (const auto* s = std::get_if<ScreenPointLocator>(&loc)) {
            if (s->x < -kMaxCoord - 1 || s->x > kMaxCoord || s->y < -kMaxCoord - 1 ||
                s->y > kMaxCoord) {
                error(path, "out_of_range", "screen_point coordinates must be -32768..32767");
            }
        } else if (const auto* im = std::get_if<ImageLocator>(&loc)) {
            checkTemplatePath(im->templatePath, sub(path, "template"));
            if (!(im->minConfidence > 0.0 && im->minConfidence <= 1.0)) {
                error(sub(path, "minConfidence"), "out_of_range",
                      "minConfidence must be in (0, 1]");
            }
            if (im->searchRegion && (im->searchRegion->w <= 0 || im->searchRegion->h <= 0)) {
                error(sub(path, "searchRegion"), "out_of_range",
                      "searchRegion width and height must be positive");
            }
            warn(path, "unsupported_in_mvp",
                 "image locator is not available in the first milestone and will fail at run time");
        }
    }

    void checkTemplatePath(const std::string& p, const std::string& path) {
        if (p.empty()) {
            error(path, "missing_field", "template path is required");
            return;
        }
        const bool abs = p[0] == '/' || p[0] == '\\' || (p.size() > 1 && p[1] == ':');
        bool parent = false;
        size_t start = 0;
        while (start <= p.size()) {
            size_t end = p.find_first_of("/\\", start);
            if (end == std::string::npos) end = p.size();
            if (p.substr(start, end - start) == "..") parent = true;
            start = end + 1;
        }
        if (abs || parent) {
            error(path, "bad_template_path",
                  "template must be a relative path inside the workflow directory");
        }
    }

    void checkLocator(const Locator& loc, const std::string& path) {
        if (const auto* f = std::get_if<FallbackLocator>(&loc)) {
            if (f->items.empty()) {
                error(sub(path, "locators"), "empty_fallback", "fallback needs at least one locator");
            }
            for (size_t i = 0; i < f->items.size(); ++i) {
                checkLeaf(f->items[i], idx(sub(path, "locators"), i));
            }
            return;
        }
        std::visit(
            [&](auto&& v) {
                using T = std::decay_t<decltype(v)>;
                if constexpr (!std::is_same_v<T, FallbackLocator>) checkLeaf(LeafLocator{v}, path);
            },
            loc);
    }

    void checkHotkey(const Step& s, const std::string& path) {
        const std::string kp = sub(path, "keys");
        if (s.keys.empty()) {
            error(kp, "empty_keys", "hotkey needs at least one key");
            return;
        }
        std::set<std::string> norm;
        bool hasNonModifier = false;
        bool bad = false;
        for (size_t i = 0; i < s.keys.size(); ++i) {
            if (!isSupportedKey(s.keys[i])) {
                error(idx(kp, i), "bad_key", "unsupported key name \"" + s.keys[i] + "\"");
                bad = true;
                continue;
            }
            const std::string n = normalizeKey(s.keys[i]);
            norm.insert(n);
            if (!isModifierKey(n)) hasNonModifier = true;
        }
        if (bad) return;
        if (!hasNonModifier) {
            error(kp, "bad_key", "hotkey needs at least one non-modifier key");
            return;
        }
        static const std::vector<std::set<std::string>> risky = {
            {"ALT", "F4"}, {"DELETE"}, {"SHIFT", "DELETE"}, {"WIN", "L"}};
        for (const auto& combo : risky) {
            if (norm == combo) {
                warn(kp, "high_risk_hotkey",
                     "this key combination can close windows, delete data or lock the session; "
                     "confirmation is required before running",
                     /*confirm=*/true);
            }
        }
    }

    void checkStep(const Workflow& wf, const Step& s, const std::string& path,
                   std::set<std::string>& seenIds) {
        if (s.id.empty()) {
            error(sub(path, "id"), "missing_field", "step id is required");
        } else if (!seenIds.insert(s.id).second) {
            error(sub(path, "id"), "duplicate_step_id", "duplicate step id \"" + s.id + "\"");
        }

        if (s.type == StepType::Unknown) {
            error(sub(path, "type"),
                  s.rawType.empty() ? "missing_field" : "unknown_step_type",
                  s.rawType.empty() ? "step type is required"
                                    : "unknown step type \"" + s.rawType + "\"");
            return;
        }

        if (requiresWindow(s.type)) {
            if (s.window.empty()) {
                error(sub(path, "window"), "missing_field", "window alias is required");
            } else if (wf.windows.find(s.window) == wf.windows.end()) {
                error(sub(path, "window"), "unknown_window",
                      "window alias \"" + s.window + "\" is not defined in windows");
            }
        }

        if (requiresLocator(s.type)) {
            if (!s.locator) {
                error(sub(path, "locator"), "locator_required",
                      std::string(toString(s.type)) + " requires a locator");
            } else {
                checkLocator(*s.locator, sub(path, "locator"));
            }
        }

        if (s.type == StepType::InputText || s.type == StepType::SetValue) {
            const bool hasText = s.text.has_value();
            if (s.type == StepType::InputText && s.method == InputMethod::Clipboard) {
                if (s.sensitive || s.textFromPrompt) {
                    error(sub(path, "method"), "sensitive_clipboard",
                          "sensitive or prompted text must not go through the clipboard "
                          "(clipboard history and sync can store it)");
                }
                warn(sub(path, "method"), "unsupported_in_mvp",
                     "clipboard input is not available yet and will fail at run time");
            }
            if (hasText && s.textFromPrompt) {
                error(path, "text_conflict", "text and textFrom=prompt cannot be used together");
            } else if (!hasText && !s.textFromPrompt) {
                error(sub(path, "text"), "text_required",
                      "provide text, or textFrom=\"prompt\" to ask at run time");
            } else if (hasText && s.text->empty()) {
                error(sub(path, "text"), "text_required", "text must not be empty");
            }
        }

        if (s.type == StepType::Hotkey) checkHotkey(s, path);

        if (s.type == StepType::Wait) {
            if (!s.durationMs) {
                error(sub(path, "durationMs"), "missing_field", "wait requires durationMs");
            } else {
                range(*s.durationMs, 0, kMaxTimeoutMs, sub(path, "durationMs"), "durationMs");
            }
        }

        if (s.timeoutMs) range(*s.timeoutMs, 1, kMaxTimeoutMs, sub(path, "timeoutMs"), "timeoutMs");
        if (s.retry) checkRetry(*s.retry, sub(path, "retry"));
        if (s.expect) {
            if (s.expect->type != "element_exists") {
                error(sub(sub(path, "expect"), "type"), "unknown_expect_type",
                      "unsupported expect type \"" + s.expect->type + "\"");
            }
            checkLocator(s.expect->locator, sub(sub(path, "expect"), "locator"));
        }
    }

private:
    ValidationReport& r_;
};

}  // namespace

// ------------------------------------------------------------------ 公开接口

bool ValidationReport::ok() const { return errorCount() == 0; }

size_t ValidationReport::errorCount() const {
    return static_cast<size_t>(std::count_if(issues.begin(), issues.end(), [](const Issue& i) {
        return i.severity == Severity::Error;
    }));
}

size_t ValidationReport::warningCount() const { return issues.size() - errorCount(); }

bool ValidationReport::needsConfirmation() const {
    return std::any_of(issues.begin(), issues.end(),
                       [](const Issue& i) { return i.requiresConfirmation; });
}

bool ValidationReport::has(const std::string& code) const { return find(code) != nullptr; }

const Issue* ValidationReport::find(const std::string& code, const std::string& path) const {
    for (const auto& i : issues) {
        if (i.code == code && (path.empty() || i.path == path)) return &i;
    }
    return nullptr;
}

std::string normalizeKey(const std::string& key) {
    std::string out = key;
    std::transform(out.begin(), out.end(), out.begin(),
                   [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
    if (out == "CONTROL") out = "CTRL";
    if (out == "ESCAPE") out = "ESC";
    if (out == "RETURN") out = "ENTER";
    if (out == "DEL") out = "DELETE";
    if (out == "WINDOWS") out = "WIN";
    return out;
}

bool isModifierKey(const std::string& key) {
    const std::string n = normalizeKey(key);
    return n == "CTRL" || n == "SHIFT" || n == "ALT" || n == "WIN";
}

bool isSupportedKey(const std::string& key) {
    const std::string n = normalizeKey(key);
    if (n.empty()) return false;
    if (n.size() == 1) return std::isalnum(static_cast<unsigned char>(n[0])) != 0;
    if (n[0] == 'F' && n.size() <= 3) {
        const std::string num = n.substr(1);
        if (!num.empty() && std::all_of(num.begin(), num.end(),
                                        [](unsigned char c) { return std::isdigit(c) != 0; })) {
            const int v = std::stoi(num);
            return v >= 1 && v <= 24;
        }
    }
    static const std::set<std::string> named = {
        "CTRL",  "SHIFT", "ALT",   "WIN",    "ENTER", "TAB",      "ESC",   "SPACE",
        "BACKSPACE", "DELETE", "INSERT", "HOME", "END", "PAGEUP", "PAGEDOWN",
        "UP",    "DOWN",  "LEFT",  "RIGHT"};
    return named.count(n) > 0;
}

ValidationReport validateWorkflow(const Workflow& wf) {
    ValidationReport report;
    Checker c(report);

    for (const auto& f : wf.unknownFields) {
        c.error(f, "unknown_field", "unknown field \"" + f + "\" (typo?)");
    }

    if (wf.schemaVersion.empty()) {
        c.error("schemaVersion", "missing_field", "schemaVersion is required");
    } else if (wf.schemaVersion != kSchemaVersion) {
        c.error("schemaVersion", "unsupported_version",
                "unsupported schemaVersion \"" + wf.schemaVersion + "\", expected \"" +
                    kSchemaVersion + "\"");
    }
    if (wf.id.empty()) c.error("id", "missing_field", "id is required");
    if (wf.name.empty()) c.error("name", "missing_field", "name is required");

    if (wf.windows.empty()) {
        c.error("windows", "missing_field", "at least one window must be defined");
    }
    for (const auto& [alias, spec] : wf.windows) {
        const std::string p = "windows." + alias;
        if (alias.empty()) c.error("windows", "bad_alias", "window alias must not be empty");
        if (spec.processName.empty() && spec.titleContains.empty()) {
            c.error(p, "missing_field",
                    "window needs processName or titleContains (refusing to match any window)");
        }
        c.range(spec.timeoutMs, 1, kMaxTimeoutMs, p + ".timeoutMs", "timeoutMs");
    }

    c.range(wf.execution.stepTimeoutMs, 1, kMaxTimeoutMs, "execution.stepTimeoutMs",
            "stepTimeoutMs");
    c.checkRetry(wf.execution.retry, "execution.retry");

    if (wf.steps.empty()) {
        c.error("steps", "empty_workflow", "workflow has no steps");
    }
    std::set<std::string> seenIds;
    for (size_t i = 0; i < wf.steps.size(); ++i) {
        c.checkStep(wf, wf.steps[i], idx("steps", i), seenIds);
    }
    return report;
}

}  // namespace wa

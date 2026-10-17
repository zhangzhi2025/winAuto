#include "core/Workflow.h"

namespace winauto {

namespace {
template <class E>
struct Entry {
    E value;
    const char* name;
};

template <class E, std::size_t N>
const char* nameOf(const Entry<E> (&table)[N], E v) {
    for (const auto& e : table)
        if (e.value == v) return e.name;
    return "unknown";
}

template <class E, std::size_t N>
bool lookup(const Entry<E> (&table)[N], std::string_view s, E* out) {
    for (const auto& e : table) {
        if (s == e.name) {
            *out = e.value;
            return true;
        }
    }
    return false;
}

const Entry<StepType> kStepTypes[] = {
    {StepType::ActivateWindow, "activate_window"}, {StepType::Click, "click"},
    {StepType::SetValue, "set_value"},             {StepType::InputText, "input_text"},
    {StepType::Hotkey, "hotkey"},                  {StepType::Wait, "wait"},
    {StepType::WaitForElement, "wait_for_element"}, {StepType::AssertElement, "assert_element"}};
const Entry<ClickMode> kClickModes[] = {
    {ClickMode::Auto, "auto"}, {ClickMode::Invoke, "invoke"}, {ClickMode::Mouse, "mouse"}};
const Entry<InputMethod> kInputMethods[] = {
    {InputMethod::Unicode, "unicode"}, {InputMethod::Clipboard, "clipboard"}, {InputMethod::ValuePattern, "value_pattern"}};
const Entry<FailurePolicy> kFailurePolicies[] = {{FailurePolicy::Stop, "stop"}, {FailurePolicy::Continue, "continue"}};
const Entry<Match> kMatches[] = {{Match::Exact, "exact"}, {Match::Contains, "contains"}};
const Entry<Ambiguity> kAmbiguities[] = {{Ambiguity::Error, "error"}, {Ambiguity::First, "first"}};
const Entry<PointAnchor> kAnchors[] = {
    {PointAnchor::TopLeft, "top_left"}, {PointAnchor::BottomRight, "bottom_right"}, {PointAnchor::Ratio, "ratio"}};
std::string num(double v) {
    if (v == static_cast<double>(static_cast<long long>(v))) return std::to_string(static_cast<long long>(v));
    std::string s = std::to_string(v);
    while (!s.empty() && s.back() == '0') s.pop_back();
    return s;
}
}  // namespace

const char* toString(StepType v) { return nameOf(kStepTypes, v); }
const char* toString(ClickMode v) { return nameOf(kClickModes, v); }
const char* toString(InputMethod v) { return nameOf(kInputMethods, v); }
const char* toString(FailurePolicy v) { return nameOf(kFailurePolicies, v); }
const char* toString(Match v) { return nameOf(kMatches, v); }
const char* toString(Ambiguity v) { return nameOf(kAmbiguities, v); }
const char* toString(PointAnchor v) { return nameOf(kAnchors, v); }

bool parseStepType(std::string_view s, StepType* out) { return lookup(kStepTypes, s, out); }
bool parseClickMode(std::string_view s, ClickMode* out) { return lookup(kClickModes, s, out); }
bool parseInputMethod(std::string_view s, InputMethod* out) { return lookup(kInputMethods, s, out); }
bool parseFailurePolicy(std::string_view s, FailurePolicy* out) { return lookup(kFailurePolicies, s, out); }
bool parseMatch(std::string_view s, Match* out) { return lookup(kMatches, s, out); }
bool parseAmbiguity(std::string_view s, Ambiguity* out) { return lookup(kAmbiguities, s, out); }
bool parsePointAnchor(std::string_view s, PointAnchor* out) { return lookup(kAnchors, s, out); }

// ------------------------------------------------------------------ Locator

const char* strategyName(const Locator& loc) {
    switch (loc.value.index()) {
    case 0: return "uia";
    case 1: return "window_point";
    case 2: return "screen_point";
    case 3: return "image";
    case 4: return "fallback";
    }
    return "unknown";
}

bool isFallback(const Locator& loc) { return std::holds_alternative<FallbackLocator>(loc.value); }

std::string describe(const Locator& loc) {
    std::string out = strategyName(loc);
    if (const auto* u = std::get_if<UiaLocator>(&loc.value)) {
        out += "(";
        bool first = true;
        auto add = [&](const char* k, const std::string& v) {
            if (v.empty()) return;
            if (!first) out += ", ";
            out += k;
            out += "=";
            out += v;
            first = false;
        };
        add("automationId", u->automationId);
        add("name", u->name);
        add("controlType", u->controlType);
        out += ")";
    } else if (const auto* w = std::get_if<WindowPointLocator>(&loc.value)) {
        out += "(" + num(w->x) + ", " + num(w->y) + ")";
    } else if (const auto* s = std::get_if<ScreenPointLocator>(&loc.value)) {
        out += "(" + std::to_string(s->x) + ", " + std::to_string(s->y) + ")";
    } else if (const auto* i = std::get_if<ImageLocator>(&loc.value)) {
        out += "(" + i->templatePath + ")";
    } else if (const auto* f = std::get_if<FallbackLocator>(&loc.value)) {
        out += "[";
        for (std::size_t k = 0; k < f->locators.size(); ++k) {
            if (k) out += " -> ";
            out += describe(f->locators[k]);
        }
        out += "]";
    }
    return out;
}

// ----------------------------------------------------------------- Workflow

const WindowSpec* Workflow::findWindow(const std::string& alias) const {
    for (const auto& w : windows)
        if (w.alias == alias) return &w.spec;
    return nullptr;
}

int Workflow::stepTimeoutMs(const Step& step) const {
    if (step.timeoutMs) return *step.timeoutMs;
    return execution.stepTimeoutMs.value_or(kDefaultStepTimeoutMs);
}

FailurePolicy Workflow::failurePolicy(const Step& step) const {
    if (step.onFailure) return *step.onFailure;
    return execution.onFailure.value_or(FailurePolicy::Stop);
}

bool stepUsesText(const Step& step) {
    return step.type == StepType::InputText || step.type == StepType::SetValue;
}

}  // namespace winauto

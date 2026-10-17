#include "core/Model.h"

namespace wa {

namespace {
template <class V>
const char* strategyOf(const V& v) {
    struct Visitor {
        const char* operator()(const UiaLocator&) const { return "uia"; }
        const char* operator()(const WindowPointLocator&) const { return "window_point"; }
        const char* operator()(const ScreenPointLocator&) const { return "screen_point"; }
        const char* operator()(const ImageLocator&) const { return "image"; }
        const char* operator()(const FallbackLocator&) const { return "fallback"; }
    };
    return std::visit(Visitor{}, v);
}
}  // namespace

const char* strategyName(const Locator& l) { return strategyOf(l); }
const char* strategyName(const LeafLocator& l) { return strategyOf(l); }

const char* toString(StepType t) {
    switch (t) {
        case StepType::ActivateWindow: return "activate_window";
        case StepType::Click: return "click";
        case StepType::SetValue: return "set_value";
        case StepType::InputText: return "input_text";
        case StepType::Hotkey: return "hotkey";
        case StepType::Wait: return "wait";
        case StepType::WaitForElement: return "wait_for_element";
        case StepType::AssertElement: return "assert_element";
        case StepType::Unknown: return "unknown";
    }
    return "unknown";
}

StepType parseStepType(const std::string& s) {
    static const StepType all[] = {StepType::ActivateWindow, StepType::Click,
                                   StepType::SetValue,       StepType::InputText,
                                   StepType::Hotkey,         StepType::Wait,
                                   StepType::WaitForElement, StepType::AssertElement};
    for (StepType t : all) {
        if (s == toString(t)) return t;
    }
    return StepType::Unknown;
}

bool requiresWindow(StepType t) { return t != StepType::Wait && t != StepType::Unknown; }

bool requiresLocator(StepType t) {
    return t == StepType::Click || t == StepType::SetValue || t == StepType::WaitForElement ||
           t == StepType::AssertElement;
}

}  // namespace wa

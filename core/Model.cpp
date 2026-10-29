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
        case StepType::MouseMove: return "mouse_move";
        case StepType::MouseClick: return "mouse_click";
        case StepType::SetValue: return "set_value";
        case StepType::InputText: return "input_text";
        case StepType::Hotkey: return "hotkey";
        case StepType::Wait: return "wait";
        case StepType::WaitForElement: return "wait_for_element";
        case StepType::AssertElement: return "assert_element";
        case StepType::Script: return "script";
        case StepType::LaunchProgram: return "launch_program";
        case StepType::If: return "if";
        case StepType::Repeat: return "repeat";
        case StepType::While: return "while";
        case StepType::Unknown: return "unknown";
    }
    return "unknown";
}

StepType parseStepType(const std::string& s) {
    static const StepType all[] = {StepType::ActivateWindow, StepType::Click,
                                   StepType::MouseMove,       StepType::MouseClick,
                                   StepType::SetValue,       StepType::InputText,
                                   StepType::Hotkey,         StepType::Wait,
                                   StepType::WaitForElement, StepType::AssertElement,
                                   StepType::Script,         StepType::LaunchProgram,
                                   StepType::If,
                                   StepType::Repeat,
                                   StepType::While};
    for (StepType t : all) {
        if (s == toString(t)) return t;
    }
    return StepType::Unknown;
}

bool isBlockType(StepType t) { return t == StepType::If || t == StepType::Repeat || t == StepType::While; }

const char* toString(ScriptLanguage language) {
    switch (language) {
        case ScriptLanguage::Python: return "python";
        case ScriptLanguage::Lua: return "lua";
    }
    return "unknown";
}

bool parseScriptLanguage(const std::string& s, ScriptLanguage* out) {
    if (s == "python") {
        if (out) *out = ScriptLanguage::Python;
        return true;
    }
    if (s == "lua") {
        if (out) *out = ScriptLanguage::Lua;
        return true;
    }
    return false;
}

// repeat 不操作任何窗口；if / while 的条件要在某个窗口里定位控件
bool requiresWindow(StepType t) {
    return t != StepType::Wait && t != StepType::Repeat && t != StepType::Unknown &&
           t != StepType::MouseMove && t != StepType::MouseClick && t != StepType::Script &&
           t != StepType::LaunchProgram;
}

bool requiresLocator(StepType t) {
    return t == StepType::Click || t == StepType::MouseMove || t == StepType::MouseClick ||
           t == StepType::SetValue || t == StepType::WaitForElement || t == StepType::AssertElement;
}

}  // namespace wa

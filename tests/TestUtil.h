#pragma once
#include <fstream>
#include <sstream>
#include <string>

#include "core/Json.h"
#include "core/Model.h"

namespace testutil {

inline std::string readFile(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

inline std::string exampleText(const std::string& name) {
    return readFile(std::string(WINAUTO_SOURCE_DIR) + "/examples/" + name);
}

inline wa::UiaLocator uia(const std::string& automationId, const std::string& controlType = "") {
    wa::UiaLocator u;
    u.automationId = automationId;
    u.controlType = controlType;
    return u;
}

inline wa::Step step(const std::string& id, wa::StepType type, const std::string& window = "main") {
    wa::Step s;
    s.id = id;
    s.type = type;
    s.rawType = wa::toString(type);
    s.window = window;
    return s;
}

inline wa::Step clickStep(const std::string& id, const std::string& automationId = "btn") {
    wa::Step s = step(id, wa::StepType::Click);
    s.locator = wa::Locator{uia(automationId, "Button")};
    return s;
}

inline wa::Step hotkeyStep(const std::string& id, std::vector<std::string> keys) {
    wa::Step s = step(id, wa::StepType::Hotkey);
    s.keys = std::move(keys);
    return s;
}

inline wa::Step typeStep(const std::string& id, const std::string& text, bool sensitive = false) {
    wa::Step s = step(id, wa::StepType::InputText);
    s.text = text;
    s.sensitive = sensitive;
    return s;
}

inline wa::Step waitStep(const std::string& id, int ms) {
    wa::Step s = step(id, wa::StepType::Wait, "");
    s.durationMs = ms;
    return s;
}

// 一个合法的最小流程：别名 main -> notepad.exe，一个 activate_window 步骤
inline wa::Workflow minimalWorkflow() {
    wa::Workflow wf;
    wf.schemaVersion = "1.0";
    wf.id = "WF-T";
    wf.name = "test";
    wa::WindowSpec w;
    w.processName = "notepad.exe";
    wf.windows["main"] = w;
    wf.steps.push_back(step("S1", wa::StepType::ActivateWindow));
    return wf;
}

}  // namespace testutil

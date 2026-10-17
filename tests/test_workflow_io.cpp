#include "core/Validator.h"
#include "core/WorkflowJson.h"
#include "tests/TestFramework.h"

using namespace winauto;

static const char* kFull = R"({
  "schemaVersion": "1.0",
  "id": "WF-1",
  "name": "全部字段",
  "windows": {
    "main": { "processName": "notepad.exe", "titleContains": "Untitled", "timeoutMs": 4000 },
    "other": { "processName": "calc.exe" }
  },
  "execution": { "stepTimeoutMs": 3000, "onFailure": "continue", "screenshotOnFailure": true },
  "steps": [
    { "id": "A", "type": "activate_window", "window": "main", "description": "d", "timeoutMs": 2000 },
    { "id": "B", "type": "click", "window": "main", "clickMode": "invoke",
      "locator": { "strategy": "uia", "automationId": "x", "name": "n", "controlType": "Button",
                   "match": "contains", "ambiguity": "error" },
      "expect": { "type": "element_exists", "locator": { "strategy": "screen_point", "x": -5, "y": 10 }, "timeoutMs": 900 },
      "retry": { "times": 2, "intervalMs": 100 }, "onFailure": "stop" },
    { "id": "C", "type": "click", "window": "other",
      "locator": { "strategy": "fallback", "locators": [
        { "strategy": "image", "template": "assets/a.png", "minConfidence": 0.9,
          "searchRegion": { "x": 1, "y": 2, "width": 30, "height": 40 } },
        { "strategy": "window_point", "x": 12, "y": 34 },
        { "strategy": "window_point", "x": 0.25, "y": 0.75, "anchor": "ratio" } ] } },
    { "id": "D", "type": "input_text", "window": "main", "text": "你好", "method": "clipboard", "sensitive": true },
    { "id": "E", "type": "set_value", "window": "main", "textFrom": "prompt",
      "locator": { "strategy": "uia", "automationId": "pw" } },
    { "id": "F", "type": "hotkey", "window": "main", "keys": ["CTRL", "SHIFT", "S"] },
    { "id": "G", "type": "wait", "durationMs": 250 },
    { "id": "H", "type": "wait_for_element", "window": "main", "timeoutMs": 800,
      "locator": { "strategy": "uia", "name": "Ready" } },
    { "id": "I", "type": "assert_element", "window": "main",
      "locator": { "strategy": "uia", "controlType": "Edit" } }
  ]
})";

static Workflow readOk(const char* text, ValidationReport* rep = nullptr) {
    Workflow wf;
    ValidationReport r = validateWorkflowText(text, &wf);
    if (rep) *rep = r;
    if (r.hasErrors()) tf::report(__FILE__, __LINE__, "unexpected errors:\n" + formatReport(r));
    return wf;
}

TEST(WorkflowIo, readsAllFields) {
    Workflow wf = readOk(kFull);
    CHECK_EQ(wf.id, std::string("WF-1"));
    CHECK_EQ(wf.windows.size(), static_cast<std::size_t>(2));
    CHECK_EQ(wf.steps.size(), static_cast<std::size_t>(9));
    REQUIRE(wf.steps.size() == 9);
    CHECK(wf.steps[1].clickMode == ClickMode::Invoke);
    const auto* u = std::get_if<UiaLocator>(&wf.steps[1].locator->value);
    REQUIRE(u != nullptr);
    CHECK_EQ(u->automationId, std::string("x"));
    CHECK(*u->match == Match::Contains);
    CHECK(wf.steps[4].textFromPrompt);
    CHECK(!wf.steps[4].text.has_value());
    CHECK_EQ(wf.steps[6].durationMs.value_or(-1), 250);
    CHECK(wf.execution.onFailure == FailurePolicy::Continue);
    CHECK(wf.steps[2].locator->value.index() == 4);  // fallback
}

TEST(WorkflowIo, writeThenReadIsLossless) {
    Workflow wf = readOk(kFull);
    JsonValue written = writeWorkflow(wf);
    JsonParseResult original = parseJson(kFull);
    REQUIRE(original.ok);
    CHECK(written == original.value);  // 规范化输出与手写文件语义一致

    Workflow again;
    ValidationReport r = validateWorkflowText(toJsonString(written), &again);
    CHECK(!r.hasErrors());
    CHECK(writeWorkflow(again) == written);
}

static std::string pathOf(const char* text, const char* code) {
    ValidationReport r = validateWorkflowText(text);
    const Issue* i = r.find(code);
    if (!i) {
        tf::report(__FILE__, __LINE__, std::string("expected issue ") + code + " in:\n" + formatReport(r));
        return "";
    }
    return i->path;
}

TEST(WorkflowIo, syntaxErrorHasLineAndColumn) {
    ValidationReport r = validateWorkflowText("{\n \"id\": }");
    CHECK(r.hasCode("E_JSON_SYNTAX"));
    CHECK(r.issues[0].message.find("line 2") != std::string::npos);
}

TEST(WorkflowIo, errorPathsPointAtTheField) {
    CHECK_EQ(pathOf(R"({"schemaVersion":"1.0","id":"a","name":"n","windows":{"m":{"processName":"p"}},
        "steps":[{"id":"s1","type":"wait","durationMs":1},
                 {"id":"s2","type":"click","window":"m","locator":{"strategy":"nope"}}]})",
                    "E_UNKNOWN_STRATEGY"),
             std::string("steps[1].locator.strategy"));

    CHECK_EQ(pathOf(R"({"schemaVersion":"1.0","id":"a","name":"n","windows":{"m":{"processName":"p"}},
        "steps":[{"id":"s1","type":"click","window":"m","locator":{"strategy":"uia","automationId":5}}]})",
                    "E_WRONG_TYPE"),
             std::string("steps[0].locator.automationId"));

    CHECK_EQ(pathOf(R"({"schemaVersion":"1.0","id":"a","name":"n","windows":{"m":{}},"steps":[]})", "E_MISSING_FIELD"),
             std::string("windows.m.processName"));

    CHECK_EQ(pathOf(R"({"schemaVersion":"1.0","id":"a","name":"n","windows":{},
        "steps":[{"id":"s1","type":"wait","durationMs":1,"bogus":1}]})",
                    "E_UNKNOWN_FIELD"),
             std::string("steps[0].bogus"));

    CHECK_EQ(pathOf(R"({"schemaVersion":"1.0","id":"a","name":"n","windows":{},
        "steps":[{"id":"s1","type":"wait","durationMs":1,"keys":["A"]}]})",
                    "E_FIELD_NOT_ALLOWED"),
             std::string("steps[0].keys"));

    CHECK_EQ(pathOf(R"({"schemaVersion":"1.0","id":"a","name":"n","windows":{},
        "steps":[{"id":"s1","type":"explode"}]})",
                    "E_UNKNOWN_STEP_TYPE"),
             std::string("steps[0].type"));

    CHECK_EQ(pathOf(R"({"schemaVersion":"1.0","id":"a","name":"n","windows":{"m":{"processName":"p"}},
        "execution":{"onFailure":"retry"},"steps":[]})",
                    "E_BAD_ENUM"),
             std::string("execution.onFailure"));

    CHECK_EQ(pathOf(R"({"schemaVersion":"1.0","id":"a","name":"n","windows":{"m":{"processName":"p"}},
        "steps":[{"id":"s1","type":"hotkey","window":"m","keys":["CTRL",5]}]})",
                    "E_WRONG_TYPE"),
             std::string("steps[0].keys[1]"));
}

TEST(WorkflowIo, rootMustBeObject) {
    ValidationReport r = validateWorkflowText("[1,2]");
    CHECK(r.hasCode("E_WRONG_TYPE"));
}

TEST(WorkflowIo, readFailureDoesNotFillOutput) {
    Workflow wf;
    wf.id = "untouched";
    ValidationReport r = validateWorkflowText(R"({"schemaVersion":"1.0"})", &wf);
    CHECK(r.hasErrors());
    CHECK_EQ(wf.id, std::string("untouched"));
}

TEST(WorkflowIo, missingFileIsReported) {
    ValidationReport r = loadWorkflowFile("/definitely/not/here.json");
    CHECK(r.hasCode("E_FILE_READ"));
}

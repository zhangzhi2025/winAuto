#include "TestUtil.h"
#include "core/Json.h"
#include "core/Validator.h"
#include "nlohmann/json.hpp"
#include "testing.h"

using namespace wa;
using nlohmann::json;

namespace {
Workflow mustRead(const std::string& text) {
    auto r = readWorkflow(text);
    if (!r.ok()) testing::reportFailure(__FILE__, __LINE__, "readWorkflow failed: " + r.detail);
    return r.value;
}
}  // namespace

TEST(reads_notepad_example) {
    Workflow wf = mustRead(testutil::exampleText("notepad-demo.json"));
    CHECK_EQ(wf.id, "WF-NOTEPAD-001");
    CHECK_EQ(wf.windows.size(), size_t(1));
    CHECK_EQ(wf.windows.at("main").processName, "notepad.exe");
    CHECK_EQ(wf.steps.size(), size_t(4));
    CHECK(wf.steps[1].type == StepType::Click);
    const auto* u = std::get_if<UiaLocator>(&*wf.steps[1].locator);
    CHECK(u != nullptr);
    if (u) {
        CHECK_EQ(u->automationId, "15");
        CHECK(u->ambiguity == Ambiguity::Error);
    }
    CHECK(wf.steps[2].expect.has_value());
    CHECK(wf.steps[2].retry.has_value());
    CHECK_EQ(wf.steps[3].keys.size(), size_t(2));
    CHECK(wf.unknownFields.empty());
}

TEST(examples_pass_the_validator) {
    for (const char* name : {"notepad-demo.json", "fallback-prompt-demo.json", "notepad-coordinates-demo.json"}) {
        Workflow wf = mustRead(testutil::exampleText(name));
        auto report = validateWorkflow(wf);
        CHECK(report.ok());
        if (!report.ok()) {
            for (const auto& i : report.issues) testing::reportFailure(name, 0, i.path + ": " + i.message);
        }
    }
}

TEST(read_write_roundtrip_matches_original_json) {
    for (const char* name : {"notepad-demo.json", "fallback-prompt-demo.json", "notepad-coordinates-demo.json"}) {
        const std::string text = testutil::exampleText(name);
        Workflow wf = mustRead(text);
        const std::string out = writeWorkflow(wf);
        // 用普通 json（对象键无序）比较，不受字段顺序影响
        CHECK(json::parse(out) == json::parse(text));
        // 二次读写稳定
        Workflow wf2 = mustRead(out);
        CHECK_EQ(writeWorkflow(wf2), out);
    }
}

TEST(fallback_and_prompt_example_structure) {
    Workflow wf = mustRead(testutil::exampleText("fallback-prompt-demo.json"));
    CHECK(wf.steps[2].textFromPrompt);
    CHECK(!wf.steps[2].text.has_value());
    CHECK(wf.steps[2].sensitive);
    const auto* fb = std::get_if<FallbackLocator>(&*wf.steps[3].locator);
    CHECK(fb != nullptr);
    if (fb) {
        CHECK_EQ(fb->items.size(), size_t(2));
        const auto* wp = std::get_if<WindowPointLocator>(&fb->items[1]);
        CHECK(wp != nullptr);
        if (wp) {
            CHECK(wp->anchor == Anchor::Ratio);
            CHECK(wp->x == 0.8);
        }
    }
}

TEST(invalid_json_syntax_is_rejected) {
    auto r = readWorkflow("{ \"id\": ");
    CHECK(!r.ok());
    CHECK(r.code == ErrCode::InvalidWorkflow);
    CHECK_CONTAINS(r.detail, "JSON syntax error");
}

TEST(root_must_be_object) {
    auto r = readWorkflow("[1,2,3]");
    CHECK(r.code == ErrCode::InvalidWorkflow);
    CHECK_CONTAINS(r.detail, "$");
}

TEST(type_errors_report_the_json_path) {
    auto r = readWorkflow(R"({"steps":[{"id":"A","type":"wait","durationMs":1},
                                       {"id":"B","type":"click","locator":"oops"}]})");
    CHECK(r.code == ErrCode::InvalidWorkflow);
    CHECK_CONTAINS(r.detail, "steps[1].locator");

    auto r2 = readWorkflow(R"({"steps":[{"id":"A","type":"wait","durationMs":"soon"}]})");
    CHECK_CONTAINS(r2.detail, "steps[0].durationMs");

    auto r3 = readWorkflow(R"({"windows":{"main":{"timeoutMs":"x"}}})");
    CHECK_CONTAINS(r3.detail, "windows.main.timeoutMs");
}

TEST(unknown_strategy_and_nested_fallback_are_read_errors) {
    auto r = readWorkflow(R"({"steps":[{"id":"A","type":"click","window":"m",
                              "locator":{"strategy":"ocr"}}]})");
    CHECK(r.code == ErrCode::InvalidWorkflow);
    CHECK_CONTAINS(r.detail, "steps[0].locator.strategy");

    auto n = readWorkflow(R"({"steps":[{"id":"A","type":"click","window":"m",
        "locator":{"strategy":"fallback","locators":[
           {"strategy":"fallback","locators":[]}]}}]})");
    CHECK(n.code == ErrCode::InvalidWorkflow);
    CHECK_CONTAINS(n.detail, "steps[0].locator.locators[0]");
    CHECK_CONTAINS(n.detail, "nested fallback");
}

TEST(missing_fields_are_read_but_left_to_the_validator) {
    auto r = readWorkflow("{}");
    CHECK(r.ok());
    auto report = validateWorkflow(r.value);
    CHECK(!report.ok());
    CHECK(report.has("missing_field"));
}

TEST(unknown_fields_are_recorded_with_paths) {
    Workflow wf = mustRead(R"({"schemaVersion":"1.0","id":"x","name":"x","bogus":1,
        "windows":{"m":{"processName":"a.exe","oops":true}},
        "steps":[{"id":"A","type":"wait","durationMs":1,"durationMS":2}]})");
    CHECK_EQ(wf.unknownFields.size(), size_t(3));
    auto report = validateWorkflow(wf);
    CHECK(report.has("unknown_field"));
    CHECK(report.find("unknown_field", "steps[0].durationMS") != nullptr);
}

TEST(integer_fields_reject_floats_and_overflow) {
    auto r = readWorkflow(R"({"execution":{"stepTimeoutMs":1.5}})");
    CHECK(r.code == ErrCode::InvalidWorkflow);
    auto r2 = readWorkflow(R"({"execution":{"stepTimeoutMs":99999999999}})");
    CHECK(r2.code == ErrCode::InvalidWorkflow);
}

TEST(unicode_text_survives_roundtrip) {
    Workflow wf = testutil::minimalWorkflow();
    wf.name = "中文 name ✓";
    wf.steps.push_back(testutil::typeStep("S2", "你好，\"世界\"\n\t\\"));
    wf.steps.back().window = "main";
    Workflow back = mustRead(writeWorkflow(wf));
    CHECK_EQ(back.name, wf.name);
    CHECK_EQ(*back.steps[1].text, "你好，\"世界\"\n\t\\");
}

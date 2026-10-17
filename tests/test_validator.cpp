#include "TestUtil.h"
#include "core/Validator.h"
#include "testing.h"

using namespace wa;
using namespace testutil;

namespace {
ValidationReport check(const Workflow& wf) { return validateWorkflow(wf); }

Workflow withStep(const Step& s) {
    Workflow wf = minimalWorkflow();
    wf.steps.push_back(s);
    return wf;
}
}  // namespace

TEST(minimal_workflow_is_valid) {
    auto r = check(minimalWorkflow());
    CHECK(r.ok());
    CHECK_EQ(r.issues.size(), size_t(0));
}

TEST(empty_workflow_is_rejected) {
    Workflow wf = minimalWorkflow();
    wf.steps.clear();
    auto r = check(wf);
    CHECK(!r.ok());
    CHECK(r.has("empty_workflow"));
}

TEST(missing_required_top_level_fields) {
    Workflow wf;  // 全空
    auto r = check(wf);
    CHECK(!r.ok());
    CHECK(r.find("missing_field", "schemaVersion") != nullptr);
    CHECK(r.find("missing_field", "id") != nullptr);
    CHECK(r.find("missing_field", "name") != nullptr);
    CHECK(r.find("missing_field", "windows") != nullptr);
    CHECK(r.has("empty_workflow"));
}

TEST(unsupported_schema_version) {
    Workflow wf = minimalWorkflow();
    wf.schemaVersion = "2.0";
    CHECK(check(wf).has("unsupported_version"));
}

TEST(duplicate_step_ids) {
    Workflow wf = minimalWorkflow();
    wf.steps.push_back(step("S1", StepType::ActivateWindow));
    auto r = check(wf);
    CHECK(!r.ok());
    CHECK(r.find("duplicate_step_id", "steps[1].id") != nullptr);
}

TEST(step_references_undefined_window_alias) {
    Step s = step("S2", StepType::ActivateWindow, "nope");
    auto r = check(withStep(s));
    CHECK(r.find("unknown_window", "steps[1].window") != nullptr);

    Step noAlias = step("S3", StepType::ActivateWindow, "");
    CHECK(check(withStep(noAlias)).find("missing_field", "steps[1].window") != nullptr);
}

TEST(wait_step_needs_no_window) {
    auto r = check(withStep(waitStep("W", 100)));
    CHECK(r.ok());
}

TEST(unknown_step_type) {
    Step s = step("S2", StepType::Unknown);
    s.rawType = "teleport";
    auto r = check(withStep(s));
    CHECK(!r.ok());
    CHECK(r.find("unknown_step_type", "steps[1].type") != nullptr);
}

TEST(window_without_any_match_condition_is_rejected) {
    Workflow wf = minimalWorkflow();
    wf.windows["main"].processName.clear();
    CHECK(check(wf).find("missing_field", "windows.main") != nullptr);
}

TEST(click_requires_locator) {
    Step s = step("S2", StepType::Click);
    CHECK(check(withStep(s)).find("locator_required", "steps[1].locator") != nullptr);
}

TEST(uia_locator_without_any_condition_is_rejected) {
    Step s = step("S2", StepType::Click);
    s.locator = Locator{UiaLocator{}};
    auto r = check(withStep(s));
    CHECK(!r.ok());
    CHECK(r.has("uia_no_condition"));
}

TEST(uia_locator_with_only_name_gives_a_warning_not_an_error) {
    Step s = step("S2", StepType::Click);
    UiaLocator u;
    u.name = "OK";
    s.locator = Locator{u};
    auto r = check(withStep(s));
    CHECK(r.ok());
    CHECK(r.has("uia_no_automation_id"));
    CHECK_EQ(r.warningCount(), size_t(1));
}

TEST(uia_ambiguity_first_warns) {
    Step s = clickStep("S2");
    std::get<UiaLocator>(*s.locator).ambiguity = Ambiguity::First;
    auto r = check(withStep(s));
    CHECK(r.ok());
    CHECK(r.has("ambiguity_first"));
}

TEST(fallback_must_not_be_empty_and_children_are_checked) {
    Step s = step("S2", StepType::Click);
    s.locator = Locator{FallbackLocator{}};
    CHECK(check(withStep(s)).has("empty_fallback"));

    FallbackLocator fb;
    fb.items.push_back(UiaLocator{});  // 无条件的 uia
    s.locator = Locator{fb};
    auto r = check(withStep(s));
    CHECK(r.find("uia_no_condition", "steps[1].locator.locators[0]") != nullptr);
}

TEST(nested_fallback_is_not_representable_in_the_model) {
    // LeafLocator 不含 FallbackLocator，嵌套在类型层面就不可能；读取器会在 JSON 层拒绝（见 test_json）。
    static_assert(!std::is_constructible_v<LeafLocator, FallbackLocator>,
                  "fallback must not be nestable");
    CHECK(true);
}

TEST(hotkey_validation) {
    CHECK(check(withStep(hotkeyStep("H", {}))).has("empty_keys"));
    CHECK(check(withStep(hotkeyStep("H", {"CTRL", "BANANA"}))).has("bad_key"));
    CHECK(check(withStep(hotkeyStep("H", {"CTRL", "SHIFT"}))).has("bad_key"));  // 只有修饰键
    CHECK(check(withStep(hotkeyStep("H", {"ctrl", "s"}))).ok());                  // 大小写不敏感
    CHECK(check(withStep(hotkeyStep("H", {"CTRL", "F12"}))).ok());
    CHECK(!check(withStep(hotkeyStep("H", {"CTRL", "F25"}))).ok());
}

TEST(high_risk_hotkeys_warn_and_require_confirmation) {
    for (auto keys : std::vector<std::vector<std::string>>{
             {"ALT", "F4"}, {"alt", "f4"}, {"DELETE"}, {"SHIFT", "DELETE"}, {"WIN", "L"}}) {
        auto r = check(withStep(hotkeyStep("H", keys)));
        CHECK(r.ok());
        CHECK(r.has("high_risk_hotkey"));
        CHECK(r.needsConfirmation());
    }
    CHECK(!check(withStep(hotkeyStep("H", {"CTRL", "S"}))).needsConfirmation());
}

TEST(input_text_text_rules) {
    Step none = step("T", StepType::InputText);
    CHECK(check(withStep(none)).has("text_required"));

    Step both = typeStep("T", "abc");
    both.textFromPrompt = true;
    CHECK(check(withStep(both)).has("text_conflict"));

    Step prompt = step("T", StepType::InputText);
    prompt.textFromPrompt = true;
    CHECK(check(withStep(prompt)).ok());

    CHECK(check(withStep(typeStep("T", ""))).has("text_required"));
}

TEST(set_value_needs_locator_and_text) {
    Step s = step("V", StepType::SetValue);
    auto r = check(withStep(s));
    CHECK(r.has("locator_required"));
    CHECK(r.has("text_required"));
}

TEST(wait_requires_duration_within_range) {
    Step w = step("W", StepType::Wait, "");
    CHECK(check(withStep(w)).has("missing_field"));
    CHECK(!check(withStep(waitStep("W", -1))).ok());
    CHECK(!check(withStep(waitStep("W", 60001))).ok());
    CHECK(check(withStep(waitStep("W", 0))).ok());
    CHECK(check(withStep(waitStep("W", 60000))).ok());
}

TEST(timeouts_and_retry_ranges) {
    Step s = clickStep("C");
    s.timeoutMs = 0;
    CHECK(check(withStep(s)).has("out_of_range"));
    s.timeoutMs = 60001;
    CHECK(check(withStep(s)).has("out_of_range"));
    s.timeoutMs = 3000;
    s.retry = RetryPolicy{11, 500};
    CHECK(check(withStep(s)).has("out_of_range"));
    s.retry = RetryPolicy{2, -5};
    CHECK(check(withStep(s)).has("out_of_range"));
    s.retry = RetryPolicy{2, 500};
    CHECK(check(withStep(s)).ok());

    Workflow wf = minimalWorkflow();
    wf.execution.stepTimeoutMs = 0;
    CHECK(check(wf).find("out_of_range", "execution.stepTimeoutMs") != nullptr);
    wf = minimalWorkflow();
    wf.windows["main"].timeoutMs = 99999;
    CHECK(check(wf).find("out_of_range", "windows.main.timeoutMs") != nullptr);
}

TEST(window_point_coordinate_ranges) {
    Step s = step("C", StepType::Click);
    s.locator = Locator{WindowPointLocator{-1, 10, Anchor::TopLeft}};
    CHECK(check(withStep(s)).has("out_of_range"));
    s.locator = Locator{WindowPointLocator{40000, 10, Anchor::TopLeft}};
    CHECK(check(withStep(s)).has("out_of_range"));
    s.locator = Locator{WindowPointLocator{10.5, 10, Anchor::TopLeft}};
    CHECK(check(withStep(s)).has("out_of_range"));
    s.locator = Locator{WindowPointLocator{420, 680, Anchor::TopLeft}};
    CHECK(check(withStep(s)).ok());
    s.locator = Locator{WindowPointLocator{1.2, 0.5, Anchor::Ratio}};
    CHECK(check(withStep(s)).has("out_of_range"));
    s.locator = Locator{WindowPointLocator{0.8, 0.5, Anchor::Ratio}};
    CHECK(check(withStep(s)).ok());
}

TEST(screen_point_allows_negative_multi_monitor_coordinates) {
    Step s = step("C", StepType::Click);
    s.locator = Locator{ScreenPointLocator{-1920, 300}};
    CHECK(check(withStep(s)).ok());
    s.locator = Locator{ScreenPointLocator{-40000, 300}};
    CHECK(check(withStep(s)).has("out_of_range"));
}

TEST(image_locator_rules) {
    Step s = step("C", StepType::Click);
    ImageLocator im;
    im.templatePath = "assets/send.png";
    s.locator = Locator{im};
    auto r = check(withStep(s));
    CHECK(r.ok());
    CHECK(r.has("unsupported_in_mvp"));

    for (const char* bad : {"../secret.png", "/etc/passwd", "C:\\x.png", "a/../../b.png", "..\\x.png"}) {
        im.templatePath = bad;
        s.locator = Locator{im};
        CHECK(check(withStep(s)).has("bad_template_path"));
    }
    im.templatePath = "";
    s.locator = Locator{im};
    CHECK(check(withStep(s)).has("missing_field"));

    im.templatePath = "a.png";
    im.minConfidence = 0;
    s.locator = Locator{im};
    CHECK(check(withStep(s)).has("out_of_range"));
    im.minConfidence = 1.5;
    s.locator = Locator{im};
    CHECK(check(withStep(s)).has("out_of_range"));
}

TEST(expect_locator_is_validated) {
    Step s = typeStep("T", "x");
    Expect e;
    e.locator = Locator{UiaLocator{}};
    s.expect = e;
    auto r = check(withStep(s));
    CHECK(r.find("uia_no_condition", "steps[1].expect.locator") != nullptr);

    e.type = "pixel_color";
    e.locator = Locator{uia("a")};
    s.expect = e;
    CHECK(check(withStep(s)).has("unknown_expect_type"));
}

TEST(report_helpers_count_errors_and_warnings) {
    Step s = step("S2", StepType::Click);
    UiaLocator u;
    u.name = "x";
    s.locator = Locator{u};           // warning
    Step bad = step("S3", StepType::Hotkey);  // error: empty keys
    Workflow wf = minimalWorkflow();
    wf.steps.push_back(s);
    wf.steps.push_back(bad);
    auto r = check(wf);
    CHECK_EQ(r.errorCount(), size_t(1));
    CHECK_EQ(r.warningCount(), size_t(1));
    CHECK(!r.ok());
}

TEST(key_helpers) {
    CHECK_EQ(normalizeKey("ctrl"), "CTRL");
    CHECK_EQ(normalizeKey("Control"), "CTRL");
    CHECK_EQ(normalizeKey("esc"), "ESC");
    CHECK(isSupportedKey("a"));
    CHECK(isSupportedKey("9"));
    CHECK(isSupportedKey("PageDown"));
    CHECK(!isSupportedKey(""));
    CHECK(!isSupportedKey("!"));
    CHECK(!isSupportedKey("F0"));
    CHECK(isModifierKey("win"));
    CHECK(!isModifierKey("s"));
}

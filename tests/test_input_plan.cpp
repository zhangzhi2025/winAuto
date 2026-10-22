#include <cmath>
#include <cstdint>
#include <string>

#include "TestUtil.h"
#include "core/Engine.h"
#include "core/InputPlan.h"
#include "core/Summary.h"
#include "core/Validator.h"
#include "fakes/Fakes.h"
#include "testing.h"

using namespace wa;
using namespace wa::fakes;
using namespace testutil;

namespace {
// Unicode 事件取其 UTF-16 代码单元；Key 事件记为 0xFFFF（便于一眼看出哪里不是字符）
std::vector<std::uint16_t> units(const std::vector<TextEvent>& events) {
    std::vector<std::uint16_t> out;
    for (const auto& e : events) {
        out.push_back(e.kind == TextEvent::Kind::Unicode ? static_cast<std::uint16_t>(e.value) : 0xFFFF);
    }
    return out;
}
}  // namespace

// ------------------------------------------------------------------ 文本计划

TEST(ascii_text_becomes_one_unit_per_char) {
    auto r = buildTextEvents("Hi!");
    CHECK(r.ok());
    CHECK((units(r.value) == std::vector<std::uint16_t>{'H', 'i', '!'}));
}

TEST(empty_text_is_an_empty_plan) {
    auto r = buildTextEvents("");
    CHECK(r.ok());
    CHECK(r.value.empty());
}

TEST(chinese_text_is_bmp_units) {
    auto r = buildTextEvents("你好");
    CHECK(r.ok());
    CHECK((units(r.value) == std::vector<std::uint16_t>{0x4F60, 0x597D}));
}

TEST(astral_characters_become_surrogate_pairs) {
    auto r = buildTextEvents("\xF0\x9F\x98\x80");  // U+1F600
    CHECK(r.ok());
    CHECK((units(r.value) == std::vector<std::uint16_t>{0xD83D, 0xDE00}));
    auto r2 = buildTextEvents("\xF0\x90\x80\x80");  // U+10000，最小的辅助平面码点
    CHECK((units(r2.value) == std::vector<std::uint16_t>{0xD800, 0xDC00}));
    auto r3 = buildTextEvents("\xF4\x8F\xBF\xBF");  // U+10FFFF，最大码点
    CHECK((units(r3.value) == std::vector<std::uint16_t>{0xDBFF, 0xDFFF}));
}

TEST(newlines_and_tabs_become_key_presses) {
    auto r = buildTextEvents("a\r\nb\nc\rd\te");
    CHECK(r.ok());
    std::string shape;  // U = 字符, R = Enter 键, T = Tab 键
    for (const auto& e : r.value) {
        if (e.kind == TextEvent::Kind::Unicode) shape += 'U';
        else if (e.value == 0x0D) shape += 'R';
        else if (e.value == 0x09) shape += 'T';
        else shape += '?';
    }
    CHECK_EQ(shape, "URURURUTU");  // CRLF 只算一次回车
}

TEST(other_control_characters_are_rejected) {
    CHECK(!buildTextEvents(std::string("a\x1b" "b")).ok());   // ESC
    CHECK(!buildTextEvents(std::string("a\x7f")).ok());       // DEL
    CHECK(!buildTextEvents(std::string("a\0b", 3)).ok());     // NUL
    CHECK(!buildTextEvents("a\x08").ok());                    // BS
    auto r = buildTextEvents("a\x1b");
    CHECK_CONTAINS(r.detail, "U+001B");
    CHECK(r.code == ErrCode::InvalidWorkflow);
}

TEST(invalid_utf8_is_rejected) {
    CHECK(!buildTextEvents("\xC3").ok());                // 截断
    CHECK(!buildTextEvents("\xC3\x28").ok());            // 续字节错误
    CHECK(!buildTextEvents("\xC0\xAF").ok());            // 过长编码
    CHECK(!buildTextEvents("\xE0\x80\xAF").ok());        // 过长编码
    CHECK(!buildTextEvents("\xED\xA0\x80").ok());        // 代理区 U+D800
    CHECK(!buildTextEvents("\xF4\x90\x80\x80").ok());    // > U+10FFFF
    CHECK(!buildTextEvents("\xFF").ok());
    CHECK(!buildTextEvents("\x80").ok());                // 孤立续字节
    auto r = buildTextEvents("ab\xC3");
    CHECK_CONTAINS(r.detail, "byte offset 2");
}

// ------------------------------------------------------------------ 组合键计划

TEST(hotkey_plan_presses_modifiers_first_and_releases_in_reverse) {
    auto r = buildKeySequence({"s", "ctrl"});  // 给定顺序是主键在前，计划仍先按修饰键
    CHECK(r.ok());
    CHECK_EQ(r.value.size(), size_t(4));
    CHECK(r.value[0].down && r.value[0].vk == 0x11);  // CTRL 按下
    CHECK(r.value[1].down && r.value[1].vk == 0x53);  // S 按下
    CHECK(!r.value[2].down && r.value[2].vk == 0x53);  // S 抬起
    CHECK(!r.value[3].down && r.value[3].vk == 0x11);  // CTRL 抬起
}

TEST(hotkey_plan_with_multiple_modifiers) {
    auto r = buildKeySequence({"CTRL", "SHIFT", "ESC"});
    CHECK(r.ok());
    const std::vector<std::pair<unsigned, bool>> expected = {
        {0x11, true}, {0x10, true}, {0x1B, true}, {0x1B, false}, {0x10, false}, {0x11, false}};
    CHECK_EQ(r.value.size(), expected.size());
    for (size_t i = 0; i < expected.size() && i < r.value.size(); ++i) {
        CHECK_EQ(r.value[i].vk, expected[i].first);
        CHECK_EQ(r.value[i].down, expected[i].second);
    }
}

TEST(hotkey_plan_marks_extended_keys_and_dedupes) {
    auto r = buildKeySequence({"ctrl", "CTRL", "Delete", "delete"});
    CHECK(r.ok());
    CHECK_EQ(r.value.size(), size_t(4));  // 重复键只算一次
    CHECK(!r.value[0].extended);          // CTRL
    CHECK(r.value[1].extended);           // DELETE 是扩展键
    CHECK(r.value[2].extended);
}

TEST(hotkey_plan_balanced_presses_and_releases) {
    for (auto keys : std::vector<std::vector<std::string>>{{"A"}, {"WIN", "L"}, {"CTRL", "ALT", "DELETE"}, {"F5"}}) {
        auto r = buildKeySequence(keys);
        CHECK(r.ok());
        int down = 0, up = 0;
        for (const auto& k : r.value) (k.down ? down : up)++;
        CHECK_EQ(down, up);
    }
}

TEST(hotkey_plan_rejects_bad_keys) {
    CHECK(!buildKeySequence({"CTRL", "BANANA"}).ok());
    CHECK(!buildKeySequence({}).ok());
}

// ------------------------------------------------------------------ 鼠标坐标

TEST(absolute_coordinates_single_monitor) {
    Rect vs{0, 0, 1920, 1080};
    auto a = toAbsoluteMouse({0, 0}, vs);
    CHECK_EQ(a.x, 0);
    CHECK_EQ(a.y, 0);
    auto b = toAbsoluteMouse({1919, 1079}, vs);
    CHECK_EQ(b.x, 65535);
    CHECK_EQ(b.y, 65535);
    auto mid = toAbsoluteMouse({960, 540}, vs);
    CHECK(mid.x > 32000 && mid.x < 33500);
}

TEST(absolute_coordinates_with_negative_origin) {
    // 副屏在主屏左侧：虚拟桌面 x 从 -1920 开始
    Rect vs{-1920, 0, 3840, 1080};
    CHECK_EQ(toAbsoluteMouse({-1920, 0}, vs).x, 0);
    CHECK_EQ(toAbsoluteMouse({1919, 0}, vs).x, 65535);
    auto zero = toAbsoluteMouse({0, 0}, vs);  // 主屏左边缘，约在中间
    CHECK(zero.x > 32000 && zero.x < 33500);
}

TEST(absolute_coordinates_are_clamped_and_monotonic) {
    Rect vs{0, 0, 1000, 500};
    CHECK_EQ(toAbsoluteMouse({-50, -50}, vs).x, 0);
    CHECK_EQ(toAbsoluteMouse({5000, 5000}, vs).x, 65535);
    CHECK_EQ(toAbsoluteMouse({5000, 5000}, vs).y, 65535);
    int prev = -1;
    for (int x = 0; x < 1000; ++x) {
        int v = toAbsoluteMouse({x, 0}, vs).x;
        CHECK(v >= prev);
        prev = v;
    }
    CHECK_EQ(toAbsoluteMouse({3, 3}, Rect{0, 0, 1, 1}).x, 0);  // 退化桌面不除零
}

TEST(normalized_point_maps_back_to_the_same_pixel) {
    // Windows 把 0..65535 映射回像素的做法是 round(n * (size-1) / 65535)；往返应落在同一像素
    for (Rect vs : {Rect{0, 0, 1920, 1080}, Rect{-2560, -200, 5120, 1440}, Rect{0, 0, 3840, 2160}}) {
        for (int x = vs.x; x < vs.x + vs.w; x += 37) {
            const int n = toAbsoluteMouse({x, vs.y}, vs).x;
            const int back = vs.x + static_cast<int>(std::lround(static_cast<double>(n) * (vs.w - 1) / 65535.0));
            if (back != x) testing::reportFailure(__FILE__, __LINE__, "pixel " + std::to_string(x) + " -> " + std::to_string(back));
        }
    }
}

// ------------------------------------------------------------------ 窗口点解析

TEST(window_point_top_left) {
    Rect client{100, 200, 800, 600};
    auto r = resolveWindowPoint(client, {10, 20, Anchor::TopLeft});
    CHECK(r.ok());
    CHECK_EQ(r.value.x, 110);
    CHECK_EQ(r.value.y, 220);
    CHECK(resolveWindowPoint(client, {0, 0, Anchor::TopLeft}).ok());
    CHECK(resolveWindowPoint(client, {799, 599, Anchor::TopLeft}).ok());
}

TEST(window_point_outside_client_is_refused) {
    Rect client{100, 200, 800, 600};
    auto r = resolveWindowPoint(client, {800, 10, Anchor::TopLeft});
    CHECK(r.code == ErrCode::ElementNotFound);
    CHECK_CONTAINS(r.detail, "800x600");
    CHECK(resolveWindowPoint(client, {10, 600, Anchor::TopLeft}).code == ErrCode::ElementNotFound);
    CHECK(resolveWindowPoint(client, {-1, 10, Anchor::TopLeft}).code == ErrCode::ElementNotFound);
}

TEST(window_point_bottom_right_anchor) {
    Rect client{100, 200, 800, 600};
    auto r = resolveWindowPoint(client, {0, 0, Anchor::BottomRight});
    CHECK(r.ok());
    CHECK_EQ(r.value.x, 899);  // 最后一个像素
    CHECK_EQ(r.value.y, 799);
    auto r2 = resolveWindowPoint(client, {50, 30, Anchor::BottomRight});
    CHECK_EQ(r2.value.x, 849);
    CHECK_EQ(r2.value.y, 769);
    CHECK(!resolveWindowPoint(client, {800, 0, Anchor::BottomRight}).ok());
}

TEST(window_point_ratio_survives_resizing) {
    auto small = resolveWindowPoint(Rect{0, 0, 401, 201}, {0.5, 0.5, Anchor::Ratio});
    auto big = resolveWindowPoint(Rect{0, 0, 801, 401}, {0.5, 0.5, Anchor::Ratio});
    CHECK_EQ(small.value.x, 200);
    CHECK_EQ(big.value.x, 400);
    CHECK_EQ(resolveWindowPoint(Rect{50, 60, 100, 100}, {0, 0, Anchor::Ratio}).value.x, 50);
    CHECK_EQ(resolveWindowPoint(Rect{50, 60, 100, 100}, {1, 1, Anchor::Ratio}).value.x, 149);  // 最后一个像素
    CHECK_EQ(resolveWindowPoint(Rect{50, 60, 100, 100}, {1, 1, Anchor::Ratio}).value.y, 159);
}

TEST(empty_client_area_is_refused) {
    auto r = resolveWindowPoint(Rect{0, 0, 0, 0}, {0, 0, Anchor::TopLeft});
    CHECK(r.code == ErrCode::ElementNotFound);
    CHECK_CONTAINS(r.detail, "minimized");
}

// ------------------------------------------------------------------ 校验器：剪贴板

TEST(clipboard_input_rules) {
    Workflow wf = minimalWorkflow();
    Step t = typeStep("T", "hello");
    t.method = InputMethod::Clipboard;
    wf.steps.push_back(t);
    auto r = validateWorkflow(wf);
    CHECK(r.ok());
    CHECK(!r.has("unsupported_in_mvp"));  // 阶段 6 起剪贴板输入可用

    wf.steps.back().text = std::string(kMaxClipboardBytes + 1, 'a');
    CHECK(validateWorkflow(wf).has("text_too_long"));
    wf.steps.back().text = "hello";

    wf.steps.back().sensitive = true;
    CHECK(validateWorkflow(wf).has("sensitive_clipboard"));
    CHECK(!validateWorkflow(wf).ok());

    wf.steps.back().sensitive = false;
    wf.steps.back().text.reset();
    wf.steps.back().textFromPrompt = true;
    CHECK(validateWorkflow(wf).has("sensitive_clipboard"));
}

// ------------------------------------------------------------------ 信任摘要

TEST(summary_lists_targets_and_actions) {
    Workflow wf;
    {
        auto r = readWorkflow(exampleText("notepad-demo.json"));
        CHECK(r.ok());
        wf = r.value;
    }
    const std::string s = summarizeWorkflow(wf, validateWorkflow(wf));
    CHECK_CONTAINS(s, "记事本输入文本");
    CHECK_CONTAINS(s, "notepad.exe");
    CHECK_CONTAINS(s, "输入文本 \"Hello Windows Automation\"");
    CHECK_CONTAINS(s, "按键 CTRL+S");
    CHECK_CONTAINS(s, "uia(automationId=15, controlType=Edit)");
    CHECK_CONTAINS(s, "警告");  // 示例有一条 uia 没有 automationId 的警告
}

TEST(summary_never_reveals_sensitive_or_prompted_text) {
    Workflow wf = minimalWorkflow();
    wf.steps.push_back(typeStep("T1", "top-secret-literal", /*sensitive=*/true));
    Step p = step("T2", StepType::InputText);
    p.textFromPrompt = true;
    wf.steps.push_back(p);
    const std::string s = summarizeWorkflow(wf, validateWorkflow(wf));
    CHECK_NOT_CONTAINS(s, "top-secret-literal");
    CHECK_CONTAINS(s, "[已隐藏]");
    CHECK_CONTAINS(s, "[运行时询问");
}

TEST(summary_flags_high_risk_and_truncates_long_text_safely) {
    Workflow wf = minimalWorkflow();
    wf.steps.push_back(hotkeyStep("H", {"ALT", "F4"}));
    wf.steps.push_back(typeStep("T", std::string(60, 'x') + "尾"));
    wf.steps.push_back(typeStep("T2", "一二三四五六七八九十一二三四五六七八九十一二三四五六七八九十一二三四五六七八九十一二三"));
    const std::string s = summarizeWorkflow(wf, validateWorkflow(wf));
    CHECK_CONTAINS(s, "高风险操作");
    CHECK_CONTAINS(s, "按键 ALT+F4");
    CHECK_CONTAINS(s, "…");
    CHECK_NOT_CONTAINS(s, "尾");  // 被截断
    // 截断后仍是合法 UTF-8：每个多字节序列都完整
    for (size_t i = 0; i < s.size(); ++i) {
        const unsigned char c = static_cast<unsigned char>(s[i]);
        if (c >= 0xC0) {
            const size_t need = c >= 0xF0 ? 3 : c >= 0xE0 ? 2 : 1;
            for (size_t k = 1; k <= need; ++k) {
                CHECK(i + k < s.size() && (static_cast<unsigned char>(s[i + k]) & 0xC0) == 0x80);
            }
        }
    }
}

// ------------------------------------------------------------------ 引擎：输入安全规则

namespace {
struct Env {
    FakeClock clock;
    FakeWindowProvider windows;
    FakeElementLocator locator;
    FakeInputController input;
    CancelToken token;
    Env() { windows.windows.push_back(FakeWindowProvider::window(1, "notepad.exe", "Untitled")); }
    RunReport run(const Workflow& wf) {
        EngineDeps d;
        d.windows = &windows;
        d.locator = &locator;
        d.input = &input;
        d.clock = &clock;
        return WorkflowEngine(d).run(wf, token);
    }
};
Workflow flow(std::initializer_list<Step> steps) {
    Workflow wf = minimalWorkflow();
    wf.steps.assign(steps.begin(), steps.end());
    wf.execution.stepTimeoutMs = 300;
    return wf;
}
}  // namespace

TEST(keyboard_input_is_refused_when_target_is_not_foreground) {
    Env env;
    env.windows.foreground = false;
    RunReport r = env.run(flow({typeStep("S1", "hello")}));
    CHECK(r.state == ExecutionState::Failed);
    CHECK(r.code == ErrCode::InputBlocked);
    CHECK_CONTAINS(r.detail, "activate_window");
    CHECK(env.input.actions.empty());

    Env env2;
    env2.windows.foreground = false;
    RunReport r2 = env2.run(flow({hotkeyStep("S1", {"CTRL", "S"})}));
    CHECK(r2.code == ErrCode::InputBlocked);
    CHECK(env2.input.actions.empty());
}

TEST(keyboard_input_is_not_retried_when_not_foreground) {
    Env env;
    env.windows.foreground = false;
    Step t = typeStep("S1", "x");
    t.retry = RetryPolicy{3, 10};
    RunReport r = env.run(flow({t}));
    CHECK_EQ(r.steps[0].attempts, 1);
}

TEST(clicks_carry_the_target_window_handle) {
    Env env;
    env.locator.handler = [](const LeafLocator&, int) {
        return Result<ElementRef>::success(FakeElementLocator::element("pt", /*invoke=*/false));
    };
    RunReport r = env.run(flow({clickStep("S1")}));
    CHECK(r.state == ExecutionState::Succeeded);
    CHECK_EQ(env.input.lastClickWindow, std::uint64_t(1));
}

TEST(click_blocked_by_an_occluding_window_fails_without_retry) {
    Env env;
    env.locator.handler = [](const LeafLocator&, int) {
        return Result<ElementRef>::success(FakeElementLocator::element("pt", false));
    };
    env.input.hook = [](const std::string&) {
        return Status::failure(ErrCode::InputBlocked, "point is covered by another window");
    };
    Step c = clickStep("S1");
    c.retry = RetryPolicy{2, 10};
    RunReport r = env.run(flow({c}));
    CHECK(r.code == ErrCode::InputBlocked);
    CHECK_EQ(r.steps[0].attempts, 1);
}

TEST(fallback_skips_strategies_that_are_unsupported) {
    Env env;
    env.locator.handler = [](const LeafLocator& l, int) {
        if (std::holds_alternative<UiaLocator>(l)) {
            return Result<ElementRef>::failure(ErrCode::Unsupported, "uia arrives in stage 3");
        }
        return Result<ElementRef>::success(FakeElementLocator::element("pt", false));
    };
    Step c = step("S1", StepType::Click);
    FallbackLocator fb;
    fb.items.push_back(uia("a", "Button"));
    fb.items.push_back(WindowPointLocator{5, 5, Anchor::TopLeft});
    c.locator = Locator{fb};
    RunReport r = env.run(flow({c}));
    CHECK(r.state == ExecutionState::Succeeded);
    CHECK_EQ(env.locator.calls.size(), size_t(2));
}

TEST(fallback_with_only_unsupported_strategies_reports_unsupported) {
    Env env;
    env.locator.handler = [](const LeafLocator&, int) {
        return Result<ElementRef>::failure(ErrCode::Unsupported, "not yet");
    };
    Step c = step("S1", StepType::Click);
    FallbackLocator fb;
    fb.items.push_back(uia("a", "Button"));
    fb.items.push_back(uia("b", "Button"));
    c.locator = Locator{fb};
    RunReport r = env.run(flow({c}));
    CHECK(r.code == ErrCode::Unsupported);
    CHECK_EQ(r.steps[0].attempts, 1);
}

TEST(plain_unsupported_locator_fails_immediately_without_polling) {
    Env env;
    env.locator.handler = [](const LeafLocator&, int) {
        return Result<ElementRef>::failure(ErrCode::Unsupported, "not yet");
    };
    RunReport r = env.run(flow({clickStep("S1")}));
    CHECK(r.code == ErrCode::Unsupported);
    CHECK_EQ(env.locator.calls.size(), size_t(1));
}

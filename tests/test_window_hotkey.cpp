#include <set>
#include <string>

#include "core/Engine.h"
#include "core/StopHotkey.h"
#include "core/Validator.h"
#include "core/VirtualKey.h"
#include "core/WindowMatcher.h"
#include "fakes/Fakes.h"
#include "TestUtil.h"
#include "testing.h"

using namespace wa;
using namespace wa::fakes;

namespace {
WindowInfo win(std::uint64_t h, const char* proc, const char* title, std::uint32_t pid = 100,
               bool visible = true) {
    WindowInfo w;
    w.handle = h;
    w.processName = proc;
    w.title = title;
    w.processId = pid;
    w.visible = visible;
    return w;
}
WindowSpec spec(const char* proc, const char* title = "") {
    WindowSpec s;
    s.processName = proc;
    s.titleContains = title;
    return s;
}
}  // namespace

// ------------------------------------------------------------------ 窗口匹配

TEST(process_name_matching_ignores_case_and_exe_suffix) {
    std::vector<WindowInfo> all = {win(1, "Notepad.EXE", "a")};
    CHECK_EQ(matchWindows(all, spec("notepad.exe")).size(), size_t(1));
    CHECK_EQ(matchWindows(all, spec("NOTEPAD")).size(), size_t(1));
    CHECK_EQ(matchWindows(all, spec("notepad2.exe")).size(), size_t(0));
    CHECK_EQ(matchWindows(all, spec("pad")).size(), size_t(0));  // 不是子串匹配
    CHECK_EQ(normalizeProcessName("A.EXE"), "a");
    CHECK_EQ(normalizeProcessName(".exe"), ".exe");  // 过短的名字不被截空
}

TEST(title_matching_is_substring_and_ascii_case_insensitive) {
    std::vector<WindowInfo> all = {win(1, "app.exe", "Report - Draft"), win(2, "app.exe", "报告 - 草稿")};
    CHECK_EQ(matchWindows(all, spec("app.exe", "draft")).size(), size_t(1));
    CHECK_EQ(matchWindows(all, spec("app.exe", "草稿")).size(), size_t(1));
    CHECK_EQ(matchWindows(all, spec("app.exe", "草稿"))[0].handle, std::uint64_t(2));
    CHECK_EQ(matchWindows(all, spec("", "report")).size(), size_t(1));  // 只给标题也行
}

TEST(both_conditions_must_hold_and_empty_spec_matches_nothing) {
    std::vector<WindowInfo> all = {win(1, "a.exe", "x"), win(2, "b.exe", "x")};
    CHECK_EQ(matchWindows(all, spec("a.exe", "x")).size(), size_t(1));
    CHECK_EQ(matchWindows(all, spec("a.exe", "y")).size(), size_t(0));
    CHECK_EQ(matchWindows(all, spec("", "")).size(), size_t(0));
}

TEST(invisible_and_own_windows_are_excluded) {
    std::vector<WindowInfo> all = {win(1, "a.exe", "t", 10, /*visible=*/false), win(2, "a.exe", "t", 20),
                                   win(3, "a.exe", "t", 30)};
    CHECK_EQ(matchWindows(all, spec("a.exe")).size(), size_t(2));
    MatchOptions opt;
    opt.excludePid = 30;
    auto hits = matchWindows(all, spec("a.exe"), opt);
    CHECK_EQ(hits.size(), size_t(1));
    CHECK_EQ(hits[0].handle, std::uint64_t(2));
    opt.requireVisible = false;
    opt.excludePid = 0;
    CHECK_EQ(matchWindows(all, spec("a.exe"), opt).size(), size_t(3));
}

TEST(pick_unique_requires_exactly_one) {
    std::vector<WindowInfo> one = {win(1, "a.exe", "t")};
    auto r = pickUniqueWindow(one, spec("a.exe"));
    CHECK(r.ok());
    CHECK_EQ(r.value.handle, std::uint64_t(1));

    auto none = pickUniqueWindow(one, spec("zzz.exe", "q"));
    CHECK(none.code == ErrCode::WindowNotFound);
    CHECK_CONTAINS(none.detail, "zzz.exe");

    std::vector<WindowInfo> two = {win(1, "a.exe", "Doc1", 11), win(2, "a.exe", "Doc2", 22)};
    auto amb = pickUniqueWindow(two, spec("a.exe"));
    CHECK(amb.code == ErrCode::WindowNotFound);
    CHECK_CONTAINS(amb.detail, "not unique");
    CHECK_CONTAINS(amb.detail, "Doc1");
    CHECK_CONTAINS(amb.detail, "titleContains");
    // 补充标题后就唯一了
    CHECK(pickUniqueWindow(two, spec("a.exe", "Doc2")).ok());
}

TEST(ambiguity_list_is_capped) {
    std::vector<WindowInfo> many;
    for (int i = 0; i < 12; ++i) many.push_back(win(i + 1, "a.exe", "w", 100 + i));
    auto r = pickUniqueWindow(many, spec("a.exe"));
    CHECK_CONTAINS(r.detail, "12 candidates");
    CHECK_CONTAINS(r.detail, "...");
    CHECK_NOT_CONTAINS(r.detail, "pid 111");
}

// ------------------------------------------------------------------ 虚拟键与热键

TEST(virtual_key_codes_match_win32_constants) {
    CHECK_EQ(*virtualKeyFor("a"), 0x41u);
    CHECK_EQ(*virtualKeyFor("Z"), 0x5Au);
    CHECK_EQ(*virtualKeyFor("0"), 0x30u);
    CHECK_EQ(*virtualKeyFor("9"), 0x39u);
    CHECK_EQ(*virtualKeyFor("F1"), 0x70u);
    CHECK_EQ(*virtualKeyFor("f10"), 0x79u);
    CHECK_EQ(*virtualKeyFor("F24"), 0x87u);
    CHECK_EQ(*virtualKeyFor("ctrl"), 0x11u);
    CHECK_EQ(*virtualKeyFor("Control"), 0x11u);
    CHECK_EQ(*virtualKeyFor("SHIFT"), 0x10u);
    CHECK_EQ(*virtualKeyFor("ALT"), 0x12u);
    CHECK_EQ(*virtualKeyFor("WIN"), 0x5Bu);
    CHECK_EQ(*virtualKeyFor("ENTER"), 0x0Du);
    CHECK_EQ(*virtualKeyFor("esc"), 0x1Bu);
    CHECK_EQ(*virtualKeyFor("DEL"), 0x2Eu);
    CHECK_EQ(*virtualKeyFor("PageDown"), 0x22u);
    CHECK_EQ(*virtualKeyFor("left"), 0x25u);
    CHECK(!virtualKeyFor("banana").has_value());
    CHECK(!virtualKeyFor("").has_value());
}

TEST(every_validator_key_has_a_distinct_virtual_key_per_name) {
    // Validator 接受的每个主要键名都必须能映射到虚拟键，否则校验通过的流程到执行时才失败
    const char* names[] = {"CTRL", "SHIFT", "ALT", "WIN", "ENTER", "TAB", "ESC", "SPACE", "BACKSPACE",
                           "DELETE", "INSERT", "HOME", "END", "PAGEUP", "PAGEDOWN", "UP", "DOWN",
                           "LEFT", "RIGHT"};
    std::set<unsigned> seen;
    for (const char* n : names) {
        CHECK(isSupportedKey(n));
        auto vk = virtualKeyFor(n);
        CHECK(vk.has_value());
        if (vk) CHECK(seen.insert(*vk).second);
    }
    for (int i = 1; i <= 24; ++i) CHECK(virtualKeyFor("F" + std::to_string(i)).has_value());
    for (char c = 'A'; c <= 'Z'; ++c) CHECK(virtualKeyFor(std::string(1, c)).has_value());
    for (char c = '0'; c <= '9'; ++c) CHECK(virtualKeyFor(std::string(1, c)).has_value());
}

TEST(extended_keys) {
    for (unsigned vk : {0x21u, 0x22u, 0x23u, 0x24u, 0x25u, 0x26u, 0x27u, 0x28u, 0x2Du, 0x2Eu, 0x5Bu}) {
        CHECK(isExtendedKey(vk));
    }
    CHECK(!isExtendedKey(0x41));  // A
    CHECK(!isExtendedKey(0x0D));  // ENTER（主键盘的 Enter 不是扩展键）
    CHECK(!isExtendedKey(0x11));  // CTRL 左键
}

TEST(parse_default_stop_hotkey) {
    auto r = parseHotkey(kDefaultStopHotkey);
    CHECK(r.ok());
    CHECK_EQ(r.value.modifiers, kModControl | kModAlt);
    CHECK_EQ(r.value.vk, 0x79u);
    CHECK_EQ(r.value.text, "CTRL+ALT+F10");
}

TEST(parse_hotkey_normalizes_order_case_and_spaces) {
    auto r = parseHotkey("shift + ctrl+  s");
    CHECK(r.ok());
    CHECK_EQ(r.value.text, "CTRL+SHIFT+S");
    CHECK_EQ(r.value.modifiers, kModControl | kModShift);
    CHECK_EQ(r.value.vk, 0x53u);

    auto w = parseHotkey("Win+Alt+End");
    CHECK(w.ok());
    CHECK_EQ(w.value.modifiers, kModWin | kModAlt);
}

TEST(parse_hotkey_rejects_bad_input) {
    CHECK(!parseHotkey("").ok());
    CHECK(!parseHotkey("F10").ok());          // 没有修饰键，会劫持普通按键
    CHECK(!parseHotkey("A").ok());
    CHECK(!parseHotkey("CTRL+ALT").ok());     // 没有主键
    CHECK(!parseHotkey("CTRL+A+B").ok());     // 两个主键
    CHECK(!parseHotkey("CTRL++A").ok());      // 空段
    CHECK(!parseHotkey("CTRL+BANANA").ok());
    CHECK(!parseHotkey("CTRL+").ok());
    CHECK(parseHotkey("CTRL+A+B").code == ErrCode::InvalidWorkflow);
}

// ------------------------------------------------------------------ 停止热键

TEST(pressing_the_hotkey_cancels_the_token) {
    FakeStopHotkey hk;
    CancelToken token;
    StopHotkeyGuard guard(hk, token);
    auto spec = parseHotkey(kDefaultStopHotkey).value;
    CHECK(guard.arm(spec).ok());
    CHECK(guard.armed());
    CHECK_EQ(hk.registered.text, "CTRL+ALT+F10");
    CHECK(!token.isCancelled());
    hk.press();
    CHECK(token.isCancelled());
}

TEST(registration_failure_is_reported_and_guard_stays_disarmed) {
    FakeStopHotkey hk;
    hk.registerResult = Status::failure(ErrCode::InputBlocked, "hotkey already in use");
    CancelToken token;
    StopHotkeyGuard guard(hk, token);
    auto st = guard.arm(parseHotkey("CTRL+ALT+F10").value);
    CHECK(!st.ok());
    CHECK(st.code == ErrCode::InputBlocked);
    CHECK(!guard.armed());
    hk.press();
    CHECK(!token.isCancelled());
}

TEST(guard_unregisters_on_destruction_and_on_disarm) {
    FakeStopHotkey hk;
    CancelToken token;
    {
        StopHotkeyGuard guard(hk, token);
        guard.arm(parseHotkey("CTRL+ALT+F10").value);
        CHECK(hk.active);
    }
    CHECK(!hk.active);
    CHECK_EQ(hk.unregisterCalls, 1);

    CancelToken t2;
    StopHotkeyGuard g2(hk, t2);
    g2.arm(parseHotkey("CTRL+ALT+F10").value);
    g2.disarm();
    g2.disarm();  // 重复 disarm 无副作用
    CHECK_EQ(hk.unregisterCalls, 2);
    hk.press();
    CHECK(!t2.isCancelled());
}

TEST(arming_twice_does_not_leak_a_registration) {
    FakeStopHotkey hk;
    CancelToken token;
    StopHotkeyGuard guard(hk, token);
    guard.arm(parseHotkey("CTRL+ALT+F10").value);
    guard.arm(parseHotkey("CTRL+ALT+F9").value);
    CHECK_EQ(hk.registerCalls, 2);
    CHECK_EQ(hk.unregisterCalls, 1);  // 第二次 arm 先注销了第一次
    CHECK_EQ(guard.spec().text, "CTRL+ALT+F9");
}

TEST(hotkey_stops_a_running_workflow_at_any_moment) {
    // 端到端：流程在长等待中，用户按下热键 -> 引擎在该步骤内以 Cancelled 结束，后续动作不执行
    FakeClock clock;
    FakeWindowProvider windows;
    windows.windows.push_back(FakeWindowProvider::window(1, "notepad.exe"));
    FakeElementLocator locator;
    FakeInputController input;
    FakeStopHotkey hk;
    CancelToken token;

    StopHotkeyGuard guard(hk, token);
    CHECK(guard.arm(parseHotkey(kDefaultStopHotkey).value).ok());
    int sleeps = 0;
    clock.onSleep = [&](std::int64_t) {
        if (++sleeps == 1) hk.press();  // wait 步骤睡眠期间用户按下热键
    };

    Workflow wf = testutil::minimalWorkflow();
    wf.steps.push_back(testutil::waitStep("W", 10000));
    wf.steps.push_back(testutil::typeStep("T", "must-not-run"));

    EngineDeps deps;
    deps.windows = &windows;
    deps.locator = &locator;
    deps.input = &input;
    deps.clock = &clock;
    WorkflowEngine engine(deps);
    RunReport r = engine.run(wf, token);

    CHECK(r.state == ExecutionState::Cancelled);
    CHECK_EQ(r.failedStepId, "W");
    CHECK(input.actions.empty());
}

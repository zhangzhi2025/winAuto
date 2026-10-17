#include "core/ControlType.h"
#include "core/UiaMatch.h"
#include "testing.h"

using namespace wa;

namespace {
UiaNode node(const char* id, const char* name, const char* type, int w = 80, int h = 24) {
    UiaNode n;
    n.automationId = id;
    n.name = name;
    n.controlType = type;
    n.bounds = Rect{10, 10, w, h};
    return n;
}
UiaSnapshot snap(std::vector<UiaNode> v) {
    UiaSnapshot s;
    s.nodes = std::move(v);
    return s;
}
UiaLocator loc(const char* id, const char* name, const char* type) {
    UiaLocator l;
    l.automationId = id;
    l.name = name;
    l.controlType = type;
    return l;
}
}  // namespace

TEST(controltype_names) {
    CHECK_EQ(controlTypeName(50000), std::string("Button"));
    CHECK_EQ(controlTypeName(50004), std::string("Edit"));
    CHECK_EQ(controlTypeName(50040), std::string("AppBar"));
    CHECK_EQ(controlTypeName(49999), std::string());
    CHECK_EQ(controlTypeName(50041), std::string());
    CHECK_EQ(canonicalControlType("button"), std::string("Button"));
    CHECK_EQ(canonicalControlType("nope"), std::string());
    CHECK_EQ(allControlTypes().size(), static_cast<size_t>(41));
}

TEST(match_conditions_and) {
    UiaNode n = node("btnOk", "OK", "Button");
    CHECK(nodeMatches(n, loc("btnOk", "", "")));
    CHECK(nodeMatches(n, loc("btnOk", "OK", "button")));
    CHECK(!nodeMatches(n, loc("btnOk", "OK", "Edit")));
    CHECK(!nodeMatches(n, loc("BTNOK", "", "")));  // automationId 区分大小写
    CHECK(!nodeMatches(n, loc("", "", "")));       // 空条件不匹配任何东西
    CHECK(!nodeMatches(n, loc("", "", "Bogus")));  // 未知类型不匹配
}

TEST(match_name_modes) {
    UiaNode n = node("", "Save As...", "Button");
    UiaLocator l = loc("", "save", "");
    CHECK(!nodeMatches(n, l));
    l.match = Match::Contains;
    CHECK(nodeMatches(n, l));
}

TEST(pick_unique_found) {
    auto s = snap({node("a", "A", "Button"), node("b", "B", "Button")});
    auto r = pickNode(s, loc("b", "", ""));
    CHECK(r.ok());
    CHECK_EQ(r.value, static_cast<size_t>(1));
}

TEST(pick_ambiguous_refuses) {
    auto s = snap({node("", "OK", "Button"), node("", "OK", "Button")});
    auto r = pickNode(s, loc("", "OK", "Button"));
    CHECK_EQ(r.code, ErrCode::ElementAmbiguous);
    CHECK_CONTAINS(r.detail, "refusing to guess");
    UiaLocator l = loc("", "OK", "Button");
    l.ambiguity = Ambiguity::First;
    auto r2 = pickNode(s, l);
    CHECK(r2.ok());
    CHECK_EQ(r2.value, static_cast<size_t>(0));
}

TEST(pick_skips_offscreen_and_empty) {
    UiaNode off = node("x", "X", "Button");
    off.offscreen = true;
    UiaNode zero = node("x", "X", "Button", 0, 0);
    UiaNode good = node("x", "X", "Button");
    auto r = pickNode(snap({off, zero, good}), loc("x", "", ""));
    CHECK(r.ok());
    CHECK_EQ(r.value, static_cast<size_t>(2));
    auto r2 = pickNode(snap({off}), loc("x", "", ""));
    CHECK_EQ(r2.code, ErrCode::ElementNotFound);
    CHECK_CONTAINS(r2.detail, "none is on screen");
}

TEST(diagnose_hints) {
    auto s = snap({node("btnOk", "OK", "Button"), node("txt", "Hello", "Edit")});
    auto r = pickNode(s, loc("btnOk", "", "Edit"));
    CHECK_EQ(r.code, ErrCode::ElementNotFound);
    CHECK_CONTAINS(r.detail, "exists but the other conditions differ");
    auto r2 = pickNode(s, loc("", "zzz", ""));
    CHECK_CONTAINS(r2.detail, "no element name contains");
    UiaLocator l = loc("", "hel", "");
    auto r3 = pickNode(s, loc("", "Hel", "Edit"));
    CHECK_CONTAINS(r3.detail, "similar name");
    auto r4 = pickNode(snap({}), l);
    CHECK_CONTAINS(r4.detail, "window_point");
    auto r5 = pickNode(s, loc("", "", "Bogus"));
    CHECK_CONTAINS(r5.detail, "unknown controlType");
}

TEST(suggest_prefers_automation_id) {
    auto s = snap({node("btnOk", "OK", "Button"), node("btnNo", "No", "Button")});
    auto r = suggestLocator(s, 0);
    CHECK(r.ok());
    CHECK_EQ(r.value.locator.automationId, std::string("btnOk"));
    CHECK_EQ(r.value.locator.controlType, std::string("Button"));
    CHECK(r.value.warnings.empty());
}

TEST(suggest_falls_back_to_name_with_warning) {
    auto s = snap({node("", "OK", "Button"), node("", "No", "Button")});
    auto r = suggestLocator(s, 1);
    CHECK(r.ok());
    CHECK_EQ(r.value.locator.name, std::string("No"));
    CHECK(!r.value.warnings.empty());
}

TEST(suggest_fails_when_not_unique) {
    auto s = snap({node("", "OK", "Button"), node("", "OK", "Button")});
    auto r = suggestLocator(s, 0);
    CHECK_EQ(r.code, ErrCode::ElementAmbiguous);
    CHECK_CONTAINS(r.detail, "window_point");
    CHECK_EQ(suggestLocator(s, 9).code, ErrCode::Internal);
}

TEST(dump_truncates) {
    UiaSnapshot s;
    for (int i = 0; i < 10; ++i) s.nodes.push_back(node("", "n", "Text"));
    s.nodes[1].depth = 2;
    std::string d = dumpSnapshot(s, 3);
    CHECK_CONTAINS(d, "7 more elements");
    CHECK_CONTAINS(d, "    [Text]");
}

#include "core/Json.h"

TEST(locator_to_json_roundtrips_through_workflow_reader) {
    UiaLocator u;
    u.automationId = "btnOk";
    u.controlType = "Button";
    std::string j = locatorToJson(Locator{u});
    CHECK_CONTAINS(j, "\"strategy\": \"uia\"");
    CHECK_CONTAINS(j, "\"automationId\": \"btnOk\"");
    // 嵌入一个最小流程并读回
    std::string wf = "{\"schemaVersion\":\"1.0\",\"id\":\"t\",\"name\":\"t\",\"windows\":{\"m\":{\"processName\":\"a.exe\"}},"
                     "\"steps\":[{\"id\":\"S1\",\"type\":\"click\",\"window\":\"m\",\"locator\":" + j + "}]}";
    auto r = readWorkflow(wf);
    CHECK(r.ok());
    if (!r.ok() || r.value.steps.empty()) return;
    const auto* back = std::get_if<UiaLocator>(&*r.value.steps[0].locator);
    CHECK(back != nullptr);
    CHECK_EQ(back->automationId, std::string("btnOk"));
}

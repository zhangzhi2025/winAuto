#include <cstdint>

#include "core/GrayImage.h"
#include "core/TemplateAssess.h"
#include "testing.h"

using namespace wa;

namespace {

GrayImage noise(int w, int h, std::uint32_t seed) {
    GrayImage g;
    g.w = w;
    g.h = h;
    g.px.resize(static_cast<size_t>(w) * static_cast<size_t>(h));
    std::uint32_t s = seed;
    for (auto& p : g.px) {
        s = s * 1664525u + 1013904223u;
        p = static_cast<std::uint8_t>(s >> 24);
    }
    return g;
}

void fill(GrayImage& g, Rect r, std::uint8_t v) {
    for (int y = r.y; y < r.y + r.h; ++y)
        for (int x = r.x; x < r.x + r.w; ++x) g.px[static_cast<size_t>(y) * static_cast<size_t>(g.w) + static_cast<size_t>(x)] = v;
}

void copyBlock(GrayImage& g, Rect from, int toX, int toY) {
    for (int j = 0; j < from.h; ++j)
        for (int i = 0; i < from.w; ++i)
            g.px[static_cast<size_t>(toY + j) * static_cast<size_t>(g.w) + static_cast<size_t>(toX + i)] = g.at(from.x + i, from.y + j);
}

const CancelToken& never() {
    static CancelToken t;
    return t;
}

bool mentions(const std::vector<std::string>& v, const std::string& needle) {
    for (const auto& s : v)
        if (s.find(needle) != std::string::npos) return true;
    return false;
}

}  // namespace

TEST(distinctive_region_is_usable_without_warnings) {
    GrayImage win = noise(240, 160, 7);
    auto a = assessTemplate(win, Rect{60, 40, 30, 24}, never());
    CHECK(a.usable);
    CHECK(a.errors.empty());
    CHECK(a.warnings.empty());
    CHECK(!a.suggestedRegion.has_value());
    CHECK_EQ(a.selection.x, 60);
    CHECK_EQ(a.selection.w, 30);
}

TEST(flat_region_is_rejected) {
    GrayImage win = noise(240, 160, 7);
    fill(win, Rect{100, 60, 50, 40}, 128);
    auto a = assessTemplate(win, Rect{105, 65, 30, 24}, never());
    CHECK(!a.usable);
    CHECK(mentions(a.errors, "纯色"));
}

TEST(tiny_region_is_rejected) {
    GrayImage win = noise(240, 160, 7);
    auto a = assessTemplate(win, Rect{10, 10, 5, 30}, never());
    CHECK(!a.usable);
    CHECK(mentions(a.errors, "太小"));
}

TEST(selection_outside_the_window_is_rejected) {
    GrayImage win = noise(100, 100, 7);
    auto a = assessTemplate(win, Rect{500, 500, 20, 20}, never());
    CHECK(!a.usable);
    CHECK(mentions(a.errors, "之外"));
}

TEST(selection_is_clamped_to_the_window) {
    GrayImage win = noise(120, 100, 7);
    auto a = assessTemplate(win, Rect{100, 80, 60, 60}, never());  // 右下角越界
    CHECK(a.usable);
    CHECK_EQ(a.selection.w, 20);
    CHECK_EQ(a.selection.h, 20);
}

TEST(repeated_pattern_warns_and_suggests_a_search_region) {
    GrayImage win = noise(300, 160, 11);
    copyBlock(win, Rect{40, 30, 28, 22}, 200, 100);  // 同样的图案出现第二次
    auto a = assessTemplate(win, Rect{40, 30, 28, 22}, never());
    CHECK(a.usable);  // 只是警告：用户可以限定搜索范围
    CHECK(mentions(a.warnings, "不止一处"));
    CHECK(a.suggestedRegion.has_value());
    CHECK(a.suggestedRegion->x <= 40 && a.suggestedRegion->y <= 30);
    CHECK(a.suggestedRegion->x + a.suggestedRegion->w >= 68);
    // 建议范围不应包含第二处
    CHECK(a.suggestedRegion->x + a.suggestedRegion->w < 200);
}

TEST(huge_selection_warns) {
    GrayImage win = noise(200, 120, 3);
    auto a = assessTemplate(win, Rect{10, 10, 150, 90}, never());
    CHECK(mentions(a.warnings, "很大"));
}

TEST(invalid_window_image_is_an_error) {
    GrayImage empty;
    auto a = assessTemplate(empty, Rect{0, 0, 20, 20}, never());
    CHECK(!a.usable);
}

TEST(expand_region_clamps_to_image) {
    Rect r = expandRegion(Rect{5, 5, 20, 20}, 40, 100, 60);
    CHECK_EQ(r.x, 0);
    CHECK_EQ(r.y, 0);
    CHECK_EQ(r.w, 65);
    CHECK_EQ(r.h, 60);
    Rect none = expandRegion(Rect{500, 500, 10, 10}, 5, 100, 60);
    CHECK_EQ(none.w, 0);
}

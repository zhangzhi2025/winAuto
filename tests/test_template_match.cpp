#include <cstdint>

#include "core/GrayImage.h"
#include "core/TemplateMatch.h"
#include "testing.h"

using namespace wa;

namespace {

// 确定性伪随机噪声图（LCG），平滑一点让图像更像真实界面而不是纯白噪声
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

GrayImage crop(const GrayImage& g, int x, int y, int w, int h) {
    int ox = 0, oy = 0;
    auto r = cropGray(g, x, y, w, h, &ox, &oy);
    return r.value;
}

void paste(GrayImage& dst, const GrayImage& src, int x, int y) {
    for (int j = 0; j < src.h; ++j)
        for (int i = 0; i < src.w; ++i)
            dst.px[static_cast<size_t>(y + j) * static_cast<size_t>(dst.w) + static_cast<size_t>(x + i)] = src.at(i, j);
}

const CancelToken& never() {
    static CancelToken t;
    return t;
}

Result<TemplateMatch> find(const GrayImage& h, const GrayImage& t, double conf = 0.9,
                           Ambiguity a = Ambiguity::Error) {
    return findTemplate(h, t, conf, a, never());
}

}  // namespace

TEST(finds_exact_crop_at_exact_position) {
    GrayImage hay = noise(200, 120, 1);
    GrayImage t = crop(hay, 57, 33, 24, 20);
    auto r = find(hay, t);
    CHECK(r.ok());
    CHECK_EQ(r.value.rect.x, 57);
    CHECK_EQ(r.value.rect.y, 33);
    CHECK_EQ(r.value.rect.w, 24);
    CHECK_EQ(r.value.rect.h, 20);
    CHECK(r.value.score > 0.999);
}

TEST(finds_in_corners_and_edges) {
    GrayImage hay = noise(150, 90, 2);
    for (auto pos : {std::pair<int, int>{0, 0}, {126, 0}, {0, 70}, {126, 70}}) {
        auto r = find(hay, crop(hay, pos.first, pos.second, 24, 20));
        CHECK(r.ok());
        CHECK_EQ(r.value.rect.x, pos.first);
        CHECK_EQ(r.value.rect.y, pos.second);
    }
}

TEST(robust_to_brightness_and_contrast_change) {
    GrayImage hay = noise(200, 120, 3);
    GrayImage t = crop(hay, 80, 40, 32, 24);
    GrayImage dim = hay;  // 整幅图变暗并降低对比度：模拟主题/亮度变化
    for (auto& p : dim.px) p = static_cast<std::uint8_t>(p * 0.6 + 30);
    auto r = find(dim, t);
    CHECK(r.ok());
    CHECK_EQ(r.value.rect.x, 80);
    CHECK_EQ(r.value.rect.y, 40);
}

TEST(tolerates_mild_noise) {
    GrayImage hay = noise(200, 120, 4);
    GrayImage t = crop(hay, 100, 50, 32, 32);
    std::uint32_t s = 99;
    for (auto& p : hay.px) {
        s = s * 1664525u + 1013904223u;
        const int d = static_cast<int>(s >> 28) - 8;  // ±8
        p = static_cast<std::uint8_t>(std::max(0, std::min(255, p + d)));
    }
    auto r = find(hay, t, 0.8);
    CHECK(r.ok());
    CHECK_EQ(r.value.rect.x, 100);
    CHECK_EQ(r.value.rect.y, 50);
}

TEST(unrelated_template_is_not_found_and_reports_best_score) {
    GrayImage hay = noise(160, 100, 5);
    GrayImage other = noise(24, 20, 777);
    auto r = find(hay, other);
    CHECK_EQ(r.code, ErrCode::ElementNotFound);
    CHECK_CONTAINS(r.detail, "below minConfidence");
}

TEST(flat_template_is_rejected) {
    GrayImage hay = noise(100, 60, 6);
    GrayImage flat;
    flat.w = 16;
    flat.h = 16;
    flat.px.assign(256, 200);
    auto r = find(hay, flat);
    CHECK_EQ(r.code, ErrCode::InvalidWorkflow);
    CHECK_CONTAINS(r.detail, "no contrast");
}

TEST(flat_search_area_never_matches) {
    GrayImage hay;
    hay.w = 80;
    hay.h = 60;
    hay.px.assign(80 * 60, 128);
    GrayImage t = noise(16, 16, 8);
    CHECK_EQ(find(hay, t, 0.5).code, ErrCode::ElementNotFound);
}

TEST(template_larger_than_search_area) {
    auto r = find(noise(30, 30, 9), noise(40, 20, 10));
    CHECK_EQ(r.code, ErrCode::ElementNotFound);
    CHECK_CONTAINS(r.detail, "smaller than the template");
}

TEST(invalid_arguments) {
    GrayImage hay = noise(50, 50, 11);
    GrayImage t = crop(hay, 5, 5, 10, 10);
    CHECK_EQ(find(hay, t, 0.0).code, ErrCode::InvalidWorkflow);
    CHECK_EQ(find(hay, t, 1.5).code, ErrCode::InvalidWorkflow);
    CHECK_EQ(find(GrayImage{}, t).code, ErrCode::InvalidWorkflow);
}

TEST(two_identical_places_are_ambiguous_by_default) {
    GrayImage hay = noise(220, 140, 12);
    GrayImage t = crop(hay, 20, 20, 28, 24);
    paste(hay, t, 150, 90);  // 在另一处放一份相同的
    auto r = find(hay, t);
    CHECK_EQ(r.code, ErrCode::ElementAmbiguous);
    CHECK_CONTAINS(r.detail, "refusing to guess");
    CHECK_CONTAINS(r.detail, "2 places");
}

TEST(ambiguity_first_picks_topmost_then_leftmost) {
    GrayImage hay = noise(220, 140, 13);
    GrayImage t = crop(hay, 20, 60, 28, 24);
    paste(hay, t, 150, 10);  // 更靠上
    paste(hay, t, 20, 100);
    auto r = find(hay, t, 0.9, Ambiguity::First);
    CHECK(r.ok());
    CHECK_EQ(r.value.rect.y, 10);
    CHECK_EQ(r.value.rect.x, 150);
}

TEST(overlapping_shifted_hits_are_not_ambiguity) {
    // 平滑图像里，最佳位置旁边偏 1px 的得分也很高；这不是第二个目标
    GrayImage hay;
    hay.w = 120;
    hay.h = 80;
    hay.px.assign(120 * 80, 40);
    for (int y = 30; y < 50; ++y)
        for (int x = 50; x < 80; ++x) hay.px[static_cast<size_t>(y) * 120 + static_cast<size_t>(x)] = static_cast<std::uint8_t>(200 - (x - 50) * 3);
    GrayImage t = crop(hay, 46, 26, 40, 28);
    auto r = find(hay, t, 0.8);
    CHECK(r.ok());
    CHECK_EQ(r.value.rect.x, 46);
    CHECK_EQ(r.value.rect.y, 26);
}

TEST(small_template_searches_at_full_resolution) {
    GrayImage hay = noise(120, 80, 14);
    auto r = find(hay, crop(hay, 33, 21, 7, 7));
    CHECK(r.ok());
    CHECK_EQ(r.value.rect.x, 33);
    CHECK_EQ(r.value.rect.y, 21);
}

TEST(large_template_uses_pyramid_and_still_exact) {
    // 平滑一些的大图（块状图案），让下采样后仍有结构
    GrayImage hay;
    hay.w = 480;
    hay.h = 320;
    hay.px.resize(480 * 320);
    for (int y = 0; y < 320; ++y)
        for (int x = 0; x < 480; ++x) {
            const std::uint32_t k = static_cast<std::uint32_t>((x / 6) * 73856093u) ^ static_cast<std::uint32_t>((y / 6) * 19349663u);
            hay.px[static_cast<size_t>(y) * 480 + static_cast<size_t>(x)] = static_cast<std::uint8_t>((k * 2654435761u) >> 24);
        }
    GrayImage t = crop(hay, 211, 143, 96, 72);
    auto r = find(hay, t);
    CHECK(r.ok());
    CHECK_EQ(r.value.rect.x, 211);
    CHECK_EQ(r.value.rect.y, 143);
}

TEST(cancel_is_honoured) {
    CancelToken tok;
    tok.cancel();
    GrayImage hay = noise(200, 120, 15);
    auto r = findTemplate(hay, crop(hay, 10, 10, 20, 20), 0.9, Ambiguity::Error, tok);
    CHECK_EQ(r.code, ErrCode::Cancelled);
}

TEST(gray_from_bgra_uses_luma_and_stride) {
    // 2x1 像素，stride 12（带 4 字节行尾填充）：红、绿
    const std::uint8_t data[12] = {0, 0, 255, 0, /*红 BGRA*/ 0, 255, 0, 0, /*绿*/ 9, 9, 9, 9};
    auto g = grayFromBgra(data, sizeof data, 2, 1, 12);
    CHECK(g.ok());
    CHECK_EQ(static_cast<int>(g.value.at(0, 0)), 76);   // 0.299*255
    CHECK_EQ(static_cast<int>(g.value.at(1, 0)), 150);  // 0.587*255
    CHECK_EQ(grayFromBgra(data, 4, 2, 1, 12).code, ErrCode::InvalidWorkflow);  // 缓冲区太小
    CHECK_EQ(grayFromBgra(data, sizeof data, 2, 1, 4).code, ErrCode::InvalidWorkflow);  // stride 太小
    CHECK_EQ(grayFromBgra(nullptr, 0, 2, 1, 8).code, ErrCode::InvalidWorkflow);
}

TEST(crop_clips_and_reports_origin) {
    GrayImage g = noise(20, 10, 16);
    int ox = -1, oy = -1;
    auto r = cropGray(g, -5, 4, 12, 20, &ox, &oy);
    CHECK(r.ok());
    CHECK_EQ(ox, 0);
    CHECK_EQ(oy, 4);
    CHECK_EQ(r.value.w, 7);
    CHECK_EQ(r.value.h, 6);
    CHECK_EQ(r.value.at(0, 0), g.at(0, 4));
    CHECK_EQ(cropGray(g, 30, 0, 5, 5, &ox, &oy).code, ErrCode::ElementNotFound);
    CHECK_EQ(cropGray(g, 0, 0, 0, 5, &ox, &oy).code, ErrCode::ElementNotFound);
}

TEST(downscale_averages_and_drops_odd_edge) {
    GrayImage g;
    g.w = 3;
    g.h = 2;
    g.px = {10, 20, 99, 30, 40, 99};
    GrayImage d = downscale2x(g);
    CHECK_EQ(d.w, 1);
    CHECK_EQ(d.h, 1);
    CHECK_EQ(static_cast<int>(d.px[0]), 25);
    GrayImage tiny;
    tiny.w = 1;
    tiny.h = 1;
    tiny.px = {1};
    CHECK(!downscale2x(tiny).valid());
}

TEST(full_hd_screen_with_odd_offsets_exercises_coarse_phases) {
    // 1920x1080、40x40 模板：代价超预算，必须走下采样粗搜索。纯噪声是最难的情形（下采样后网格相位错位会让得分塌掉），
    // 这里对每种相位（x、y 对 4 取模）各取一个位置，确认都能精确找到。
    GrayImage hay = noise(1920, 1080, 21);
    int tried = 0;
    for (int dx = 0; dx < 4; ++dx) {
        for (int dy = 0; dy < 4; ++dy) {
            const int x = 801 + dx * 111 + dx, y = 403 + dy * 97 + dy;
            auto r = find(hay, crop(hay, x, y, 40, 40));
            CHECK(r.ok());
            CHECK_EQ(r.value.rect.x, x);
            CHECK_EQ(r.value.rect.y, y);
            ++tried;
        }
    }
    CHECK_EQ(tried, 16);
}

TEST(partially_matching_template_respects_threshold) {
    // 模板有三成像素被换成别的内容：相关系数约 0.8。阈值 0.9 必须拒绝，阈值 0.6 应接受
    GrayImage hay = noise(160, 100, 31);
    GrayImage t = crop(hay, 40, 30, 32, 32);
    std::uint32_t s = 5;
    for (size_t i = 0; i < t.px.size(); ++i) {
        s = s * 1664525u + 1013904223u;
        if ((s >> 24) % 10 < 3) t.px[i] = static_cast<std::uint8_t>(s >> 16);
    }
    auto strict = find(hay, t, 0.95);
    CHECK_EQ(strict.code, ErrCode::ElementNotFound);
    CHECK_CONTAINS(strict.detail, "below minConfidence");
    auto lax = find(hay, t, 0.6);
    CHECK(lax.ok());
    CHECK_EQ(lax.value.rect.x, 40);
    CHECK_EQ(lax.value.rect.y, 30);
    CHECK(lax.value.score > 0.6 && lax.value.score < 0.95);
}

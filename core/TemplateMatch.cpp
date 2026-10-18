#include "core/TemplateMatch.h"

#include <algorithm>
#include <cmath>
#include <memory>
#include <sstream>

namespace wa {

namespace {

constexpr size_t kMaxCandidates = 24;
constexpr double kFlatEpsilon = 1e-6;

struct Cand {
    int x = 0, y = 0;
    double score = -2.0;
};

// 预处理后的模板：零均值，按行存 double
struct Tpl {
    int w = 0, h = 0;
    std::vector<double> z;  // 减均值后的值
    double norm = 0.0;      // sqrt(Σz²)
};

Tpl prepare(const GrayImage& t) {
    Tpl p;
    p.w = t.w;
    p.h = t.h;
    double sum = 0;
    for (auto v : t.px) sum += v;
    const double mean = sum / static_cast<double>(t.px.size());
    p.z.resize(t.px.size());
    double ss = 0;
    for (size_t i = 0; i < t.px.size(); ++i) {
        p.z[i] = static_cast<double>(t.px[i]) - mean;
        ss += p.z[i] * p.z[i];
    }
    p.norm = std::sqrt(ss);
    return p;
}

// 积分图（和与平方和），尺寸 (w+1)*(h+1)
struct Integral {
    int w = 0, h = 0;
    std::vector<double> s, s2;
    explicit Integral(const GrayImage& g) : w(g.w), h(g.h) {
        const size_t stride = static_cast<size_t>(w) + 1;
        s.assign(stride * (static_cast<size_t>(h) + 1), 0.0);
        s2.assign(s.size(), 0.0);
        for (int y = 0; y < h; ++y) {
            double rs = 0, rs2 = 0;
            for (int x = 0; x < w; ++x) {
                const double v = g.at(x, y);
                rs += v;
                rs2 += v * v;
                const size_t i = static_cast<size_t>(y + 1) * stride + static_cast<size_t>(x + 1);
                s[i] = s[i - stride] + rs;
                s2[i] = s2[i - stride] + rs2;
            }
        }
    }
    double rect(const std::vector<double>& a, int x, int y, int rw, int rh) const {
        const size_t stride = static_cast<size_t>(w) + 1;
        auto at = [&](int xx, int yy) { return a[static_cast<size_t>(yy) * stride + static_cast<size_t>(xx)]; };
        return at(x + rw, y + rh) - at(x, y + rh) - at(x + rw, y) + at(x, y);
    }
};

double zncc(const GrayImage& hay, const Integral& in, const Tpl& t, int x, int y) {
    double num = 0;
    for (int ty = 0; ty < t.h; ++ty) {
        const std::uint8_t* row = &hay.px[static_cast<size_t>(y + ty) * static_cast<size_t>(hay.w) + static_cast<size_t>(x)];
        const double* z = &t.z[static_cast<size_t>(ty) * static_cast<size_t>(t.w)];
        for (int tx = 0; tx < t.w; ++tx) num += static_cast<double>(row[tx]) * z[tx];
    }
    const double n = static_cast<double>(t.w) * static_cast<double>(t.h);
    const double sum = in.rect(in.s, x, y, t.w, t.h);
    const double var = in.rect(in.s2, x, y, t.w, t.h) - sum * sum / n;
    if (var <= kFlatEpsilon) return 0.0;  // 搜索窗口纯色：没有可比较的结构
    return num / (std::sqrt(var) * t.norm);
}

// f×f 块平均下采样；从 (ox,oy) 开始分块，不足一块的边缘丢弃。
GrayImage boxDownscale(const GrayImage& src, int f, int ox, int oy) {
    GrayImage g;
    g.w = (src.w - ox) / f;
    g.h = (src.h - oy) / f;
    if (g.w <= 0 || g.h <= 0) return GrayImage{};
    g.px.resize(static_cast<size_t>(g.w) * static_cast<size_t>(g.h));
    const unsigned area = static_cast<unsigned>(f * f);
    for (int y = 0; y < g.h; ++y) {
        for (int x = 0; x < g.w; ++x) {
            unsigned sum = 0;
            for (int j = 0; j < f; ++j)
                for (int i = 0; i < f; ++i) sum += src.at(ox + x * f + i, oy + y * f + j);
            g.px[static_cast<size_t>(y) * static_cast<size_t>(g.w) + static_cast<size_t>(x)] =
                static_cast<std::uint8_t>((sum + area / 2) / area);
        }
    }
    return g;
}

bool better(const Cand& a, const Cand& b) {
    if (a.score != b.score) return a.score > b.score;
    return a.y != b.y ? a.y < b.y : a.x < b.x;
}

constexpr double kBudget = 2.5e8;  // 粗层允许的乘加次数上限（约零点几秒）
constexpr int kMinCoarseSide = 6;  // 下采样后模板最短边不小于此值
constexpr int kMaxLevel = 3;       // 最大下采样倍数 8

}  // namespace

Result<TemplateMatch> findTemplate(const GrayImage& hay0, const GrayImage& tpl0, double minConfidence,
                                   Ambiguity ambiguity, const CancelToken& token) {
    using R = Result<TemplateMatch>;
    if (!hay0.valid() || !tpl0.valid()) return R::failure(ErrCode::InvalidWorkflow, "invalid image");
    if (!(minConfidence > 0.0 && minConfidence <= 1.0)) {
        return R::failure(ErrCode::InvalidWorkflow, "minConfidence must be in (0, 1]");
    }
    if (tpl0.w > hay0.w || tpl0.h > hay0.h) {
        return R::failure(ErrCode::ElementNotFound, "the search area (" + std::to_string(hay0.w) + "x" +
                                                        std::to_string(hay0.h) + ") is smaller than the template (" +
                                                        std::to_string(tpl0.w) + "x" + std::to_string(tpl0.h) + ")");
    }
    const Tpl full = prepare(tpl0);
    if (full.norm <= kFlatEpsilon) {
        return R::failure(ErrCode::InvalidWorkflow,
                          "the template has no contrast (a single colour); it would match any area of that colour");
    }

    // 选粗搜索的下采样倍数 f：能在预算内完成的最小倍数。
    // 下采样后模板与搜索图的网格相位可能不一致（模板在原图里不一定落在块边界上），
    // 所以对模板的 f×f 种起始相位各做一份，分别在粗层搜索，避免因错位漏掉真目标。
    const double cost0 = static_cast<double>(hay0.w - tpl0.w + 1) * static_cast<double>(hay0.h - tpl0.h + 1) *
                         static_cast<double>(tpl0.w) * static_cast<double>(tpl0.h);
    int level = 0;
    while (level < kMaxLevel && cost0 / static_cast<double>(1 << (2 * level)) > kBudget &&
           std::min(tpl0.w, tpl0.h) / (2 << level) >= kMinCoarseSide) {
        ++level;
    }
    const int f = 1 << level;

    const double slack = level == 0 ? 0.0 : 0.15;  // 粗层得分偏低，阈值放宽
    const double coarseThreshold = std::max(0.2, minConfidence - slack);

    std::vector<Cand> all;  // 已换算成原图坐标
    Cand bestSeen;          // 所有评估里的最高分（即使低于阈值），仅用于失败时给出线索
    {
        const GrayImage hayC = f == 1 ? hay0 : boxDownscale(hay0, f, 0, 0);
        if (!hayC.valid()) return R::failure(ErrCode::ElementNotFound, "search area too small");
        const Integral in(hayC);
        for (int py = 0; py < f; ++py) {
            for (int px = 0; px < f; ++px) {
                const GrayImage tv = f == 1 ? tpl0 : boxDownscale(tpl0, f, px, py);
                // 相位 (px,py) 的模板从原模板的 (px,py) 起，下采样后的块 (0,0) 对应原模板像素 (px,py)
                if (!tv.valid() || tv.w > hayC.w || tv.h > hayC.h) continue;
                const Tpl tp = prepare(tv);
                if (tp.norm <= kFlatEpsilon) continue;
                for (int y = 0; y + tv.h <= hayC.h; ++y) {
                    if (token.isCancelled()) return R::failure(ErrCode::Cancelled);
                    for (int x = 0; x + tv.w <= hayC.w; ++x) {
                        const double sc = zncc(hayC, in, tp, x, y);
                        const int ox = x * f - px, oy = y * f - py;  // 模板左上角在原图的位置
                        if (sc > bestSeen.score && ox >= 0 && oy >= 0) bestSeen = Cand{ox, oy, sc};
                        if (sc < coarseThreshold) continue;
                        if (ox < 0 || oy < 0 || ox + tpl0.w > hay0.w || oy + tpl0.h > hay0.h) continue;
                        all.push_back(Cand{ox, oy, sc});
                    }
                }
            }
        }
    }
    std::sort(all.begin(), all.end(), better);

    // 贪心非极大值抑制，保留前 K 个
    std::vector<Cand> cands;
    {
        const int sep = std::max(1, std::min(tpl0.w, tpl0.h) / 2);
        for (const Cand& c : all) {
            bool near = false;
            for (const Cand& k : cands) {
                if (std::abs(k.x - c.x) < sep && std::abs(k.y - c.y) < sep) { near = true; break; }
            }
            if (!near) cands.push_back(c);
            if (cands.size() >= kMaxCandidates) break;
        }
    }

    // 在原分辨率上精修：粗层位置只有 ±1~2 像素的误差
    if (level > 0) {
        const Integral in0(hay0);
        std::vector<Cand> refined;
        for (const Cand& c : cands) {
            if (token.isCancelled()) return R::failure(ErrCode::Cancelled);
            Cand best;
            for (int dy = -f; dy <= f; ++dy) {
                for (int dx = -f; dx <= f; ++dx) {
                    const int x = c.x + dx, y = c.y + dy;
                    if (x < 0 || y < 0 || x + tpl0.w > hay0.w || y + tpl0.h > hay0.h) continue;
                    const double sc = zncc(hay0, in0, full, x, y);
                    if (sc > best.score) best = Cand{x, y, sc};
                }
            }
            if (best.score > -2.0) refined.push_back(best);
        }
        std::sort(refined.begin(), refined.end(), better);
        cands.clear();
        for (const Cand& c : refined) {
            bool dup = false;
            for (const Cand& k : cands) {
                if (k.x == c.x && k.y == c.y) { dup = true; break; }
            }
            if (!dup) cands.push_back(c);
        }
    }

    if (cands.empty()) {
        std::ostringstream os;
        os << "image not found: best match score " << std::max(bestSeen.score, 0.0)
           << (level > 0 ? " (approximate)" : "") << " is below minConfidence " << minConfidence;
        return R::failure(ErrCode::ElementNotFound, os.str());
    }
    std::sort(cands.begin(), cands.end(), [](const Cand& a, const Cand& b) { return a.score > b.score; });
    const Cand best = cands.front();
    if (best.score < minConfidence) {
        std::ostringstream os;
        os << "image not found: best match score " << best.score << " is below minConfidence " << minConfidence
           << " (at " << best.x << "," << best.y << ")";
        return R::failure(ErrCode::ElementNotFound, os.str());
    }

    // 其他不重叠且同样达标的位置 → 歧义
    std::vector<Cand> rivals;
    for (size_t i = 1; i < cands.size(); ++i) {
        const Cand& c = cands[i];
        if (c.score < minConfidence) continue;
        const bool overlaps = std::abs(c.x - best.x) < tpl0.w / 2 + 1 && std::abs(c.y - best.y) < tpl0.h / 2 + 1;
        if (!overlaps) rivals.push_back(c);
    }
    Cand chosen = best;
    if (!rivals.empty()) {
        if (ambiguity == Ambiguity::Error) {
            std::ostringstream os;
            os << (rivals.size() + 1) << " places match the template; refusing to guess (add searchRegion, use a more "
               << "distinctive template, or set ambiguity=first): (" << best.x << "," << best.y << ") score "
               << best.score;
            for (size_t i = 0; i < rivals.size() && i < 4; ++i) {
                os << "; (" << rivals[i].x << "," << rivals[i].y << ") score " << rivals[i].score;
            }
            return R::failure(ErrCode::ElementAmbiguous, os.str());
        }
        // First：得分相近（差 0.02 内）的达标位置里取最靠上、再靠左的，结果与得分的微小噪声无关
        for (const Cand& c : rivals) {
            if (c.score >= best.score - 0.02 && (c.y < chosen.y || (c.y == chosen.y && c.x < chosen.x))) chosen = c;
        }
    }

    TemplateMatch m;
    m.rect = Rect{chosen.x, chosen.y, tpl0.w, tpl0.h};
    m.score = chosen.score;
    return R::success(m);
}

}  // namespace wa

#include "core/GrayImage.h"

#include <algorithm>

namespace wa {

Result<GrayImage> grayFromBgra(const std::uint8_t* data, size_t byteCount, int w, int h, int stride) {
    using R = Result<GrayImage>;
    if (!data || w <= 0 || h <= 0 || stride < w * 4) {
        return R::failure(ErrCode::InvalidWorkflow, "invalid bitmap dimensions");
    }
    const size_t needed = static_cast<size_t>(stride) * static_cast<size_t>(h - 1) + static_cast<size_t>(w) * 4;
    if (byteCount < needed) return R::failure(ErrCode::InvalidWorkflow, "bitmap buffer is too small");

    GrayImage g;
    g.w = w;
    g.h = h;
    g.px.resize(static_cast<size_t>(w) * static_cast<size_t>(h));
    for (int y = 0; y < h; ++y) {
        const std::uint8_t* row = data + static_cast<size_t>(y) * static_cast<size_t>(stride);
        std::uint8_t* out = &g.px[static_cast<size_t>(y) * static_cast<size_t>(w)];
        for (int x = 0; x < w; ++x) {
            const unsigned b = row[x * 4 + 0], gr = row[x * 4 + 1], r = row[x * 4 + 2];
            out[x] = static_cast<std::uint8_t>((299u * r + 587u * gr + 114u * b + 500u) / 1000u);
        }
    }
    return R::success(std::move(g));
}

Result<GrayImage> cropGray(const GrayImage& src, int x, int y, int w, int h, int* originX, int* originY) {
    using R = Result<GrayImage>;
    if (!src.valid()) return R::failure(ErrCode::InvalidWorkflow, "invalid image");
    const long long x0 = std::max<long long>(0, x), y0 = std::max<long long>(0, y);
    const long long x1 = std::min<long long>(src.w, static_cast<long long>(x) + w);
    const long long y1 = std::min<long long>(src.h, static_cast<long long>(y) + h);
    if (w <= 0 || h <= 0 || x1 <= x0 || y1 <= y0) {
        return R::failure(ErrCode::ElementNotFound, "search region is outside the image");
    }
    GrayImage g;
    g.w = static_cast<int>(x1 - x0);
    g.h = static_cast<int>(y1 - y0);
    g.px.resize(static_cast<size_t>(g.w) * static_cast<size_t>(g.h));
    for (int row = 0; row < g.h; ++row) {
        const std::uint8_t* s = &src.px[static_cast<size_t>(y0 + row) * static_cast<size_t>(src.w) + static_cast<size_t>(x0)];
        std::copy(s, s + g.w, &g.px[static_cast<size_t>(row) * static_cast<size_t>(g.w)]);
    }
    if (originX) *originX = static_cast<int>(x0);
    if (originY) *originY = static_cast<int>(y0);
    return R::success(std::move(g));
}

GrayImage downscale2x(const GrayImage& src) {
    GrayImage g;
    g.w = src.w / 2;
    g.h = src.h / 2;
    if (g.w <= 0 || g.h <= 0) return GrayImage{};
    g.px.resize(static_cast<size_t>(g.w) * static_cast<size_t>(g.h));
    for (int y = 0; y < g.h; ++y) {
        for (int x = 0; x < g.w; ++x) {
            const unsigned s = src.at(2 * x, 2 * y) + src.at(2 * x + 1, 2 * y) + src.at(2 * x, 2 * y + 1) +
                               src.at(2 * x + 1, 2 * y + 1);
            g.px[static_cast<size_t>(y) * static_cast<size_t>(g.w) + static_cast<size_t>(x)] =
                static_cast<std::uint8_t>((s + 2u) / 4u);
        }
    }
    return g;
}

}  // namespace wa

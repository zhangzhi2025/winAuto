#pragma once

#include <algorithm>

#include "StockModel.h"

inline stock_monitor::Image makeStockImage(int width, int height, std::uint8_t r = 20,
                                           std::uint8_t g = 20, std::uint8_t b = 20) {
    stock_monitor::Image image;
    image.width = width;
    image.height = height;
    image.stride = static_cast<std::size_t>(width) * 4U;
    image.rgba.resize(image.stride * static_cast<std::size_t>(height));
    for (std::size_t i = 0; i < image.rgba.size(); i += 4U) {
        image.rgba[i] = r;
        image.rgba[i + 1U] = g;
        image.rgba[i + 2U] = b;
        image.rgba[i + 3U] = 255;
    }
    return image;
}

inline void paintStockRect(stock_monitor::Image& image, const stock_monitor::Rect& rect,
                           std::uint8_t r, std::uint8_t g, std::uint8_t b) {
    const int right = std::min(image.width, rect.x + rect.width);
    const int bottom = std::min(image.height, rect.y + rect.height);
    for (int y = std::max(0, rect.y); y < bottom; ++y) {
        for (int x = std::max(0, rect.x); x < right; ++x) {
            const auto offset = static_cast<std::size_t>(y) * image.stride +
                                static_cast<std::size_t>(x) * 4U;
            image.rgba[offset] = r;
            image.rgba[offset + 1U] = g;
            image.rgba[offset + 2U] = b;
        }
    }
}

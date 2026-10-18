#pragma once
// 灰度图像：图像识别在灰度上做模板匹配，纯数据，不含任何平台类型。
#include <cstdint>
#include <vector>

#include "core/ErrCode.h"

namespace wa {

struct GrayImage {
    int w = 0;
    int h = 0;
    std::vector<std::uint8_t> px;  // 行优先，大小 w*h

    bool valid() const { return w > 0 && h > 0 && px.size() == static_cast<size_t>(w) * static_cast<size_t>(h); }
    std::uint8_t at(int x, int y) const { return px[static_cast<size_t>(y) * static_cast<size_t>(w) + static_cast<size_t>(x)]; }
};

// 从 BGRA（Windows 位图/WIC 的 32bpp 顺序）转灰度，整数亮度公式；alpha 忽略。
// stride 是每行字节数（>= w*4）。参数不合法返回 InvalidWorkflow。
Result<GrayImage> grayFromBgra(const std::uint8_t* data, size_t byteCount, int w, int h, int stride);

// 取子区域（越界部分裁掉）；裁完为空返回 ElementNotFound。originX/Y 返回实际裁出区域左上角相对原图的偏移。
Result<GrayImage> cropGray(const GrayImage& src, int x, int y, int w, int h, int* originX, int* originY);

// 2x 下采样（2x2 取平均）；奇数尺寸丢掉最后一行/列。
GrayImage downscale2x(const GrayImage& src);

}  // namespace wa

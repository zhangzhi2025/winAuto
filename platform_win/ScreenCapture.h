#pragma once
// 屏幕截取（GDI BitBlt）。坐标是虚拟桌面物理像素，进程必须是 Per-Monitor V2 感知（否则坐标会被系统缩放）。
// 限制：硬件加速/受保护内容（部分视频、DRM 窗口、个别游戏）可能截到黑块；此时图像定位不可用，请改用 uia 或 window_point。
#include <cstdint>
#include <vector>

#include "core/ErrCode.h"
#include "core/Model.h"

namespace wa::platform {

struct Bitmap {
    int w = 0;
    int h = 0;
    std::vector<std::uint8_t> bgra;  // 紧密排列，stride = w*4，alpha 为 255
    bool hasTransparency = false;    // 仅加载文件时可能为 true
};

// 截取 screenRect（会裁到虚拟桌面范围内）。actual 返回实际截取的区域。区域为空返回 ElementNotFound。
Result<Bitmap> captureScreen(Rect screenRect, Rect* actual);

}  // namespace wa::platform

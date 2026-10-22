#pragma once
// 截取目标窗口 client 区域的当前画面（"截取模板"用）。坐标系与图像定位一致：client 区域左上角为原点，物理像素。
#include <cstdint>

#include "ScreenCapture.h"
#include "core/ErrCode.h"

namespace wa::platform {

// 窗口最小化 → ElementNotFound（需要先激活）。
Result<Bitmap> grabWindowClient(std::uint64_t windowHandle);

}  // namespace wa::platform

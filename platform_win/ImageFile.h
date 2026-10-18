#pragma once
// 图像文件读写（WIC）。读：PNG/BMP 等 → BGRA；写：PNG。路径为 UTF-8。
#include <string>

#include "ScreenCapture.h"
#include "core/ErrCode.h"

namespace wa::platform {

Result<Bitmap> loadImage(const std::string& utf8Path);
Status savePng(const std::string& utf8Path, const Bitmap& bitmap);

}  // namespace wa::platform

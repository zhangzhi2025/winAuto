#pragma once
// 图像定位：截取目标窗口的屏幕区域 → 灰度 → core 的模板匹配（ZNCC，已测试）。
// 平台层只做：取窗口区域、截屏、读模板文件、把结果换回屏幕坐标。规则（阈值、歧义、平坦模板）都在 core。
//
// 安全：
//   - 模板路径相对流程文件所在目录，并检查真实路径（符号链接/联接）不得越界；
//   - 带透明像素的模板直接拒绝（透明区域的 RGB 无意义，会让匹配失真）；
//   - 命中点必须落在目标窗口自己身上（不是被别的窗口遮住的同样图案）。
#include <map>
#include <string>

#include "core/GrayImage.h"
#include "core/Interfaces.h"

namespace wa::platform {

struct ImageHit {
    Rect screenRect;     // 命中区域，屏幕物理像素
    double score = 0.0;  // ZNCC
};

class Win32ImageLocator {
public:
    // baseDir：流程文件所在目录（UTF-8）；流程未保存时传空串，图像定位会提示先保存。
    explicit Win32ImageLocator(std::string baseDir) : baseDir_(std::move(baseDir)) {}

    Result<ImageHit> search(std::uint64_t windowHandle, const ImageLocator& locator, const CancelToken& token);
    Result<ElementRef> locate(std::uint64_t windowHandle, const ImageLocator& locator, const CancelToken& token);

private:
    struct Cached {
        long long stamp = 0;  // 文件大小 + 修改时间，变了就重新加载
        GrayImage gray;
    };
    Result<const GrayImage*> templateFor(const ImageLocator& locator);

    std::string baseDir_;
    std::map<std::string, Cached> cache_;
};

}  // namespace wa::platform

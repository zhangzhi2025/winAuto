#pragma once
#include "core/Interfaces.h"

namespace wa::platform {

// IElementLocator：支持 window_point / screen_point。
// uia 与 image 返回 Unsupported（阶段 3 / 5 实现），因此 fallback 链会跳过它们。
// 阶段 3 会把它与 UIA 定位器组合成一个复合定位器。
class Win32PointLocator : public IElementLocator {
public:
    Result<ElementRef> locate(std::uint64_t windowHandle, const LeafLocator& locator,
                              const CancelToken& token) override;
};

}  // namespace wa::platform

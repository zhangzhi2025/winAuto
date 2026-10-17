#pragma once
#include "core/Interfaces.h"

namespace wa::platform {

// IElementLocator：支持 window_point / screen_point。
// uia 与 image 返回 Unsupported（uia 由 Win32UiaLocator 处理），因此 fallback 链会跳过它们。
class Win32PointLocator : public IElementLocator {
public:
    Result<ElementRef> locate(std::uint64_t windowHandle, const LeafLocator& locator,
                              const CancelToken& token) override;
};

}  // namespace wa::platform

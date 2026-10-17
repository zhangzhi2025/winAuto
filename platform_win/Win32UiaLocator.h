#pragma once
#include "UiaSession.h"
#include "Win32PointLocator.h"
#include "core/Interfaces.h"

namespace wa::platform {

// 复合定位器：uia → UI Automation（快照 + core 规则）；window_point / screen_point → Win32PointLocator；
// image → Unsupported（阶段 5）。fallback 的展开由引擎负责，这里只处理叶子。
class Win32UiaLocator : public IElementLocator {
public:
    explicit Win32UiaLocator(UiaSession& session) : session_(session) {}
    Result<ElementRef> locate(std::uint64_t windowHandle, const LeafLocator& locator,
                              const CancelToken& token) override;

private:
    UiaSession& session_;
    Win32PointLocator points_;
};

}  // namespace wa::platform

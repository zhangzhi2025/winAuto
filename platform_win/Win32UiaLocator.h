#pragma once
#include "UiaSession.h"
#include "Win32ImageLocator.h"
#include "Win32PointLocator.h"
#include "core/Interfaces.h"

namespace wa::platform {

// 复合定位器：uia → UI Automation（快照 + core 规则）；window_point / screen_point → Win32PointLocator；
// image → Win32ImageLocator（阶段 5，未提供时 Unsupported）。fallback 的展开由引擎负责，这里只处理叶子。
class Win32UiaLocator : public IElementLocator {
public:
    // images 可为空：此时 image 策略返回 Unsupported（fallback 链会跳过它）
    explicit Win32UiaLocator(UiaSession& session, Win32ImageLocator* images = nullptr)
        : session_(session), images_(images) {}
    Result<ElementRef> locate(std::uint64_t windowHandle, const LeafLocator& locator,
                              const CancelToken& token) override;

private:
    UiaSession& session_;
    Win32ImageLocator* images_;
    Win32PointLocator points_;
};

}  // namespace wa::platform

#pragma once
// IScreenshot 的实现：把目标窗口所在的屏幕区域存成 PNG。
// 取"屏幕上看到的样子"而不是窗口自己的位图（PrintWindow）：这样能拍到盖在目标上面的弹窗、遮挡物，
// 这正是排查失败原因最需要的信息。代价：被其他窗口遮住的部分拍到的是遮挡物；最小化的窗口无法截图。
// 只截目标窗口的矩形，不截整个桌面，减少无关内容落盘。
// 限制：与图像定位相同，硬件加速/受保护内容可能是黑块。
#include "core/Interfaces.h"

namespace wa::platform {

class Win32Screenshot : public IScreenshot {
public:
    Status captureWindow(std::uint64_t windowHandle, const std::string& pngPath) override;
};

}  // namespace wa::platform

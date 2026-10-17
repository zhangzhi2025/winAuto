#pragma once
// UI Automation 会话：COM 的薄封装。只负责"读出快照"和"执行 Invoke/SetValue"，
// 匹配/唯一性/诊断规则全在 core（UiaMatch，已测试）。
//
// 线程：构造时在当前线程初始化 COM（MTA），请在同一线程使用并销毁。
// 句柄：快照里的 nativeHandle = (代数 << 32) | 序号。每次新快照代数 +1，旧句柄失效并返回 ElementNotFound，
//       绝不会悄悄操作到别的元素。
#include <cstdint>
#include <memory>

#include "core/CancelToken.h"
#include "core/Interfaces.h"
#include "core/UiaSnapshot.h"

namespace wa::platform {

class UiaSession {
public:
    UiaSession();
    ~UiaSession();
    UiaSession(const UiaSession&) = delete;
    UiaSession& operator=(const UiaSession&) = delete;

    // 初始化失败时返回 false，原因见 initError()
    bool available() const;
    const std::string& initError() const;

    // 读取目标窗口的控件树（控件视图，深度优先）。最多 maxNodes 个，超出则 truncated=true。
    Result<UiaSnapshot> snapshot(std::uint64_t windowHandle, const CancelToken& token, int maxNodes = 5000);

    Status invoke(std::uint64_t nativeHandle);
    Status setValue(std::uint64_t nativeHandle, const std::string& utf8Text);

    // 屏幕坐标处的元素在最近一次快照中的下标；不在快照里（如不属于控件视图）返回 ElementNotFound
    Result<size_t> indexAtPoint(Point screenPoint);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace wa::platform

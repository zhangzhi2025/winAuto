#pragma once
#include <cstdint>
#include <map>
#include <mutex>

#include "core/Interfaces.h"

namespace wa::platform {

// IWindowProvider 的 Win32 实现。本类只负责"枚举候选 + 平台动作"，
// 是否匹配、是否唯一由 core 的 pickUniqueWindow 决定（已有单元测试）。
//
// 枚举规则：只列出可见、未被 DWM 隐藏（cloaked）、非工具窗口、标题非空的顶层窗口。
// 说明：应用的对话框（如"另存为"）是独立的顶层窗口，与主窗口同属一个进程；
//       按进程名匹配会因"不唯一"而被拒绝，需要用 titleContains 区分。
class Win32WindowProvider : public IWindowProvider {
public:
    Win32WindowProvider();

    std::vector<WindowInfo> enumerateWindows() override;
    Result<WindowInfo> findWindow(const WindowSpec& spec) override;
    Status activateWindow(std::uint64_t handle) override;
    bool isWindowValid(std::uint64_t handle) override;
    bool isForeground(std::uint64_t handle) override;
    bool isSelfElevated() override { return selfElevated_; }

private:
    bool sameWindow(std::uint64_t handle);  // 句柄仍然有效，且仍属于记录时的进程（防句柄复用）

    std::uint32_t selfPid_ = 0;
    bool selfElevated_ = false;
    std::mutex mutex_;
    std::map<std::uint64_t, std::uint32_t> knownPid_;  // findWindow 返回过的句柄 -> 进程 ID
};

}  // namespace wa::platform

#pragma once
// 窗口匹配：纯逻辑，不碰平台。平台层只负责"枚举出候选列表"，是否匹配、是否唯一都在这里判定，
// 因此可以在任何环境完整测试。
#include <vector>

#include "core/Interfaces.h"

namespace wa {

struct MatchOptions {
    std::uint32_t excludePid = 0;  // 排除本进程自己的窗口，防止误操作自己的界面
    bool requireVisible = true;
};

// 规则：
//   - processName：忽略大小写；是否带 ".exe" 后缀都能匹配（"notepad" == "Notepad.EXE"）
//   - titleContains：忽略 ASCII 大小写的子串匹配；非 ASCII 字符按字节精确比较
//   - 两个条件都为空 → 不匹配任何窗口（拒绝"匹配所有窗口"）
//   - 两个条件同时给出时必须都满足
std::vector<WindowInfo> matchWindows(const std::vector<WindowInfo>& all, const WindowSpec& spec,
                                     const MatchOptions& options = {});

// 要求恰好一个匹配。0 个或多个都返回 WindowNotFound，多个时 detail 列出候选，便于用户补充 titleContains。
Result<WindowInfo> pickUniqueWindow(const std::vector<WindowInfo>& all, const WindowSpec& spec,
                                    const MatchOptions& options = {});

// 去掉 ".exe" 后缀并转小写（ASCII），用于进程名比较
std::string normalizeProcessName(const std::string& name);

}  // namespace wa

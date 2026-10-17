// 阶段 0 占位，保证 platform_win 目标能在 Windows 上编译链接。
// 平台适配器将在这里实现 core/Interfaces.h 中的接口。
#include <windows.h>

#include "core/Interfaces.h"

namespace wa::platform {

// 返回 true 表示当前进程已提权。后续由 Win32WindowProvider::isSelfElevated 使用。
bool placeholderIsElevated() {
    return false;
}

}  // namespace wa::platform

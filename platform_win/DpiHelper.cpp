#include "DpiHelper.h"

#include <windows.h>

namespace wa::platform {

bool enablePerMonitorDpiAwareness() {
    // SetProcessDpiAwarenessContext 需要 Win10 1703+，动态获取以兼容更旧系统
    using SetContextFn = BOOL(WINAPI*)(DPI_AWARENESS_CONTEXT);
    HMODULE user32 = GetModuleHandleW(L"user32.dll");
    if (user32) {
        auto fn = reinterpret_cast<SetContextFn>(
            reinterpret_cast<void*>(GetProcAddress(user32, "SetProcessDpiAwarenessContext")));
        if (fn) {
            if (fn(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2)) return true;
            // 已经通过清单设置过，系统会返回 ERROR_ACCESS_DENIED，这属于成功
            return GetLastError() == ERROR_ACCESS_DENIED;
        }
    }
    return SetProcessDPIAware() != FALSE;  // 退化为系统级 DPI 感知
}

unsigned dpiForWindow(std::uint64_t hwnd) {
    using GetDpiFn = UINT(WINAPI*)(HWND);
    HMODULE user32 = GetModuleHandleW(L"user32.dll");
    if (!user32) return 96;
    auto fn = reinterpret_cast<GetDpiFn>(
        reinterpret_cast<void*>(GetProcAddress(user32, "GetDpiForWindow")));
    if (!fn) return 96;
    const UINT dpi = fn(reinterpret_cast<HWND>(static_cast<UINT_PTR>(hwnd)));
    return dpi == 0 ? 96u : static_cast<unsigned>(dpi);
}

Rect virtualScreenRect() {
    Rect r;
    r.x = static_cast<int>(GetSystemMetrics(SM_XVIRTUALSCREEN));
    r.y = static_cast<int>(GetSystemMetrics(SM_YVIRTUALSCREEN));
    r.w = static_cast<int>(GetSystemMetrics(SM_CXVIRTUALSCREEN));
    r.h = static_cast<int>(GetSystemMetrics(SM_CYVIRTUALSCREEN));
    return r;
}

}  // namespace wa::platform

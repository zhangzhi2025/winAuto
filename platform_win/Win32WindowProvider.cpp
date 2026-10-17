#include "Win32WindowProvider.h"

#include <windows.h>
#include <dwmapi.h>

#include <map>

#include "Win32Util.h"
#include "core/WindowMatcher.h"

namespace wa::platform {

namespace {

// 判断进程是否以提升权限运行。无法检查时一律按"已提权"处理（保守：宁可拒绝，也不盲目操作）。
bool isProcessElevated(HANDLE process) {
    HANDLE token = nullptr;
    if (!OpenProcessToken(process, TOKEN_QUERY, &token)) {
        // 对更高完整性级别的进程，普通权限打不开令牌（ACCESS_DENIED），正说明它权限更高
        return true;
    }
    TOKEN_ELEVATION elevation{};
    DWORD size = 0;
    const BOOL ok = GetTokenInformation(token, TokenElevation, &elevation, sizeof(elevation), &size);
    CloseHandle(token);
    if (!ok) return true;
    return elevation.TokenIsElevated != 0;
}

struct ProcessInfo {
    std::string name;
    std::string path;
    bool elevated = true;
};

ProcessInfo queryProcess(DWORD pid) {
    ProcessInfo info;
    HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!h) return info;  // 打不开：名称为空，按已提权处理
    wchar_t buf[MAX_PATH * 2];
    DWORD len = static_cast<DWORD>(sizeof(buf) / sizeof(buf[0]));
    if (QueryFullProcessImageNameW(h, 0, buf, &len)) {
        const std::wstring full(buf, len);
        info.path = toUtf8(full);
        const size_t slash = full.find_last_of(L"\\/");
        info.name = toUtf8(slash == std::wstring::npos ? full : full.substr(slash + 1));
    }
    info.elevated = isProcessElevated(h);
    CloseHandle(h);
    return info;
}

bool isCloaked(HWND h) {
    DWORD cloaked = 0;
    if (SUCCEEDED(DwmGetWindowAttribute(h, DWMWA_CLOAKED, &cloaked, sizeof(cloaked)))) {
        return cloaked != 0;
    }
    return false;
}

std::string windowTitle(HWND h) {
    const int len = GetWindowTextLengthW(h);
    if (len <= 0) return {};
    std::wstring w(static_cast<size_t>(len) + 1, L'\0');
    const int got = GetWindowTextW(h, &w[0], len + 1);
    if (got <= 0) return {};
    w.resize(static_cast<size_t>(got));
    return toUtf8(w);
}

struct EnumContext {
    std::vector<WindowInfo>* out;
    std::map<DWORD, ProcessInfo> processCache;  // 同一次枚举内按 PID 缓存，避免反复打开进程
};

BOOL CALLBACK enumProc(HWND h, LPARAM lp) {
    auto* ctx = reinterpret_cast<EnumContext*>(lp);
    if (!IsWindowVisible(h)) return TRUE;
    if (isCloaked(h)) return TRUE;

    const LONG_PTR ex = GetWindowLongPtrW(h, GWL_EXSTYLE);
    if ((ex & WS_EX_TOOLWINDOW) && !(ex & WS_EX_APPWINDOW)) return TRUE;

    std::string title = windowTitle(h);
    if (title.empty()) return TRUE;  // 无标题的多为辅助窗口，列出只会制造"不唯一"

    DWORD pid = 0;
    GetWindowThreadProcessId(h, &pid);
    if (pid == 0) return TRUE;

    auto it = ctx->processCache.find(pid);
    if (it == ctx->processCache.end()) it = ctx->processCache.emplace(pid, queryProcess(pid)).first;

    WindowInfo w;
    w.handle = static_cast<std::uint64_t>(reinterpret_cast<UINT_PTR>(h));
    w.processId = pid;
    w.processName = it->second.name;
    w.processPath = it->second.path;
    w.title = std::move(title);
    w.visible = true;
    w.elevated = it->second.elevated;
    RECT rc{};
    if (GetWindowRect(h, &rc)) {
        w.bounds = Rect{static_cast<int>(rc.left), static_cast<int>(rc.top),
                        static_cast<int>(rc.right - rc.left), static_cast<int>(rc.bottom - rc.top)};
    }
    ctx->out->push_back(std::move(w));
    return TRUE;
}

HWND toHwnd(std::uint64_t handle) { return reinterpret_cast<HWND>(static_cast<UINT_PTR>(handle)); }

}  // namespace

Win32WindowProvider::Win32WindowProvider() {
    selfPid_ = GetCurrentProcessId();
    // 本进程是否已提权：令牌可查询，直接读 TokenElevation
    selfElevated_ = isProcessElevated(GetCurrentProcess());
}

std::vector<WindowInfo> Win32WindowProvider::enumerateWindows() {
    std::vector<WindowInfo> out;
    EnumContext ctx{&out, {}};
    EnumWindows(enumProc, reinterpret_cast<LPARAM>(&ctx));
    return out;
}

Result<WindowInfo> Win32WindowProvider::findWindow(const WindowSpec& spec) {
    MatchOptions options;
    options.excludePid = selfPid_;  // 不把自己的界面当成目标
    Result<WindowInfo> r = pickUniqueWindow(enumerateWindows(), spec, options);
    if (r.ok()) {
        std::lock_guard<std::mutex> lock(mutex_);
        knownPid_[r.value.handle] = r.value.processId;
    }
    return r;
}

bool Win32WindowProvider::sameWindow(std::uint64_t handle) {
    HWND h = toHwnd(handle);
    if (!IsWindow(h)) return false;
    std::uint32_t expected = 0;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = knownPid_.find(handle);
        if (it != knownPid_.end()) expected = it->second;
    }
    if (expected == 0) return true;  // 不是通过 findWindow 得到的句柄，只能做存在性检查
    DWORD pid = 0;
    GetWindowThreadProcessId(h, &pid);
    return pid == expected;  // 句柄被系统回收给其他进程时，PID 会不同
}

bool Win32WindowProvider::isWindowValid(std::uint64_t handle) { return sameWindow(handle); }

Status Win32WindowProvider::activateWindow(std::uint64_t handle) {
    if (!sameWindow(handle)) {
        return Status::failure(ErrCode::WindowNotFound, "window is gone or its handle was reused");
    }
    HWND h = toHwnd(handle);
    if (IsIconic(h)) ShowWindow(h, SW_RESTORE);

    SetForegroundWindow(h);

    // 不假定成功：Windows 的前台锁可能拒绝请求。确认真的到了前台，否则报错，
    // 不使用 AttachThreadInput 之类绕过前台限制的技巧。
    // 这里最多等待 ~500ms；Sleep 不可被取消，但时间很短。
    for (int i = 0; i < 10; ++i) {
        HWND fg = GetForegroundWindow();
        if (fg == h || (fg && GetAncestor(fg, GA_ROOT) == h)) return okStatus();
        Sleep(50);
    }
    return Status::failure(ErrCode::InputBlocked,
                           "Windows refused to bring the window to the foreground");
}

}  // namespace wa::platform

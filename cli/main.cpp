// winauto-cli
//   validate <flow.json>...        校验流程文件
//   windows                        列出可见顶层窗口（仅 Windows）
//   find <process> [title]         按流程里同样的规则查找唯一窗口（仅 Windows）
//   activate <process> [title]     查找并激活窗口，拒绝提权目标（仅 Windows）
//   stop-hotkey-test [hotkey]      验证全局停止热键能否注册并被触发（仅 Windows）
//   run <flow.json>                执行流程（阶段 2 实现）
//
// 退出码：0 成功；1 失败（校验未通过、找不到窗口等）；2 用法错误；3 功能尚未实现
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include "core/Json.h"
#include "core/Validator.h"

#ifdef WINAUTO_HAVE_PLATFORM
#include <windows.h>
#include <shellapi.h>

#include "DpiHelper.h"
#include "Win32StopHotkey.h"
#include "Win32WindowProvider.h"
#include "core/StopHotkey.h"
#include "core/WindowMatcher.h"
#endif

namespace {

// Windows 的 argv 是 ANSI 代码页，中文参数会乱码；这里改用宽字符命令行并转成 UTF-8。
std::vector<std::string> utf8Args(int argc, char** argv) {
    std::vector<std::string> args;
#ifdef WINAUTO_HAVE_PLATFORM
    (void)argc;
    (void)argv;
    int n = 0;
    LPWSTR* wargs = CommandLineToArgvW(GetCommandLineW(), &n);
    if (wargs) {
        for (int i = 0; i < n; ++i) {
            const int len = WideCharToMultiByte(CP_UTF8, 0, wargs[i], -1, nullptr, 0, nullptr, nullptr);
            std::string s(len > 0 ? static_cast<size_t>(len - 1) : 0, '\0');
            if (len > 1) WideCharToMultiByte(CP_UTF8, 0, wargs[i], -1, &s[0], len, nullptr, nullptr);
            args.push_back(s);
        }
        LocalFree(wargs);
        return args;
    }
#endif
    for (int i = 0; i < argc; ++i) args.emplace_back(argv[i]);
    return args;
}

int usage() {
    std::cerr << "usage:\n"
                 "  winauto-cli validate <flow.json>...\n"
#ifdef WINAUTO_HAVE_PLATFORM
                 "  winauto-cli windows\n"
                 "  winauto-cli find <process> [titleContains]\n"
                 "  winauto-cli activate <process> [titleContains]\n"
                 "  winauto-cli stop-hotkey-test [hotkey]\n"
#endif
                 "  winauto-cli run <flow.json>        (available from stage 2)\n";
    return 2;
}

bool readAll(const std::string& path, std::string* out) {
    // 用 u8path 打开，Windows 上带中文的路径才能正确解析
    std::ifstream in(std::filesystem::u8path(path), std::ios::binary);
    if (!in) return false;
    std::ostringstream ss;
    ss << in.rdbuf();
    *out = ss.str();
    // 去掉 UTF-8 BOM（Windows 记事本保存的 JSON 常带）
    if (out->size() >= 3 && static_cast<unsigned char>((*out)[0]) == 0xEF &&
        static_cast<unsigned char>((*out)[1]) == 0xBB && static_cast<unsigned char>((*out)[2]) == 0xBF) {
        out->erase(0, 3);
    }
    return true;
}

int validateFile(const std::string& path) {
    std::string text;
    if (!readAll(path, &text)) {
        std::cerr << path << ": cannot open file\n";
        return 1;
    }
    wa::Result<wa::Workflow> wf = wa::readWorkflow(text);
    if (!wf.ok()) {
        std::cerr << path << ": " << wa::errName(wf.code) << ": " << wf.detail << "\n";
        return 1;
    }
    const wa::ValidationReport report = wa::validateWorkflow(wf.value);
    for (const auto& i : report.issues) {
        std::cout << path << ": " << (i.severity == wa::Severity::Error ? "error" : "warning") << " ["
                  << i.code << "] " << i.path << ": " << i.message
                  << (i.requiresConfirmation ? " (needs confirmation)" : "") << "\n";
    }
    std::cout << path << ": " << (report.ok() ? "OK" : "INVALID") << " (" << report.errorCount()
              << " errors, " << report.warningCount() << " warnings)\n";
    return report.ok() ? 0 : 1;
}

#ifdef WINAUTO_HAVE_PLATFORM

void printWindow(const wa::WindowInfo& w) {
    char hwnd[32];
    std::snprintf(hwnd, sizeof(hwnd), "0x%llX", static_cast<unsigned long long>(w.handle));
    std::cout << hwnd << "\tpid=" << w.processId << "\t" << (w.elevated ? "ELEVATED" : "normal  ") << "\t"
              << (w.processName.empty() ? "(inaccessible)" : w.processName) << "\t\"" << w.title << "\"\n";
}

wa::WindowSpec specFromArgs(const std::vector<std::string>& args) {
    wa::WindowSpec s;
    s.processName = args.size() > 2 ? args[2] : "";
    s.titleContains = args.size() > 3 ? args[3] : "";
    return s;
}

int cmdWindows() {
    wa::platform::Win32WindowProvider provider;
    const auto windows = provider.enumerateWindows();
    for (const auto& w : windows) printWindow(w);
    std::cout << windows.size() << " windows; this process is "
              << (provider.isSelfElevated() ? "ELEVATED" : "not elevated") << "\n";
    return 0;
}

int cmdFind(const std::vector<std::string>& args, bool activate) {
    if (args.size() < 3) return usage();
    wa::platform::Win32WindowProvider provider;
    auto r = provider.findWindow(specFromArgs(args));
    if (!r.ok()) {
        std::cerr << wa::errName(r.code) << ": " << r.detail << "\n";
        return 1;
    }
    printWindow(r.value);
    if (!activate) return 0;

    if (r.value.elevated && !provider.isSelfElevated()) {
        std::cerr << wa::errName(wa::ErrCode::WindowElevated) << ": " << wa::errText(wa::ErrCode::WindowElevated)
                  << "\n";
        return 1;
    }
    wa::Status st = provider.activateWindow(r.value.handle);
    if (!st.ok()) {
        std::cerr << wa::errName(st.code) << ": " << st.detail << "\n";
        return 1;
    }
    std::cout << "activated\n";
    return 0;
}

int cmdStopHotkeyTest(const std::vector<std::string>& args) {
    const std::string text = args.size() > 2 ? args[2] : wa::kDefaultStopHotkey;
    auto spec = wa::parseHotkey(text);
    if (!spec.ok()) {
        std::cerr << spec.detail << "\n";
        return 1;
    }
    wa::platform::Win32StopHotkey hotkey;
    wa::CancelToken token;
    wa::StopHotkeyGuard guard(hotkey, token);
    wa::Status st = guard.arm(spec.value);
    if (!st.ok()) {
        std::cerr << wa::errName(st.code) << ": " << st.detail << "\n";
        return 1;
    }
    std::cout << "Registered " << spec.value.text << ". Press it within 30 seconds (try it while another "
              << "program has focus)...\n";
    const bool timedOut = token.waitFor(std::chrono::seconds(30));
    if (timedOut) {
        std::cout << "Hotkey was NOT triggered.\n";
        return 1;
    }
    std::cout << "Hotkey received: the stop path works.\n";
    return 0;
}

#endif  // WINAUTO_HAVE_PLATFORM

}  // namespace

int main(int argc, char** argv) {
#ifdef WINAUTO_HAVE_PLATFORM
    wa::platform::enablePerMonitorDpiAwareness();  // 必须在创建任何窗口之前
    SetConsoleOutputCP(CP_UTF8);
#endif
    const std::vector<std::string> args = utf8Args(argc, argv);
    if (args.size() < 2) return usage();
    const std::string& cmd = args[1];

    if (cmd == "validate") {
        if (args.size() < 3) return usage();
        int rc = 0;
        for (size_t i = 2; i < args.size(); ++i) {
            if (validateFile(args[i]) != 0) rc = 1;
        }
        return rc;
    }
#ifdef WINAUTO_HAVE_PLATFORM
    if (cmd == "windows") return cmdWindows();
    if (cmd == "find") return cmdFind(args, /*activate=*/false);
    if (cmd == "activate") return cmdFind(args, /*activate=*/true);
    if (cmd == "stop-hotkey-test") return cmdStopHotkeyTest(args);
#endif
    if (cmd == "run") {
        std::cerr << "'run' is not implemented yet: it needs the input adapter (stage 2).\n";
        return 3;
    }
    return usage();
}

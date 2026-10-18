// winauto-cli
//   validate <flow.json>...        校验流程文件
//   windows                        列出可见顶层窗口（仅 Windows）
//   find <process> [title]         按流程里同样的规则查找唯一窗口（仅 Windows）
//   activate <process> [title]     查找并激活窗口，拒绝提权目标（仅 Windows）
//   stop-hotkey-test [hotkey]      验证全局停止热键能否注册并被触发（仅 Windows）
//   run <flow.json> [--yes] [--confirm-risks] [--hotkey CTRL+ALT+F10]
//                                  执行流程（仅 Windows）
//
// 退出码：0 成功；1 失败（校验未通过、执行失败、被拒绝等）；2 用法错误；3 当前平台不支持；4 被用户取消
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
#include "Win32InputController.h"
#include "ImageFile.h"
#include "ScreenCapture.h"
#include "Win32ImageLocator.h"
#include "Win32UiaLocator.h"
#include "UiaSession.h"
#include "Win32StopHotkey.h"
#include "Win32WindowProvider.h"
#include "core/Clock.h"
#include "core/Engine.h"
#include "core/StopHotkey.h"
#include "core/GrayImage.h"
#include "core/Summary.h"
#include "core/TemplateMatch.h"
#include "core/UiaMatch.h"
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
                 "  winauto-cli inspect <process> [titleContains] [--max N]   dump the UI Automation tree\n"
                 "  winauto-cli pick <process> [titleContains] [--delay S]    show the control under the cursor\n"
                 "  winauto-cli shot <process> [titleContains] --out <file.png> [--region x,y,w,h]\n"
                 "                                                            save a template image (region is relative to the window client area)\n"
                 "  winauto-cli find-image <process> [titleContains] <template.png> [--min 0.9] [--region x,y,w,h] [--first]\n"
                 "  winauto-cli stop-hotkey-test [hotkey]\n"
                 "  winauto-cli run <flow.json> [--yes] [--confirm-risks] [--hotkey CTRL+ALT+F10]\n"
#endif
                 ;
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

// 解析 "--name value" 形式的整数选项，并把它和值从参数表里去掉
bool takeIntOption(std::vector<std::string>& args, const std::string& name, int& value) {
    for (size_t i = 0; i + 1 < args.size(); ++i) {
        if (args[i] != name) continue;
        try {
            value = std::stoi(args[i + 1]);
        } catch (...) {
            std::cerr << name << " needs a number\n";
            return false;
        }
        args.erase(args.begin() + static_cast<long>(i), args.begin() + static_cast<long>(i) + 2);
        return true;
    }
    return true;
}

// 找到目标窗口；提权窗口一律拒绝（UIA 读不到，输入也会被 UIPI 丢弃）
bool findTarget(const std::vector<std::string>& args, wa::platform::Win32WindowProvider& provider,
                wa::WindowInfo& out) {
    auto r = provider.findWindow(specFromArgs(args));
    if (!r.ok()) {
        std::cerr << wa::errName(r.code) << ": " << r.detail << "\n";
        return false;
    }
    if (r.value.elevated && !provider.isSelfElevated()) {
        std::cerr << wa::errName(wa::ErrCode::WindowElevated) << ": " << wa::errText(wa::ErrCode::WindowElevated) << "\n";
        return false;
    }
    out = r.value;
    return true;
}

int cmdInspect(std::vector<std::string> args) {
    int maxNodes = 5000;
    if (!takeIntOption(args, "--max", maxNodes) || maxNodes < 1) return usage();
    if (args.size() < 3) return usage();
    wa::platform::Win32WindowProvider provider;
    wa::WindowInfo target;
    if (!findTarget(args, provider, target)) return 1;

    wa::platform::UiaSession session;
    if (!session.available()) {
        std::cerr << "UI Automation unavailable: " << session.initError() << "\n";
        return 1;
    }
    wa::CancelToken token;
    auto snap = session.snapshot(target.handle, token, maxNodes);
    if (!snap.ok()) {
        std::cerr << wa::errName(snap.code) << ": " << snap.detail << "\n";
        return 1;
    }
    std::cout << wa::dumpSnapshot(snap.value, 2000);
    std::cout << snap.value.nodes.size() << " elements\n";
    if (snap.value.nodes.size() <= 1) {
        std::cout << "this window exposes almost no UI Automation elements; use window_point locators for it\n";
    }
    return 0;
}

int cmdPick(std::vector<std::string> args) {
    int delay = 5;
    if (!takeIntOption(args, "--delay", delay) || delay < 0 || delay > 60) return usage();
    if (args.size() < 3) return usage();
    wa::platform::Win32WindowProvider provider;
    wa::WindowInfo target;
    if (!findTarget(args, provider, target)) return 1;

    wa::platform::UiaSession session;
    if (!session.available()) {
        std::cerr << "UI Automation unavailable: " << session.initError() << "\n";
        return 1;
    }
    std::cout << "把鼠标移到目标控件上（不要点击），" << delay << " 秒后取样...\n";
    for (int i = delay; i > 0; --i) {
        std::cout << "  " << i << "\r" << std::flush;
        Sleep(1000);
    }
    POINT cursor{};
    if (!GetCursorPos(&cursor)) {
        std::cerr << "GetCursorPos failed\n";
        return 1;
    }
    wa::CancelToken token;
    auto snap = session.snapshot(target.handle, token);
    if (!snap.ok()) {
        std::cerr << wa::errName(snap.code) << ": " << snap.detail << "\n";
        return 1;
    }
    auto idx = session.indexAtPoint(wa::Point{static_cast<int>(cursor.x), static_cast<int>(cursor.y)});
    if (!idx.ok()) {
        std::cerr << wa::errName(idx.code) << ": " << idx.detail << "\n";
        return 1;
    }
    const wa::UiaNode& n = snap.value.nodes[idx.value];
    std::cout << "鼠标下的控件：[" << n.controlType << "] id=\"" << n.automationId << "\" name=\"" << n.name
              << "\"" << (n.enabled ? "" : " (disabled)") << (n.supportsInvoke ? " invoke" : "")
              << (n.supportsValue ? " value" : "") << "\n";
    auto sug = wa::suggestLocator(snap.value, idx.value);
    if (!sug.ok()) {
        std::cout << "没有唯一的 UIA 定位条件：" << sug.detail << "\n";
        return 1;
    }
    for (const auto& w : sug.value.warnings) std::cout << "注意：" << w << "\n";
    std::cout << "建议的 locator（可直接粘贴进流程）：\n"
              << wa::locatorToJson(wa::Locator{sug.value.locator}) << "\n";
    return 0;
}

bool takeStringOption(std::vector<std::string>& args, const std::string& name, std::string& value) {
    for (size_t i = 0; i + 1 < args.size(); ++i) {
        if (args[i] != name) continue;
        value = args[i + 1];
        args.erase(args.begin() + static_cast<long>(i), args.begin() + static_cast<long>(i) + 2);
        return true;
    }
    return false;
}

bool takeFlag(std::vector<std::string>& args, const std::string& name) {
    for (size_t i = 0; i < args.size(); ++i) {
        if (args[i] == name) {
            args.erase(args.begin() + static_cast<long>(i));
            return true;
        }
    }
    return false;
}

bool parseRegion(const std::string& text, wa::Rect& out) {
    int v[4] = {0, 0, 0, 0};
    size_t pos = 0;
    for (int i = 0; i < 4; ++i) {
        size_t used = 0;
        try {
            v[i] = std::stoi(text.substr(pos), &used);
        } catch (...) {
            return false;
        }
        pos += used;
        if (i < 3) {
            if (pos >= text.size() || text[pos] != ',') return false;
            ++pos;
        }
    }
    if (pos != text.size() || v[2] <= 0 || v[3] <= 0) return false;
    out = wa::Rect{v[0], v[1], v[2], v[3]};
    return true;
}

// 目标窗口 client 区域（屏幕坐标）
bool clientRectOf(std::uint64_t handle, wa::Rect& out) {
    HWND h = reinterpret_cast<HWND>(static_cast<UINT_PTR>(handle));
    RECT cr{};
    POINT origin{0, 0};
    if (!IsWindow(h) || IsIconic(h) || !GetClientRect(h, &cr) || !ClientToScreen(h, &origin)) return false;
    out = wa::Rect{static_cast<int>(origin.x), static_cast<int>(origin.y), static_cast<int>(cr.right - cr.left),
                   static_cast<int>(cr.bottom - cr.top)};
    return true;
}

int cmdShot(std::vector<std::string> args) {
    std::string out, regionText;
    takeStringOption(args, "--out", out);
    const bool hasRegion = takeStringOption(args, "--region", regionText);
    if (out.empty() || args.size() < 3) return usage();
    wa::Rect region;
    if (hasRegion && !parseRegion(regionText, region)) {
        std::cerr << "--region must be x,y,w,h with positive width and height\n";
        return 1;
    }
    wa::platform::Win32WindowProvider provider;
    wa::WindowInfo target;
    if (!findTarget(args, provider, target)) return 1;
    wa::Rect client;
    if (!clientRectOf(target.handle, client)) {
        std::cerr << "cannot read the window area (minimized?). Activate the window first.\n";
        return 1;
    }
    wa::Rect area = client;
    if (hasRegion) area = wa::Rect{client.x + region.x, client.y + region.y, region.w, region.h};
    wa::Rect actual;
    auto shot = wa::platform::captureScreen(area, &actual);
    if (!shot.ok()) {
        std::cerr << wa::errName(shot.code) << ": " << shot.detail << "\n";
        return 1;
    }
    auto saved = wa::platform::savePng(out, shot.value);
    if (!saved.ok()) {
        std::cerr << wa::errName(saved.code) << ": " << saved.detail << "\n";
        return 1;
    }
    std::cout << "saved " << shot.value.w << "x" << shot.value.h << " (window-relative origin " << (actual.x - client.x)
              << "," << (actual.y - client.y) << ") to " << out << "\n"
              << "提示：窗口需在最前且不被遮挡；模板请选结构明显的小图（图标/按钮），不要含时间、动画或渐变。\n";
    return 0;
}

int cmdFindImage(std::vector<std::string> args) {
    std::string regionText, minText;
    const bool hasRegion = takeStringOption(args, "--region", regionText);
    const bool hasMin = takeStringOption(args, "--min", minText);
    const bool first = takeFlag(args, "--first");
    if (args.size() < 4) return usage();
    const std::string templatePath = args.back();
    args.pop_back();

    wa::ImageLocator loc;
    if (hasMin) {
        try {
            loc.minConfidence = std::stod(minText);
        } catch (...) {
            std::cerr << "--min must be a number in (0, 1]\n";
            return 1;
        }
    }
    if (hasRegion) {
        wa::Rect r;
        if (!parseRegion(regionText, r)) {
            std::cerr << "--region must be x,y,w,h with positive width and height\n";
            return 1;
        }
        loc.searchRegion = r;
    }
    if (first) loc.ambiguity = wa::Ambiguity::First;

    // 模板按"相对其所在目录"解析：与流程里的写法一致
    namespace fs = std::filesystem;
    const fs::path tp = fs::u8path(templatePath);
    loc.templatePath = tp.filename().u8string();
    const std::string baseDir = tp.has_parent_path() ? tp.parent_path().u8string() : std::string(".");

    wa::platform::Win32WindowProvider provider;
    wa::WindowInfo target;
    if (!findTarget(args, provider, target)) return 1;

    wa::platform::Win32ImageLocator images(baseDir);
    wa::CancelToken token;
    auto hit = images.search(target.handle, loc, token);
    if (!hit.ok()) {
        std::cerr << wa::errName(hit.code) << ": " << hit.detail << "\n";
        return 1;
    }
    wa::Rect client;
    clientRectOf(target.handle, client);
    const wa::Rect& r = hit.value.screenRect;
    std::cout << "found: score " << hit.value.score << ", screen (" << r.x << "," << r.y << " " << r.w << "x" << r.h
              << "), window-relative (" << (r.x - client.x) << "," << (r.y - client.y) << ")\n";
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

// ---------------------------------------------------------------- run

std::string toUtf8Wide(const wchar_t* w, size_t len) {
    if (len == 0) return {};
    const int n = WideCharToMultiByte(CP_UTF8, 0, w, static_cast<int>(len), nullptr, 0, nullptr, nullptr);
    std::string s(n > 0 ? static_cast<size_t>(n) : 0, '\0');
    if (n > 0) WideCharToMultiByte(CP_UTF8, 0, w, static_cast<int>(len), &s[0], n, nullptr, nullptr);
    return s;
}

// 读取一行控制台输入，不回显（用于运行时询问的敏感文本）。必须是交互式控制台。
wa::Result<std::string> readSecret(const std::string& label) {
    HANDLE in = GetStdHandle(STD_INPUT_HANDLE);
    DWORD mode = 0;
    if (in == INVALID_HANDLE_VALUE || !GetConsoleMode(in, &mode)) {
        return wa::Result<std::string>::failure(
            wa::ErrCode::Unsupported, "a prompted step needs an interactive console (input is redirected)");
    }
    std::cout << label << std::flush;
    SetConsoleMode(in, mode & ~static_cast<DWORD>(ENABLE_ECHO_INPUT));
    wchar_t buf[1024];
    DWORD read = 0;
    const BOOL ok = ReadConsoleW(in, buf, static_cast<DWORD>(sizeof(buf) / sizeof(buf[0])), &read, nullptr);
    SetConsoleMode(in, mode);
    std::cout << "\n";
    if (!ok) {
        SecureZeroMemory(buf, sizeof(buf));
        return wa::Result<std::string>::failure(wa::ErrCode::Cancelled, "console read failed");
    }
    size_t len = read;
    while (len > 0 && (buf[len - 1] == L'\r' || buf[len - 1] == L'\n')) --len;
    std::string text = toUtf8Wide(buf, len);
    SecureZeroMemory(buf, sizeof(buf));
    return wa::Result<std::string>::success(std::move(text));
}

bool askYes(const std::string& question, const char* expected) {
    std::cout << question << " " << std::flush;
    std::string line;
    std::getline(std::cin, line);
    return line == expected;
}

int cmdRun(const std::vector<std::string>& args) {
    std::string path;
    bool yes = false;
    bool confirmRisks = false;
    std::string hotkeyText = wa::kDefaultStopHotkey;
    for (size_t i = 2; i < args.size(); ++i) {
        if (args[i] == "--yes") yes = true;
        else if (args[i] == "--confirm-risks") confirmRisks = true;
        else if (args[i] == "--hotkey" && i + 1 < args.size()) hotkeyText = args[++i];
        else if (!args[i].empty() && args[i][0] == '-') return usage();
        else if (path.empty()) path = args[i];
        else return usage();
    }
    if (path.empty()) return usage();

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
    if (!report.ok()) {
        for (const auto& i : report.issues) {
            if (i.severity == wa::Severity::Error) std::cerr << "error [" << i.code << "] " << i.path << ": " << i.message << "\n";
        }
        std::cerr << "workflow is invalid; nothing was run\n";
        return 1;
    }

    // 1) 信任确认：别人给的流程文件等于一段键鼠脚本，运行前必须让用户看清
    std::cout << wa::summarizeWorkflow(wf.value, report) << "\n";
    if (report.needsConfirmation() && !confirmRisks) {
        if (!askYes("流程包含高风险操作。输入 RISK 确认继续：", "RISK")) {
            std::cerr << "not confirmed; nothing was run\n";
            return 1;
        }
        confirmRisks = true;
    }
    if (!yes && !askYes("输入 yes 开始执行：", "yes")) {
        std::cerr << "not confirmed; nothing was run\n";
        return 1;
    }

    // 2) 没有紧急停止手段就不运行
    auto hotkeySpec = wa::parseHotkey(hotkeyText);
    if (!hotkeySpec.ok()) {
        std::cerr << hotkeySpec.detail << "\n";
        return 1;
    }
    wa::platform::Win32StopHotkey stopHotkey;
    wa::CancelToken token;
    wa::StopHotkeyGuard guard(stopHotkey, token);
    wa::Status armed = guard.arm(hotkeySpec.value);
    if (!armed.ok()) {
        std::cerr << "cannot register the emergency stop hotkey (" << armed.detail
                  << "); refusing to run without it. Use --hotkey to pick another.\n";
        return 1;
    }
    std::cout << "紧急停止：随时按 " << hotkeySpec.value.text << "\n\n";

    // 3) 执行
    wa::platform::Win32WindowProvider windows;
    wa::platform::UiaSession uia;  // 不可用时 uia 定位返回 Unsupported，fallback 会跳过
    // 图像模板相对流程文件所在目录解析
    namespace fs = std::filesystem;
    const fs::path flowDir = fs::absolute(fs::u8path(path)).parent_path();
    wa::platform::Win32ImageLocator images(flowDir.u8string());
    wa::platform::Win32UiaLocator locator(uia, &images);
    wa::platform::Win32InputController input(&uia);
    wa::SteadyClock clock;

    wa::EngineDeps deps;
    deps.windows = &windows;
    deps.locator = &locator;
    deps.input = &input;
    deps.clock = &clock;
    deps.prompt = [](const wa::Step& s) { return readSecret("请输入步骤 " + s.id + " 的内容（不回显）："); };
    deps.observer.onStepStarted = [](const std::string& id) { std::cout << "  " << id << " ... " << std::flush; };
    deps.observer.onStepFinished = [](const wa::StepRecord& r) {
        if (r.code == wa::ErrCode::Ok) {
            std::cout << "ok (" << r.durationMs << " ms)\n";
        } else {
            std::cout << "FAILED " << wa::errName(r.code) << (r.detail.empty() ? "" : ": " + r.detail) << "\n";
        }
    };

    wa::RunOptions options;
    options.confirmedRisks = confirmRisks;
    wa::WorkflowEngine engine(deps);
    const wa::RunReport result = engine.run(wf.value, token, options);
    guard.disarm();

    std::cout << "\n";
    switch (result.state) {
        case wa::ExecutionState::Succeeded:
            std::cout << "完成：全部 " << result.steps.size() << " 步成功\n";
            return 0;
        case wa::ExecutionState::Cancelled:
            std::cout << "已取消" << (result.failedStepId.empty() ? "" : "（在步骤 " + result.failedStepId + "）") << "\n";
            return 4;
        default:
            std::cerr << "失败：" << wa::errName(result.code) << " - " << wa::errText(result.code);
            if (!result.failedStepId.empty()) std::cerr << "（步骤 " << result.failedStepId << "）";
            if (!result.detail.empty()) std::cerr << "\n  " << result.detail;
            std::cerr << "\n";
            return 1;
    }
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
    if (cmd == "inspect") return cmdInspect(args);
    if (cmd == "pick") return cmdPick(args);
    if (cmd == "shot") return cmdShot(args);
    if (cmd == "find-image") return cmdFindImage(args);
    if (cmd == "stop-hotkey-test") return cmdStopHotkeyTest(args);
    if (cmd == "run") return cmdRun(args);
#else
    if (cmd == "run") {
        std::cerr << "'run' is only available on Windows.\n";
        return 3;
    }
#endif
    return usage();
}

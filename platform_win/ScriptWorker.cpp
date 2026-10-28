#include "ScriptWorker.h"

#include <windows.h>

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <string>
#include <vector>

#include "nlohmann/json.hpp"

namespace wa::platform {
namespace {
using json = nlohmann::json;
constexpr size_t kMaxLineBytes = 1u << 20;
constexpr DWORD kCancelGraceMs = 100;
constexpr int kProtocolVersion = 1;

std::wstring widen(const std::string& value) {
    if (value.empty()) return {};
    const int size = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(),
                                         static_cast<int>(value.size()), nullptr, 0);
    if (size <= 0) return {};
    std::wstring result(static_cast<size_t>(size), L'\0');
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()),
                        &result[0], size);
    return result;
}

std::wstring quote(const std::wstring& value) {
    std::wstring out = L"\"";
    size_t slashes = 0;
    for (wchar_t ch : value) {
        if (ch == L'\\') {
            ++slashes;
        } else if (ch == L'\"') {
            out.append(slashes * 2 + 1, L'\\');
            out.push_back(ch);
            slashes = 0;
        } else {
            out.append(slashes, L'\\');
            slashes = 0;
            out.push_back(ch);
        }
    }
    out.append(slashes * 2, L'\\');
    out.push_back(L'\"');
    return out;
}

std::wstring executable(const wchar_t* envName, const wchar_t* fallback) {
    wchar_t configured[32768]{};
    const DWORD size = GetEnvironmentVariableW(envName, configured, static_cast<DWORD>(std::size(configured)));
    if (size > 0 && size < std::size(configured)) return configured;
    wchar_t found[32768]{};
    const DWORD length = SearchPathW(nullptr, fallback, nullptr, static_cast<DWORD>(std::size(found)),
                                     found, nullptr);
    return length > 0 && length < std::size(found) ? std::wstring(found, length) : std::wstring();
}

std::wstring workerPath(const wchar_t* filename) {
    wchar_t configured[32768]{};
    DWORD size = GetEnvironmentVariableW(L"WINAUTO_SCRIPT_WORKER_DIR", configured,
                                         static_cast<DWORD>(std::size(configured)));
    std::wstring directory;
    if (size > 0 && size < std::size(configured)) {
        directory.assign(configured, size);
    } else {
#ifdef WINAUTO_SCRIPT_WORKER_DIR
        directory = widen(WINAUTO_SCRIPT_WORKER_DIR);
#endif
    }
    if (directory.empty()) return {};
    if (directory.back() != L'\\' && directory.back() != L'/') directory.push_back(L'\\');
    directory += filename;
    return directory;
}

struct Process {
    HANDLE process = nullptr;
    HANDLE thread = nullptr;
    HANDLE input = nullptr;
    HANDLE output = nullptr;
    HANDLE job = nullptr;
    ~Process() {
        if (job) CloseHandle(job);
        if (input) CloseHandle(input);
        if (output) CloseHandle(output);
        if (thread) CloseHandle(thread);
        if (process) CloseHandle(process);
    }
    void terminate() {
        if (job) TerminateJobObject(job, 1);
        else if (process) TerminateProcess(process, 1);
    }
};

bool writeAll(HANDLE pipe, const std::string& value) {
    size_t offset = 0;
    while (offset < value.size()) {
        DWORD written = 0;
        const DWORD chunk = static_cast<DWORD>(std::min<size_t>(value.size() - offset, 1u << 16));
        if (!WriteFile(pipe, value.data() + offset, chunk, &written, nullptr) || written == 0) return false;
        offset += written;
    }
    return true;
}

bool sendJson(HANDLE pipe, const json& value) {
    const std::string line = value.dump() + "\n";
    return line.size() <= kMaxLineBytes && writeAll(pipe, line);
}

bool waitForWorkerExit(HANDLE process, std::chrono::steady_clock::time_point deadline) {
    const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(
        deadline - std::chrono::steady_clock::now());
    if (remaining.count() <= 0) return WaitForSingleObject(process, 0) == WAIT_OBJECT_0;
    return WaitForSingleObject(process, static_cast<DWORD>(remaining.count())) == WAIT_OBJECT_0;
}
}  // namespace

Result<ScriptResult> ScriptWorker::run(const ScriptRequest& request, const CancelToken& token,
                                       const ScriptHostHandler& host) {
    const auto deadline = std::chrono::steady_clock::now() +
                          std::chrono::milliseconds(std::max(1, request.timeoutMs));
    const bool python = request.language == ScriptLanguage::Python;
    std::wstring runtime = executable(python ? L"WINAUTO_PYTHON" : L"WINAUTO_LUA",
                                      python ? L"python.exe" : L"lua.exe");
    const std::wstring script = workerPath(python ? L"python_worker.py" : L"lua_worker.lua");
    if (runtime.empty()) {
        return Result<ScriptResult>::failure(ErrCode::Unsupported,
                                             python ? "Python runtime not found; set WINAUTO_PYTHON"
                                                    : "Lua runtime not found; set WINAUTO_LUA");
    }
    if (script.empty()) {
        return Result<ScriptResult>::failure(ErrCode::Unsupported, "script worker files are unavailable");
    }
    SECURITY_ATTRIBUTES security{sizeof(SECURITY_ATTRIBUTES), nullptr, TRUE};
    HANDLE childInputRead = nullptr, parentInputWrite = nullptr;
    HANDLE parentOutputRead = nullptr, childOutputWrite = nullptr;
    if (!CreatePipe(&childInputRead, &parentInputWrite, &security, 0) ||
        !CreatePipe(&parentOutputRead, &childOutputWrite, &security, 0)) {
        if (childInputRead) CloseHandle(childInputRead);
        if (parentInputWrite) CloseHandle(parentInputWrite);
        if (parentOutputRead) CloseHandle(parentOutputRead);
        if (childOutputWrite) CloseHandle(childOutputWrite);
        return Result<ScriptResult>::failure(ErrCode::Unsupported, "could not create script worker pipes");
    }
    SetHandleInformation(parentInputWrite, HANDLE_FLAG_INHERIT, 0);
    SetHandleInformation(parentOutputRead, HANDLE_FLAG_INHERIT, 0);
    HANDLE nullError = CreateFileW(L"NUL", GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, &security,
                                   OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (nullError == INVALID_HANDLE_VALUE) {
        CloseHandle(childInputRead);
        CloseHandle(parentInputWrite);
        CloseHandle(parentOutputRead);
        CloseHandle(childOutputWrite);
        return Result<ScriptResult>::failure(ErrCode::Unsupported, "could not isolate script worker diagnostics");
    }
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    startup.dwFlags = STARTF_USESTDHANDLES;
    startup.hStdInput = childInputRead;
    startup.hStdOutput = childOutputWrite;
    startup.hStdError = nullError;
    PROCESS_INFORMATION processInfo{};
    std::wstring command = quote(runtime) + (python ? L" -u " : L" ") + quote(script);
    std::vector<wchar_t> mutableCommand(command.begin(), command.end());
    mutableCommand.push_back(L'\0');
    const BOOL created = CreateProcessW(runtime.c_str(), mutableCommand.data(), nullptr, nullptr, TRUE,
                                        CREATE_NO_WINDOW | CREATE_UNICODE_ENVIRONMENT, nullptr, nullptr,
                                        &startup, &processInfo);
    CloseHandle(childInputRead);
    CloseHandle(childOutputWrite);
    CloseHandle(nullError);
    if (!created) {
        CloseHandle(parentInputWrite);
        CloseHandle(parentOutputRead);
        return Result<ScriptResult>::failure(ErrCode::Unsupported, "could not start script worker process");
    }

    Process process;
    process.process = processInfo.hProcess;
    process.thread = processInfo.hThread;
    process.input = parentInputWrite;
    process.output = parentOutputRead;
    process.job = CreateJobObjectW(nullptr, nullptr);
    if (!process.job) {
        process.terminate();
        WaitForSingleObject(process.process, INFINITE);
        return Result<ScriptResult>::failure(ErrCode::Unsupported, "could not create script worker job object");
    }
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
    limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    if (!SetInformationJobObject(process.job, JobObjectExtendedLimitInformation, &limits, sizeof(limits)) ||
        !AssignProcessToJobObject(process.job, process.process)) {
        CloseHandle(process.job);
        process.job = nullptr;
        process.terminate();
        WaitForSingleObject(process.process, INFINITE);
        return Result<ScriptResult>::failure(ErrCode::Unsupported, "could not supervise script worker process tree");
    }

    json data;
    try {
        data = json::parse(request.workflowDataJson);
    } catch (const std::exception&) {
        process.terminate();
        WaitForSingleObject(process.process, INFINITE);
        return Result<ScriptResult>::failure(ErrCode::InvalidWorkflow, "invalid workflow data for script worker");
    }
    const json start{{"type", "start"}, {"version", kProtocolVersion},
                     {"language", toString(request.language)}, {"step_id", request.stepId},
                     {"timeout_ms", request.timeoutMs}, {"data", data}, {"code", request.code}};
    if (!sendJson(process.input, start)) {
        process.terminate();
        WaitForSingleObject(process.process, INFINITE);
        return Result<ScriptResult>::failure(ErrCode::Unsupported, "script worker rejected startup message");
    }

    std::string pending;
    size_t totalBytes = 0;
    std::uint64_t expectedRequest = 1;
    for (;;) {
        if (token.isCancelled()) {
            sendJson(process.input, json{{"type", "cancel"}});
            if (WaitForSingleObject(process.process, kCancelGraceMs) == WAIT_TIMEOUT) process.terminate();
            WaitForSingleObject(process.process, INFINITE);
            return Result<ScriptResult>::failure(ErrCode::Cancelled, "script cancelled by user");
        }
        if (std::chrono::steady_clock::now() >= deadline) {
            sendJson(process.input, json{{"type", "cancel"}});
            if (WaitForSingleObject(process.process, kCancelGraceMs) == WAIT_TIMEOUT) process.terminate();
            WaitForSingleObject(process.process, INFINITE);
            return Result<ScriptResult>::failure(ErrCode::Timeout, "script step timed out");
        }
        DWORD available = 0;
        if (!PeekNamedPipe(process.output, nullptr, 0, nullptr, &available, nullptr)) {
            process.terminate();
            WaitForSingleObject(process.process, INFINITE);
            return Result<ScriptResult>::failure(ErrCode::Unsupported, "script worker output pipe failed");
        }
        if (available > 0) {
            char buffer[8192];
            DWORD read = 0;
            if (!ReadFile(process.output, buffer, std::min<DWORD>(available, static_cast<DWORD>(sizeof(buffer))), &read, nullptr) ||
                read == 0) {
                process.terminate();
                WaitForSingleObject(process.process, INFINITE);
                return Result<ScriptResult>::failure(ErrCode::Unsupported, "script worker closed its protocol pipe");
            }
            totalBytes += read;
            if (totalBytes > 8u * kMaxLineBytes) {
                process.terminate();
                WaitForSingleObject(process.process, INFINITE);
                return Result<ScriptResult>::failure(ErrCode::LimitExceeded, "script worker output limit exceeded");
            }
            pending.append(buffer, read);
            if (pending.size() > kMaxLineBytes && pending.find('\n') == std::string::npos) {
                process.terminate();
                WaitForSingleObject(process.process, INFINITE);
                return Result<ScriptResult>::failure(ErrCode::LimitExceeded, "script protocol line too large");
            }
            size_t newline = std::string::npos;
            while ((newline = pending.find('\n')) != std::string::npos) {
                const std::string line = pending.substr(0, newline);
                pending.erase(0, newline + 1);
                json message;
                try {
                    message = json::parse(line);
                } catch (const std::exception&) {
                    process.terminate();
                    WaitForSingleObject(process.process, INFINITE);
                    return Result<ScriptResult>::failure(ErrCode::Unsupported, "script worker sent malformed protocol JSON");
                }
                if (!message.is_object() || !message.contains("type") || !message["type"].is_string()) {
                    process.terminate();
                    WaitForSingleObject(process.process, INFINITE);
                    return Result<ScriptResult>::failure(ErrCode::Unsupported, "script worker sent an invalid protocol message");
                }
                const std::string type = message["type"].get<std::string>();
                if (type == "request") {
                    if (!message.contains("request_id") || !message["request_id"].is_number_unsigned() ||
                        message["request_id"].get<std::uint64_t>() != expectedRequest++ ||
                        !message.contains("operation") || !message["operation"].is_string() ||
                        !message.contains("arguments") || !message["arguments"].is_object()) {
                        process.terminate();
                        WaitForSingleObject(process.process, INFINITE);
                        return Result<ScriptResult>::failure(ErrCode::Unsupported, "invalid script host request");
                    }
                    ScriptHostRequest operation{message["request_id"].get<std::uint64_t>(),
                                                message["operation"].get<std::string>(),
                                                message["arguments"].dump()};
                    Result<ScriptHostResponse> response = host(operation);
                    json reply{{"type", "response"}, {"request_id", operation.requestId}, {"ok", response.ok()}};
                    if (response.ok()) {
                        try {
                            reply["result"] = json::parse(response.value.resultJson);
                        } catch (const std::exception&) {
                            process.terminate();
                            WaitForSingleObject(process.process, INFINITE);
                            return Result<ScriptResult>::failure(ErrCode::Internal, "host returned invalid JSON");
                        }
                    } else {
                        reply["error"] = {{"code", errName(response.code)}, {"diagnostic", response.detail}};
                    }
                    if (!sendJson(process.input, reply)) {
                        process.terminate();
                        WaitForSingleObject(process.process, INFINITE);
                        return Result<ScriptResult>::failure(ErrCode::Unsupported, "could not answer script host request");
                    }
                } else if (type == "complete") {
                    if (!waitForWorkerExit(process.process, deadline)) {
                        process.terminate();
                        WaitForSingleObject(process.process, INFINITE);
                        return Result<ScriptResult>::failure(ErrCode::Timeout,
                                                             "script worker did not exit after completion");
                    }
                    return Result<ScriptResult>::success(ScriptResult{});
                } else if (type == "error") {
                    const std::string diagnostic = message.contains("diagnostic") &&
                                                           message["diagnostic"].is_string()
                                                       ? message["diagnostic"].get<std::string>().substr(0, 512)
                                                       : "script execution failed";
                    if (!waitForWorkerExit(process.process, deadline)) {
                        process.terminate();
                        WaitForSingleObject(process.process, INFINITE);
                        return Result<ScriptResult>::failure(ErrCode::Timeout,
                                                             "script worker did not exit after reporting an error");
                    }
                    return Result<ScriptResult>::failure(ErrCode::Unsupported, diagnostic);
                } else {
                    process.terminate();
                    WaitForSingleObject(process.process, INFINITE);
                    return Result<ScriptResult>::failure(ErrCode::Unsupported, "unexpected script protocol message");
                }
            }
        } else {
            const DWORD processState = WaitForSingleObject(process.process, 0);
            if (processState == WAIT_OBJECT_0) {
                DWORD exitCode = 0;
                GetExitCodeProcess(process.process, &exitCode);
                return Result<ScriptResult>::failure(ErrCode::Unsupported,
                                                     exitCode == 0 ? "script worker exited without completion"
                                                                   : "script worker terminated unexpectedly");
            }
            Sleep(5);
        }
    }
}

}  // namespace wa::platform

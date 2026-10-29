#include "Win32ProcessLauncher.h"

#include <windows.h>

#include <vector>

#include "Win32Util.h"

namespace wa::platform {

namespace {

bool strictWide(const std::string& source, std::wstring* result) {
    if (source.empty()) {
        result->clear();
        return true;
    }
    const int size = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, source.data(),
                                         static_cast<int>(source.size()), nullptr, 0);
    if (size <= 0) return false;
    result->resize(static_cast<size_t>(size));
    return MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, source.data(),
                               static_cast<int>(source.size()), &(*result)[0], size) == size;
}

std::wstring quoteArgument(const std::wstring& argument) {
    std::wstring result(1, L'"');
    size_t slashes = 0;
    for (const wchar_t ch : argument) {
        if (ch == L'\\') {
            ++slashes;
        } else if (ch == L'"') {
            result.append(slashes * 2 + 1, L'\\');
            result.push_back(ch);
            slashes = 0;
        } else {
            result.append(slashes, L'\\');
            slashes = 0;
            result.push_back(ch);
        }
    }
    result.append(slashes * 2, L'\\');
    result.push_back(L'"');
    return result;
}

}  // namespace

Result<std::uint32_t> Win32ProcessLauncher::launch(const std::string& programPath,
                                                   const std::string& arguments) {
    std::wstring application;
    std::wstring extraArguments;
    if (!strictWide(programPath, &application) || !strictWide(arguments, &extraArguments)) {
        return Result<std::uint32_t>::failure(ErrCode::Unsupported, "program path or arguments are not valid UTF-8");
    }
    if (application.empty()) {
        return Result<std::uint32_t>::failure(ErrCode::Unsupported, "program path is empty");
    }

    std::wstring commandLine = quoteArgument(application);
    if (!extraArguments.empty()) {
        commandLine.push_back(L' ');
        commandLine += extraArguments;
    }
    if (commandLine.size() >= 32767) {
        return Result<std::uint32_t>::failure(ErrCode::LimitExceeded, "process command line is too long");
    }
    std::vector<wchar_t> mutableCommand(commandLine.begin(), commandLine.end());
    mutableCommand.push_back(L'\0');

    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION process{};
    if (!CreateProcessW(application.c_str(), mutableCommand.data(), nullptr, nullptr, FALSE, 0, nullptr,
                        nullptr, &startup, &process)) {
        return Result<std::uint32_t>::failure(
            ErrCode::Unsupported, "CreateProcessW failed for \"" + programPath + "\": " +
                                      lastErrorText(GetLastError()));
    }
    const std::uint32_t pid = process.dwProcessId;
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    return Result<std::uint32_t>::success(pid);
}

}  // namespace wa::platform

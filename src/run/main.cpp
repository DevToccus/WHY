// why-run: runs a command exactly as the terminal would, and when it fails keeps the end of what it printed
// so WHY? can show the error next to the crash. Usage: why-run npm run dev
//
// Standard output and input stay connected to the terminal, so colours, prompts and key shortcuts keep working.
// Only standard error passes through WHY?, which shows it as it arrives and remembers the last part.
#include <windows.h>
#include <wil/resource.h>

#include <cstdio>
#include <deque>
#include <filesystem>
#include <fstream>
#include <nlohmann/json.hpp>
#include <string>
#include <thread>

#include "collect/devwatch.h"
#include "core/devtools.h"
#include "core/text.h"
#include "engine/winutil.h"

namespace {

constexpr std::size_t kKeptBytes = 24 * 1024;  // About the last two hundred lines.

// The Windows rule for quoting one argument so the child receives it unchanged.
std::wstring quoteArgument(const std::wstring& argument) {
    if (!argument.empty() && argument.find_first_of(L" \t\"") == std::wstring::npos) return argument;
    std::wstring quoted = L"\"";
    std::size_t backslashes = 0;
    for (const wchar_t c : argument) {
        if (c == L'\\') {
            ++backslashes;
        } else if (c == L'"') {
            quoted.append(backslashes * 2 + 1, L'\\');
            quoted += c;
            backslashes = 0;
        } else {
            quoted.append(backslashes, L'\\');
            quoted += c;
            backslashes = 0;
        }
    }
    quoted.append(backslashes * 2, L'\\');
    return quoted + L"\"";
}

// Colour codes make sense in a terminal, not in a saved record.
std::string stripAnsi(const std::string& text) {
    std::string out;
    out.reserve(text.size());
    for (std::size_t i = 0; i < text.size(); ++i) {
        if (text[i] == '\x1b' && i + 1 < text.size() && text[i + 1] == '[') {
            i += 2;
            while (i < text.size() && !(text[i] >= '@' && text[i] <= '~')) ++i;
            continue;
        }
        if (text[i] != '\r') out += text[i];
    }
    return out;
}

// The program behind a command, for its icon: npm, npx, pnpm and yarn run on Node; pip and uvicorn on Python.
std::wstring runtimeFor(const std::wstring& command) {
    std::wstring tool = why::toWide(why::toLower(why::toUtf8(command)));
    if (const auto slash = tool.find_last_of(L"\\/"); slash != std::wstring::npos) tool = tool.substr(slash + 1);
    for (const wchar_t* extension : {L".exe", L".cmd", L".bat", L".ps1"}) {
        if (tool.size() > wcslen(extension) && tool.ends_with(extension)) tool.resize(tool.size() - wcslen(extension));
    }
    if (tool == L"npm" || tool == L"npx" || tool == L"pnpm" || tool == L"yarn" || tool == L"corepack") tool = L"node";
    if (tool == L"py" || tool == L"pip" || tool == L"uvicorn" || tool == L"flask" || tool == L"django-admin") tool = L"python";
    wchar_t found[MAX_PATH]{};
    return SearchPathW(nullptr, tool.c_str(), L".exe", MAX_PATH, found, nullptr) ? found : L"";
}

// The child receives Ctrl+C itself and decides how to stop; why-run waits for it.
BOOL WINAPI ignoreCtrl(DWORD) { return TRUE; }

}  // namespace

int wmain(int argc, wchar_t** argv) {
    if (argc < 2) {
        fwprintf(stderr, L"Usage: why-run <command> [arguments]\n  For example: why-run npm run dev\n\n"
                         L"Runs the command as usual. If it fails, WHY? keeps the end of its error output.\n");
        return 2;
    }

    std::wstring command;
    for (int i = 1; i < argc; ++i) command += (i > 1 ? L" " : L"") + quoteArgument(argv[i]);
    // cmd.exe finds npm.cmd, npx and other batch launchers the same way the terminal does.
    std::wstring commandLine = L"cmd.exe /d /s /c \"" + command + L"\"";

    SECURITY_ATTRIBUTES inherit{sizeof(inherit), nullptr, TRUE};
    wil::unique_handle readError, writeError;
    if (!CreatePipe(&readError, &writeError, &inherit, 0)) return 1;
    SetHandleInformation(readError.get(), HANDLE_FLAG_INHERIT, 0);

    // Duplicates make sure the terminal's own handles reach the child even when they were not inheritable.
    const auto inheritable = [](DWORD which) {
        HANDLE copy = nullptr;
        DuplicateHandle(GetCurrentProcess(), GetStdHandle(which), GetCurrentProcess(), &copy, 0, TRUE, DUPLICATE_SAME_ACCESS);
        return wil::unique_handle(copy);
    };
    const auto input = inheritable(STD_INPUT_HANDLE);
    const auto output = inheritable(STD_OUTPUT_HANDLE);

    // Standard error is now a pipe; this keeps tools from dropping their colours.
    if (!GetEnvironmentVariableW(L"FORCE_COLOR", nullptr, 0)) SetEnvironmentVariableW(L"FORCE_COLOR", L"1");

    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    startup.dwFlags = STARTF_USESTDHANDLES;
    startup.hStdInput = input.get();
    startup.hStdOutput = output.get();
    startup.hStdError = writeError.get();
    PROCESS_INFORMATION process{};
    const auto started = GetTickCount64();
    const std::int64_t startedMs = why::nowUnixMs();
    if (!CreateProcessW(nullptr, commandLine.data(), nullptr, nullptr, TRUE, 0, nullptr, nullptr, &startup, &process)) {
        fwprintf(stderr, L"why-run: the command could not start (error %lu).\n", GetLastError());
        return 1;
    }
    wil::unique_handle child(process.hProcess);
    wil::unique_handle thread(process.hThread);
    writeError.reset();  // The child holds the only write end, so the pipe closes when it exits.
    SetConsoleCtrlHandler(ignoreCtrl, TRUE);

    std::deque<char> kept;
    std::thread relay([&] {
        const HANDLE terminalError = GetStdHandle(STD_ERROR_HANDLE);
        char buffer[4096];
        DWORD read = 0;
        while (ReadFile(readError.get(), buffer, sizeof(buffer), &read, nullptr) && read > 0) {
            DWORD written = 0;
            WriteFile(terminalError, buffer, read, &written, nullptr);
            kept.insert(kept.end(), buffer, buffer + read);
            while (kept.size() > kKeptBytes) kept.pop_front();
        }
    });

    WaitForSingleObject(child.get(), INFINITE);
    relay.join();
    DWORD code = 0;
    GetExitCodeProcess(child.get(), &code);
    if (why::isDeliberateExit(code)) return static_cast<int>(code);

    // Keep the record where WHY? looks for it, whether or not WHY? is running now.
    std::string text = stripAnsi(std::string(kept.begin(), kept.end()));
    if (kept.size() >= kKeptBytes) {
        const auto firstLine = text.find('\n');
        if (firstLine != std::string::npos) text = text.substr(firstLine + 1);  // Drop the partial first line.
    }
    wchar_t directory[MAX_PATH]{};
    GetCurrentDirectoryW(MAX_PATH, directory);
    std::wstring label;
    for (int i = 1; i < argc; ++i) label += (i > 1 ? L" " : L"") + std::wstring(argv[i]);
    if (label.size() > 60) label = label.substr(0, 57) + L"...";

    const nlohmann::json record{{"timeMs", why::nowUnixMs()},
                                {"command", why::toUtf8(command)},
                                {"label", why::toUtf8(label)},
                                {"image", why::toUtf8(runtimeFor(argv[1]))},
                                {"directory", why::toUtf8(directory)},
                                {"exitCode", code},
                                {"runtimeMs", static_cast<std::int64_t>(GetTickCount64() - started)},
                                {"startedMs", startedMs},
                                {"pid", process.dwProcessId},
                                {"output", why::trim(text)}};
    const std::filesystem::path folder = why::userRunFolder();
    std::error_code ignored;
    std::filesystem::create_directories(folder, ignored);
    const auto file = folder / (std::to_wstring(record["timeMs"].get<std::int64_t>()) + L"-" + std::to_wstring(process.dwProcessId) + L".json");
    std::ofstream(file, std::ios::binary) << record.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace);
    fwprintf(stderr, L"\nwhy-run: exited with code %lu. WHY? kept the error output.\n", code);
    return static_cast<int>(code);
}

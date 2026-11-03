// Entry point: the same executable is the app, the headless collector, the tray notifier and the service.
#include <windows.h>
#include <shellapi.h>
#include <wil/resource.h>

#include <cstdio>
#include <ctime>
#include <fstream>
#include <nlohmann/json.hpp>
#include <optional>
#include <string>
#include <vector>

#include "app/tray.h"
#include "app/window.h"
#include "engine/engine.h"
#include "engine/service.h"
#include "engine/winutil.h"

namespace {

// A WIN32 program has no console of its own. Borrow the caller's so command-line use can print.
void attachConsole() {
    if (AttachConsole(ATTACH_PARENT_PROCESS)) {
        FILE* stream = nullptr;
        freopen_s(&stream, "CONOUT$", "w", stdout);
        freopen_s(&stream, "CONOUT$", "w", stderr);
    }
}

std::wstring defaultReportName() {
    wchar_t date[16]{};
    const std::time_t now = std::time(nullptr);
    std::tm local{};
    localtime_s(&local, &now);
    std::wcsftime(date, 16, L"%Y-%m-%d", &local);
    return std::wstring(L"WHY-report-") + date + L".whyreport.json";
}

int runCollect(const std::vector<std::wstring>& args) {
    attachConsole();
    std::wstring out = defaultReportName();
    int days = 180;
    for (std::size_t i = 1; i + 1 < args.size(); ++i) {
        if (args[i] == L"--out") out = args[i + 1];
        if (args[i] == L"--days") days = _wtoi(args[i + 1].c_str());
    }

    why::EngineOptions options;
    options.historyDays = days > 0 ? days : 180;
    why::Engine engine(options);
    engine.scanOnce();

    const std::string report = why::makeReport(*engine.state());
    std::ofstream file(out, std::ios::binary);
    file << report;
    file.close();
    if (!file) {
        fwprintf(stderr, L"The report could not be written to %s\n", out.c_str());
        return 1;
    }

    const auto parsed = nlohmann::json::parse(report);
    int alerts = 0;
    for (const auto& item : parsed["cases"]) {
        if (!item["alert"].get<std::string>().empty()) ++alerts;
    }
    wprintf(L"Report written to %s\n%zu problems found, %d need a look.\n", out.c_str(), parsed["cases"].size(), alerts);
    if (!why::isElevated()) wprintf(L"Run as administrator to include crash dumps and the file change journal.\n");
    return 0;
}

int runServiceCommand(const std::vector<std::wstring>& args) {
    const std::wstring action = args.size() > 1 ? args[1] : L"";
    std::string error;
    if (action == L"run") {
        error = why::runService();
    } else if (action == L"install") {
        attachConsole();
        error = why::installService();
        if (error.empty()) wprintf(L"Live monitoring is installed and running.\n");
    } else if (action == L"uninstall") {
        attachConsole();
        error = why::uninstallService();
        if (error.empty()) wprintf(L"Live monitoring and its history were removed.\n");
    } else {
        attachConsole();
        error = "Use: why service install | uninstall";
    }
    if (!error.empty()) {
        fprintf(stderr, "%s\n", error.c_str());
        return 1;
    }
    return 0;
}

}  // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int) {
    // Only System32 and the executable's own embedded code: no DLLs from the current folder.
    SetDefaultDllDirectories(LOAD_LIBRARY_SEARCH_SYSTEM32 | LOAD_LIBRARY_SEARCH_USER_DIRS);

    int argc = 0;
    wil::unique_hlocal_ptr<LPWSTR> argv(CommandLineToArgvW(GetCommandLineW(), &argc));
    std::vector<std::wstring> args;
    for (int i = 1; i < argc; ++i) args.emplace_back(argv.get()[i]);

    const std::wstring command = args.empty() ? L"" : args[0];
    try {
        if (command == L"collect") return runCollect(args);
        if (command == L"service") return runServiceCommand(args);
        if (command == L"--tray") return why::app::runTray(instance);
        std::optional<std::wstring> reportPath;
        if (!command.empty() && command[0] != L'-') reportPath = command;
        return why::app::runWindow(instance, reportPath);
    } catch (const std::exception& error) {
        MessageBoxA(nullptr, error.what(), "WHY?", MB_ICONERROR);
        return 1;
    }
}

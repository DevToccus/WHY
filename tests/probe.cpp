// Runs each Windows source against the real machine and reports what it found.
// Usage: why_probe [seconds-to-watch]. Run elevated to include dumps, the journal and the recorder.
#include <windows.h>
#include <dbghelp.h>
#include <psapi.h>

#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <nlohmann/json.hpp>
#include <thread>
#include <wil/com.h>

#include "app/monitor.h"
#include "collect/devwatch.h"
#include "collect/drivers.h"
#include "collect/dumps.h"
#include "collect/etw.h"
#include "collect/eventlog.h"
#include "collect/usn.h"
#include "engine/engine.h"
#include "engine/pipe.h"
#include "engine/winutil.h"

using namespace why;

namespace {

bool g_failed = false;

void check(bool ok, const char* what) {
    printf("  %s %s\n", ok ? "PASS" : "FAIL", what);
    if (!ok) g_failed = true;
}

// The debug engine opens any dump, so one written on the spot proves it works without needing a real crash.
void testDebugEngine() {
    puts("Debug engine, with a dump of this process:");
    const auto path = std::filesystem::temp_directory_path() / L"why_probe.dmp";
    HANDLE file = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, 0, nullptr);
    const BOOL wrote = MiniDumpWriteDump(GetCurrentProcess(), GetCurrentProcessId(), file, MiniDumpNormal, nullptr, nullptr, nullptr);
    CloseHandle(file);
    check(wrote != FALSE, "dump written");

    const auto address = reinterpret_cast<std::uint64_t>(&testDebugEngine);
    const auto hit = findModuleInDump(path.wstring(), address);
    check(hit.has_value(), "dump opened and a module found");
    if (hit) {
        printf("    module=%s evidence=%s\n", hit->module.c_str(), hit->evidence.c_str());
        check(hit->module.find("why_probe") != std::string::npos && hit->evidence == "parameter", "address resolved to this executable");
    }
    std::error_code ignored;
    std::filesystem::remove(path, ignored);
}

void testPipe() {
    puts("Engine over the pipe:");
    Engine engine(EngineOptions{});
    engine.start();
    const wchar_t* name = L"\\\\.\\pipe\\why-probe";
    startPipeServer(engine, name);
    Sleep(200);
    for (int i = 0; i < 300 && engine.revision() == 0; ++i) Sleep(100);

    const auto state = pipeRequest(R"({"cmd":"state"})", name, false);
    check(state.has_value(), "state returned");
    const auto parsed = nlohmann::json::parse(state.value_or("{}"), nullptr, false);
    check(parsed.is_object() && parsed.value("schema", 0) == 1, "state is valid");
    const auto bad = pipeRequest("not json", name, false);
    check(bad.has_value() && bad->find("error") != std::string::npos, "malformed request refused");
    const auto unknown = pipeRequest(R"({"cmd":"case","key":"no-such-case","action":"tried_fix"})", name, false);
    check(unknown.has_value() && unknown->find("false") != std::string::npos, "unknown case refused");
    check(!pipeRequest(R"({"cmd":"state"})", name, true).has_value(), "client refuses a server outside session 0");

    // "I tried a fix" must reach the engine and change the case, end to end.
    if (parsed.is_object() && !parsed["cases"].empty()) {
        const std::string key = parsed["cases"][0]["key"];
        for (int i = 0; i < 300 && nlohmann::json::parse(*engine.state()).value("scanning", true); ++i) Sleep(100);
        const auto rev = engine.revision();
        const auto acted = pipeRequest(nlohmann::json{{"cmd", "case"}, {"key", key}, {"action", "tried_fix"}}.dump(), name, false);
        check(acted.has_value() && acted->find("true") != std::string::npos, "known case accepted");
        engine.waitForChange(rev, 5000);
        const auto after = nlohmann::json::parse(*engine.state());
        std::string status;
        for (const auto& item : after["cases"]) {
            if (item["key"] == key) status = item["status"];
        }
        check(status == "watching", "case is now being watched");
    }
    fflush(stdout);
    engine.stop();
}

}  // namespace

int main(int argc, char** argv) {
    SetConsoleOutputCP(CP_UTF8);
    const int watchSeconds = argc > 1 ? atoi(argv[1]) : 0;
    printf("Elevated: %s\n", isElevated() ? "yes" : "no");

    puts("Event log:");
    const auto started = std::chrono::steady_clock::now();
    const auto history = readEventHistory(180);
    const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - started).count();
    printf("    %zu events in %lld ms, oldest log record %lld. %s\n", history.events.size(), ms, history.oldestLogMs, history.error.c_str());
    check(history.error.empty(), "read without errors");
    check(history.oldestLogMs > 0, "log start found");

    puts("Drivers:");
    const auto drivers = readDrivers();
    int versioned = 0, dated = 0;
    for (const auto& driver : drivers) {
        versioned += !driver.version.empty();
        dated += driver.installedMs > 0;
    }
    printf("    %zu drivers, %d with a version, %d with an install date\n", drivers.size(), versioned, dated);
    check(!drivers.empty(), "inventory found");
    check(versioned > static_cast<int>(drivers.size()) / 2, "most have a version");

    puts("Crash dumps:");
    const auto dumps = listDumps();
    printf("    %zu dumps, access denied: %s\n", dumps.paths.size(), dumps.accessDenied ? "yes" : "no");
    for (const auto& path : dumps.paths) {
        if (const auto info = analyzeDump(path)) printf("    %s code=0x%X module=%s\n", info->fileName.c_str(), info->bugCheck, info->module.c_str());
    }
    testDebugEngine();

    puts("Change journal:");
    const auto usn = readDriverFileChanges(0, 0);
    printf("    available=%s changes=%zu records=%lld journal=%lld MB oldest=%lld (%.1f hours ago) %s\n", usn.available ? "yes" : "no", usn.changes.size(), usn.recordsRead,
           usn.journalBytes / (1024 * 1024), usn.oldestMs, usn.oldestMs ? static_cast<double>(nowUnixMs() - usn.oldestMs) / 3600000.0 : 0.0, usn.error.c_str());
    if (isElevated()) check(usn.available, "readable when elevated");

    puts("Activity recorder:");
    ActivityRecorder recorder;
    const bool recording = recorder.start();
    printf("    running=%s %s\n", recording ? "yes" : "no", recorder.error().c_str());
    if (isElevated()) {
        check(recording, "starts when elevated");
        Sleep(1500);
        STARTUPINFOW si{sizeof(si)};
        PROCESS_INFORMATION pi{};
        wchar_t cmd[] = L"cmd.exe /c exit";
        if (CreateProcessW(nullptr, cmd, nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi)) {
            WaitForSingleObject(pi.hProcess, 3000);
            CloseHandle(pi.hProcess);
            CloseHandle(pi.hThread);
        }
        Sleep(7000);
        const auto records = recorder.snapshot();
        const bool sawCmd = std::any_of(records.begin(), records.end(), [](const ActivityRecord& r) { return r.kind == "process_start" && r.image.find("cmd.exe") != std::string::npos; });
        printf("    %zu records, %llu events seen, running=%s, error='%s', %s\n", records.size(), recorder.eventsSeen(), recorder.running() ? "yes" : "no", recorder.error().c_str(), recorder.sessionStats().c_str());
        for (std::size_t i = 0; i < records.size() && i < 5; ++i) printf("    %s %s\n", records[i].kind.c_str(), records[i].image.c_str());
        check(sawCmd, "saw a process start");
        const bool sawStop = std::any_of(records.begin(), records.end(), [](const ActivityRecord& r) { return r.kind == "process_stop" && r.image.find("cmd.exe") != std::string::npos; });
        check(sawStop, "saw a process stop with a readable name");
    }
    recorder.stop();

    testPipe();

    if (watchSeconds > 0) {
        puts("Resource use while watching:");
        const auto cpuMs = [] {
            FILETIME create, exit, kernel, user;
            GetProcessTimes(GetCurrentProcess(), &create, &exit, &kernel, &user);
            const auto ticks = [](const FILETIME& t) { return (static_cast<std::uint64_t>(t.dwHighDateTime) << 32) | t.dwLowDateTime; };
            return static_cast<double>(ticks(kernel) + ticks(user)) / 10000.0;
        };
        Engine engine(EngineOptions{});
        engine.start();
        for (int i = 0; i < 300 && engine.revision() < 2; ++i) Sleep(100);  // The first scan is a one-off cost, measured apart.
        const double before = cpuMs();
        Sleep(static_cast<DWORD>(watchSeconds) * 1000);
        const double used = cpuMs() - before;
        SYSTEM_INFO system{};
        GetSystemInfo(&system);
        PROCESS_MEMORY_COUNTERS counters{};
        counters.cb = sizeof(counters);
        K32GetProcessMemoryInfo(GetCurrentProcess(), &counters, sizeof(counters));
        printf("    working set %zu MB, peak %zu MB\n", counters.WorkingSetSize / (1024 * 1024), counters.PeakWorkingSetSize / (1024 * 1024));
        printf("    CPU while watching: %.0f ms in %d s, %.3f%% of one core, %.4f%% of %lu logical processors\n", used, watchSeconds,
               used / (watchSeconds * 10.0), used / (watchSeconds * 10.0 * system.dwNumberOfProcessors), system.dwNumberOfProcessors);
        engine.stop();
    }

    puts("Performance monitor");
    {
        const auto com = wil::CoInitializeEx(COINIT_APARTMENTTHREADED);
        why::app::SystemMonitor system;
        why::app::ProcessMonitor processes;
        processes.sample();
        system.sample();
        Sleep(1000);
        const auto stats = system.sample();
        check(stats["cpu"]["cores"].size() > 0, "per-core CPU read");
        check(stats["memory"]["total"].get<std::uint64_t>() > 0, "memory read");
        printf("    cpu %.1f%% at %.0f MHz, %zu cores, %s\n", stats["cpu"]["usage"].get<double>(), stats["cpu"]["currentMhz"].get<double>(),
               stats["cpu"]["cores"].size(), stats["cpu"]["name"].get<std::string>().c_str());
        printf("    gpu %.1f%%, disks %zu, volumes %zu, adapters %zu, processes %d\n", stats["gpu"]["usage"].get<double>(), stats["disks"].size(),
               stats["volumes"].size(), stats["network"].size(), stats["processes"].get<int>());
        const auto list = processes.sample();
        int apps = 0, withIcons = 0;
        std::vector<std::wstring> paths;
        for (const auto& item : list) {
            if (item["group"] == "app") ++apps;
            if (!item["path"].get<std::string>().empty() && paths.size() < 5) paths.push_back(why::toWide(item["path"].get<std::string>()));
        }
        withIcons = static_cast<int>(why::app::programIcons(paths).size());
        check(!list.empty(), "processes listed");
        printf("    %zu processes, %d apps with windows, %d of %zu icons\n", list.size(), apps, withIcons, paths.size());
    }

    puts("Development tools");
    {
        // A Node script that throws, started without why-run: the watcher must see it exit with an error,
        // and must also pick up any why-run records already in this person's folder.
        std::mutex mutex;
        std::vector<why::Event> seen;
        why::DevWatcher devWatcher([&](why::Event event) {
            const std::lock_guard lock(mutex);
            seen.push_back(std::move(event));
        });
        const auto script = std::filesystem::temp_directory_path() / "why-probe-crash.js";
        std::ofstream(script) << "setTimeout(() => { throw new Error('probe'); }, 2500);";
        std::wstring command = L"node \"" + script.wstring() + L"\"";
        STARTUPINFOW startup{sizeof(startup)};
        PROCESS_INFORMATION process{};
        const bool nodeStarted = CreateProcessW(nullptr, command.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &startup, &process);
        check(nodeStarted, "node started");
        if (nodeStarted) {
            WaitForSingleObject(process.hProcess, 10'000);
            CloseHandle(process.hProcess);
            CloseHandle(process.hThread);
        }
        Sleep(2500);
        std::filesystem::remove(script);
        const std::lock_guard lock(mutex);
        const auto exit = std::find_if(seen.begin(), seen.end(), [](const why::Event& e) { return e.eventId == 1; });
        check(exit != seen.end(), "error exit of a dev process seen");
        if (exit != seen.end()) {
            printf("    %s: %s, exit %s, label '%s'\n", exit->signal.c_str(), exit->data.at("CommandLine").c_str(), exit->data.at("ExitCode").c_str(),
                   exit->data.at("Label").c_str());
        }
        printf("    %zu why-run records\n", static_cast<std::size_t>(std::count_if(seen.begin(), seen.end(), [](const why::Event& e) { return e.eventId == 2; })));
    }

    puts("Fixes kept across restarts");
    {
        // The window's engine keeps history in memory; a fix marked in one session must still hold in the next.
        const auto path = std::filesystem::temp_directory_path() / "why-probe-fixes.jsonl";
        std::filesystem::remove(path);
        why::EngineOptions options;
        options.overridesPath = path.wstring();
        const auto firstCase = [](const why::Engine& engine) -> nlohmann::json {
            for (int i = 0; i < 600 && engine.revision() < 2; ++i) Sleep(100);
            const auto state = nlohmann::json::parse(*engine.state());
            return state["cases"].empty() ? nlohmann::json() : state["cases"][0];
        };
        std::string key;
        {
            why::Engine engine(options);
            engine.start();
            const auto item = firstCase(engine);
            if (item.is_object()) {
                key = item["key"].get<std::string>();
                check(engine.applyCaseAction(key, "tried_fix"), "fix accepted");
            }
            engine.stop();
        }
        if (key.empty()) {
            puts("    no case on this PC to mark, skipped");
        } else {
            why::Engine engine(options);
            engine.start();
            for (int i = 0; i < 600 && engine.revision() < 2; ++i) Sleep(100);
            const auto state = nlohmann::json::parse(*engine.state());
            std::string status;
            for (const auto& item : state["cases"]) {
                if (item["key"] == key) status = item["status"].get<std::string>();
            }
            check(status == "watching", "fix still marked after a restart");
            printf("    %s is %s\n", key.c_str(), status.c_str());
            engine.stop();
        }
        std::filesystem::remove(path);
    }

    puts(g_failed ? "SOME CHECKS FAILED" : "ALL CHECKS PASSED");
    fflush(stdout);
    TerminateProcess(GetCurrentProcess(), g_failed ? 1 : 0);  // Pipe threads hold the engine reference until exit.
    return 0;
}

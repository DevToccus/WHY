#include "collect/devwatch.h"

#include <windows.h>
#include <shlobj.h>
#include <tlhelp32.h>
#include <wil/resource.h>
#include <winternl.h>

#include <atomic>
#include <condition_variable>
#include <filesystem>
#include <fstream>
#include <map>
#include <mutex>
#include <nlohmann/json.hpp>
#include <set>
#include <sstream>
#include <thread>

#include "core/devtools.h"
#include "core/signals.h"
#include "core/text.h"
#include "engine/winutil.h"

using nlohmann::json;

namespace why {
namespace {

constexpr auto kPollEvery = std::chrono::milliseconds(1500);
constexpr std::size_t kMaxCommandLine = 600;
constexpr int kAncestorsChecked = 8;

std::int64_t fileTimeToUnixMs(const FILETIME& time) {
    const ULARGE_INTEGER value{time.dwLowDateTime, time.dwHighDateTime};
    return static_cast<std::int64_t>(value.QuadPart / 10'000) - 11'644'473'600'000LL;
}

std::wstring knownFolder(REFKNOWNFOLDERID id) {
    wil::unique_cotaskmem_string path;
    if (FAILED(SHGetKnownFolderPath(id, 0, nullptr, &path))) return L"";
    return path.get();
}

// Reads a UNICODE_STRING that lives in another process.
std::wstring readRemoteString(HANDLE process, const UNICODE_STRING& remote) {
    if (!remote.Buffer || remote.Length == 0) return L"";
    std::wstring text(remote.Length / sizeof(wchar_t), L'\0');
    SIZE_T read = 0;
    if (!ReadProcessMemory(process, remote.Buffer, text.data(), remote.Length, &read)) return L"";
    text.resize(read / sizeof(wchar_t));
    return text;
}

// The command line and working folder of another process, from its process parameters.
// Works for the person's own processes; elevated ones are only readable when WHY? is elevated too.
void readProcessParameters(HANDLE process, std::string& commandLine, std::string& directory) {
    using QueryInformation = NTSTATUS(NTAPI*)(HANDLE, PROCESSINFOCLASS, PVOID, ULONG, PULONG);
    static const auto query = reinterpret_cast<QueryInformation>(GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "NtQueryInformationProcess"));
    if (!query) return;
    PROCESS_BASIC_INFORMATION basic{};
    if (query(process, ProcessBasicInformation, &basic, sizeof(basic), nullptr) != 0 || !basic.PebBaseAddress) return;

    // Offsets in the 64-bit PEB and RTL_USER_PROCESS_PARAMETERS, stable since Windows Vista.
    PVOID parameters = nullptr;
    SIZE_T read = 0;
    if (!ReadProcessMemory(process, reinterpret_cast<BYTE*>(basic.PebBaseAddress) + 0x20, &parameters, sizeof(parameters), &read)) return;
    UNICODE_STRING currentDirectory{};
    UNICODE_STRING command{};
    if (ReadProcessMemory(process, reinterpret_cast<BYTE*>(parameters) + 0x38, &currentDirectory, sizeof(currentDirectory), &read)) {
        directory = toUtf8(readRemoteString(process, currentDirectory));
        while (directory.size() > 3 && (directory.back() == '\\' || directory.back() == '/')) directory.pop_back();
    }
    if (ReadProcessMemory(process, reinterpret_cast<BYTE*>(parameters) + 0x70, &command, sizeof(command), &read)) {
        commandLine = toUtf8(readRemoteString(process, command));
    }
}

std::uint64_t fnv1a(std::string_view text) {
    std::uint64_t hash = 1469598103934665603ULL;
    for (const unsigned char c : text) hash = (hash ^ c) * 1099511628211ULL;
    return hash >> 1;  // Stored as a signed SQLite integer.
}

struct Tracked {
    wil::unique_handle handle;
    std::string image;
    std::string commandLine;
    std::string directory;
    std::int64_t startMs = 0;
};

struct ProcessEntry {
    DWORD parent = 0;
    std::string exe;
};

// Folders where why-run may have left records. The service reads every profile; the app only its own.
std::vector<std::filesystem::path> runFolders() {
    std::vector<std::filesystem::path> folders;
    const std::wstring own = userRunFolder();
    if (!own.empty()) folders.emplace_back(own);
    if (!isElevated()) return folders;
    std::error_code code;
    const std::filesystem::path profiles = knownFolder(FOLDERID_UserProfiles);
    for (const auto& profile : std::filesystem::directory_iterator(profiles, code)) {
        auto folder = profile.path() / L"AppData" / L"Local" / L"WHY" / L"runs";
        if (folder != std::filesystem::path(own) && std::filesystem::is_directory(folder, code)) folders.push_back(std::move(folder));
    }
    return folders;
}

}  // namespace

std::wstring userRunFolder() {
    const std::wstring base = knownFolder(FOLDERID_LocalAppData);
    return base.empty() ? L"" : base + L"\\WHY\\runs";
}

struct DevWatcher::Impl {
    Callback callback;
    std::map<DWORD, Tracked> tracked;
    std::set<DWORD> ignored;
    std::set<std::wstring> seenRecords;
    std::mutex mutex;
    std::condition_variable wake;
    bool stopping = false;
    std::thread worker;

    void emit(Event event) {
        classify(event);
        callback(std::move(event));
    }

    // A process started by why-run is reported by why-run, with its output.
    static bool underWhyRun(DWORD pid, const std::map<DWORD, ProcessEntry>& processes) {
        for (int depth = 0; depth < kAncestorsChecked; ++depth) {
            const auto it = processes.find(pid);
            if (it == processes.end()) return false;
            if (it->second.exe == "why-run.exe") return true;
            if (it->second.parent == pid) return false;
            pid = it->second.parent;
        }
        return false;
    }

    void startTracking(DWORD pid, const std::map<DWORD, ProcessEntry>& processes) {
        wil::unique_handle process(OpenProcess(SYNCHRONIZE | PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, pid));
        const bool full = static_cast<bool>(process);
        if (!full) process.reset(OpenProcess(SYNCHRONIZE | PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid));
        if (!process) {
            ignored.insert(pid);
            return;
        }
        Tracked item;
        wchar_t image[MAX_PATH]{};
        DWORD size = MAX_PATH;
        if (QueryFullProcessImageNameW(process.get(), 0, image, &size)) item.image = toUtf8(image);
        if (full) readProcessParameters(process.get(), item.commandLine, item.directory);
        if (item.commandLine.empty()) item.commandLine = item.image;
        if (isLauncher(item.commandLine) || isOutsideProjects(item.commandLine) || underWhyRun(pid, processes)) {
            ignored.insert(pid);
            return;
        }
        FILETIME created{}, exited{}, kernel{}, user{};
        item.startMs = GetProcessTimes(process.get(), &created, &exited, &kernel, &user) ? fileTimeToUnixMs(created) : nowUnixMs();
        item.handle = std::move(process);
        tracked.emplace(pid, std::move(item));
    }

    void reportExit(DWORD pid, const Tracked& item) {
        DWORD code = 0;
        if (!GetExitCodeProcess(item.handle.get(), &code) || isDeliberateExit(code)) return;
        const std::int64_t now = nowUnixMs();
        std::string commandLine = item.commandLine;
        if (commandLine.size() > kMaxCommandLine) commandLine = commandLine.substr(0, kMaxCommandLine) + "...";

        Event event;
        event.channel = kDevChannel;
        event.provider = kDevProvider;
        event.eventId = kDevExitEvent;
        event.level = 2;
        event.timeMs = now;
        event.recordId = fnv1a(std::to_string(pid) + ":" + std::to_string(item.startMs) + ":" + commandLine);
        event.data = {{"Image", item.image},
                      {"CommandLine", commandLine},
                      {"Directory", item.directory},
                      {"Label", devLabel(item.commandLine)},
                      {"ExitCode", std::to_string(code)},
                      {"RuntimeMs", std::to_string(now - item.startMs)},
                      {"Pid", std::to_string(pid)}};
        emit(std::move(event));
    }

    void scanProcesses() {
        std::map<DWORD, ProcessEntry> processes;
        wil::unique_handle snapshot(CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0));
        if (snapshot && snapshot.get() != INVALID_HANDLE_VALUE) {
            PROCESSENTRY32W entry{};
            entry.dwSize = sizeof(entry);
            for (BOOL more = Process32FirstW(snapshot.get(), &entry); more; more = Process32NextW(snapshot.get(), &entry)) {
                processes[entry.th32ProcessID] = {entry.th32ParentProcessID, toLower(toUtf8(entry.szExeFile))};
            }
        }

        for (auto it = tracked.begin(); it != tracked.end();) {
            if (WaitForSingleObject(it->second.handle.get(), 0) == WAIT_OBJECT_0) {
                reportExit(it->first, it->second);
                it = tracked.erase(it);
            } else {
                ++it;
            }
        }
        // Parents before children: a dev server's own workers belong to it and are not reported again.
        // Children whose parent is not a dev runtime, or is only a launcher such as npm, go first.
        for (const bool children : {false, true}) {
            for (const auto& [pid, entry] : processes) {
                if (pid == 0 || tracked.contains(pid) || ignored.contains(pid) || !isDevRuntime(entry.exe)) continue;
                const auto parent = processes.find(entry.parent);
                const bool devParent = parent != processes.end() && isDevRuntime(parent->second.exe) && !ignored.contains(entry.parent);
                if (devParent != children) continue;
                if (devParent && tracked.contains(entry.parent)) {
                    ignored.insert(pid);
                    continue;
                }
                startTracking(pid, processes);
            }
        }
        // Process IDs are reused, so an ignored ID is forgotten once its process is gone.
        std::erase_if(ignored, [&](DWORD pid) { return !processes.contains(pid); });
    }

    void scanRunRecords() {
        for (const auto& folder : runFolders()) {
            std::error_code code;
            for (const auto& file : std::filesystem::directory_iterator(folder, code)) {
                if (file.path().extension() != L".json" || seenRecords.contains(file.path().wstring())) continue;
                seenRecords.insert(file.path().wstring());
                std::ifstream stream(file.path(), std::ios::binary);
                std::stringstream buffer;
                buffer << stream.rdbuf();
                const auto record = json::parse(buffer.str(), nullptr, false);
                if (!record.is_object() || !record.contains("timeMs")) continue;

                Event event;
                event.channel = kDevChannel;
                event.provider = kDevProvider;
                event.eventId = kDevRunEvent;
                event.level = 2;
                event.timeMs = record.value("timeMs", std::int64_t{0});
                event.recordId = fnv1a(toUtf8(file.path().filename().wstring()));
                event.data = {{"Image", record.value("image", "")},
                              {"CommandLine", record.value("command", "")},
                              {"Directory", record.value("directory", "")},
                              {"Label", record.value("label", "")},
                              {"ExitCode", std::to_string(record.value("exitCode", std::uint32_t{1}))},
                              {"RuntimeMs", std::to_string(record.value("runtimeMs", std::int64_t{0}))},
                              {"Output", record.value("output", "")},
                              {"Pid", std::to_string(record.value("pid", 0))}};
                emit(std::move(event));
            }
        }
    }

    void run() {
        SetThreadPriority(GetCurrentThread(), THREAD_MODE_BACKGROUND_BEGIN);
        std::unique_lock lock(mutex);
        while (!stopping) {
            lock.unlock();
            scanProcesses();
            scanRunRecords();
            lock.lock();
            wake.wait_for(lock, kPollEvery, [&] { return stopping; });
        }
        SetThreadPriority(GetCurrentThread(), THREAD_MODE_BACKGROUND_END);
    }
};

DevWatcher::DevWatcher(Callback callback) : impl_(std::make_unique<Impl>()) {
    impl_->callback = std::move(callback);
    impl_->worker = std::thread([impl = impl_.get()] { impl->run(); });
}

DevWatcher::~DevWatcher() {
    {
        const std::lock_guard lock(impl_->mutex);
        impl_->stopping = true;
    }
    impl_->wake.notify_all();
    if (impl_->worker.joinable()) impl_->worker.join();
}

}  // namespace why

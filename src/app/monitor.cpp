#include "app/monitor.h"

// Winsock must come before windows.h for the TCP table types.
#include <winsock2.h>
#include <ws2ipdef.h>
#include <windows.h>
#include <dwmapi.h>
#include <dxgi.h>
#include <iphlpapi.h>
#include <pdh.h>
#include <pdhmsg.h>
#include <psapi.h>
#include <shellapi.h>
#include <shlwapi.h>
#include <tlhelp32.h>
#include <wil/com.h>
#include <wil/resource.h>
#include <wincodec.h>

#include <algorithm>
#include <map>
#include <mutex>
#include <set>

#include "core/devtools.h"
#include "core/text.h"
#include "engine/winutil.h"

#pragma comment(lib, "dwmapi.lib")
#pragma comment(lib, "pdh.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "iphlpapi.lib")
#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "windowscodecs.lib")

using nlohmann::json;

namespace why::app {
namespace {

// One PDH counter, possibly with a wildcard instance, read as doubles keyed by instance name.
class Counter {
public:
    void add(PDH_HQUERY query, const wchar_t* path) {
        if (PdhAddEnglishCounterW(query, path, 0, &handle_) != ERROR_SUCCESS) handle_ = nullptr;
    }

    std::map<std::wstring, double> values() const {
        std::map<std::wstring, double> out;
        if (!handle_) return out;
        DWORD bytes = 0;
        DWORD count = 0;
        if (PdhGetFormattedCounterArrayW(handle_, PDH_FMT_DOUBLE | PDH_FMT_NOCAP100, &bytes, &count, nullptr) != PDH_MORE_DATA) return out;
        std::vector<std::byte> buffer(bytes);
        auto* items = reinterpret_cast<PDH_FMT_COUNTERVALUE_ITEM_W*>(buffer.data());
        if (PdhGetFormattedCounterArrayW(handle_, PDH_FMT_DOUBLE | PDH_FMT_NOCAP100, &bytes, &count, items) != ERROR_SUCCESS) return out;
        for (DWORD i = 0; i < count; ++i) {
            if (items[i].FmtValue.CStatus == PDH_CSTATUS_VALID_DATA || items[i].FmtValue.CStatus == PDH_CSTATUS_NEW_DATA) {
                out[items[i].szName] += items[i].FmtValue.doubleValue;
            }
        }
        return out;
    }

    double total() const {
        double sum = 0;
        for (const auto& [name, value] : values()) sum += value;
        return sum;
    }

private:
    PDH_HCOUNTER handle_ = nullptr;
};

std::wstring registryString(HKEY root, const wchar_t* key, const wchar_t* name) {
    wchar_t buffer[256]{};
    DWORD size = sizeof(buffer);
    if (RegGetValueW(root, key, name, RRF_RT_REG_SZ, nullptr, buffer, &size) != ERROR_SUCCESS) return L"";
    return buffer;
}

DWORD registryDword(HKEY root, const wchar_t* key, const wchar_t* name) {
    DWORD value = 0;
    DWORD size = sizeof(value);
    RegGetValueW(root, key, name, RRF_RT_REG_DWORD, nullptr, &value, &size);
    return value;
}

int physicalCores() {
    DWORD bytes = 0;
    GetLogicalProcessorInformationEx(RelationProcessorCore, nullptr, &bytes);
    std::vector<std::byte> buffer(bytes);
    auto* info = reinterpret_cast<SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX*>(buffer.data());
    if (!GetLogicalProcessorInformationEx(RelationProcessorCore, info, &bytes)) return 0;
    int cores = 0;
    for (DWORD offset = 0; offset < bytes; ++cores) {
        offset += reinterpret_cast<SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX*>(buffer.data() + offset)->Size;
    }
    return cores;
}

struct GpuInfo {
    std::string name;
    std::uint64_t dedicated = 0;
};

// The graphics adapters DXGI reports, without the software renderer.
std::vector<GpuInfo> graphicsAdapters() {
    std::vector<GpuInfo> adapters;
    wil::com_ptr_nothrow<IDXGIFactory1> factory;
    if (FAILED(CreateDXGIFactory1(IID_PPV_ARGS(factory.put())))) return adapters;
    wil::com_ptr_nothrow<IDXGIAdapter1> adapter;
    for (UINT i = 0; factory->EnumAdapters1(i, adapter.put()) != DXGI_ERROR_NOT_FOUND; ++i) {
        DXGI_ADAPTER_DESC1 desc{};
        if (SUCCEEDED(adapter->GetDesc1(&desc)) && !(desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE)) {
            adapters.push_back({trim(toUtf8(desc.Description)), desc.DedicatedVideoMemory});
        }
    }
    return adapters;
}

// "pid_1234_luid_..._engtype_3D" -> 1234 and "3D".
bool parseGpuEngine(const std::wstring& instance, DWORD& pid, std::wstring& type) {
    if (instance.rfind(L"pid_", 0) != 0) return false;
    pid = static_cast<DWORD>(_wtoi(instance.c_str() + 4));
    const auto at = instance.find(L"engtype_");
    type = at == std::wstring::npos ? L"" : instance.substr(at + 8);
    return true;
}

// Task Manager's GPU figure: the busiest engine type, each type summed over the processes using it.
double busiestEngine(const std::map<std::wstring, double>& engines, std::map<DWORD, double>* perProcess) {
    std::map<std::wstring, double> byType;
    std::map<DWORD, std::map<std::wstring, double>> byProcess;
    for (const auto& [instance, value] : engines) {
        DWORD pid = 0;
        std::wstring type;
        if (!parseGpuEngine(instance, pid, type)) continue;
        byType[type] += value;
        byProcess[pid][type] += value;
    }
    if (perProcess) {
        for (const auto& [pid, types] : byProcess) {
            double busiest = 0;
            for (const auto& [type, value] : types) busiest = std::max(busiest, value);
            (*perProcess)[pid] = std::min(100.0, busiest);
        }
    }
    double busiest = 0;
    for (const auto& [type, value] : byType) busiest = std::max(busiest, value);
    return std::min(100.0, busiest);
}

std::string base64(const std::vector<BYTE>& bytes) {
    static constexpr char kAlphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    out.reserve((bytes.size() + 2) / 3 * 4);
    for (std::size_t i = 0; i < bytes.size(); i += 3) {
        const std::uint32_t chunk = (bytes[i] << 16) | (i + 1 < bytes.size() ? bytes[i + 1] << 8 : 0) | (i + 2 < bytes.size() ? bytes[i + 2] : 0);
        out += kAlphabet[(chunk >> 18) & 63];
        out += kAlphabet[(chunk >> 12) & 63];
        out += i + 1 < bytes.size() ? kAlphabet[(chunk >> 6) & 63] : '=';
        out += i + 2 < bytes.size() ? kAlphabet[chunk & 63] : '=';
    }
    return out;
}

std::string iconAsPng(HICON icon) {
    auto factory = wil::CoCreateInstanceNoThrow<IWICImagingFactory>(CLSID_WICImagingFactory);
    if (!factory) return {};
    wil::com_ptr_nothrow<IWICBitmap> bitmap;
    if (FAILED(factory->CreateBitmapFromHICON(icon, bitmap.put()))) return {};
    wil::com_ptr_nothrow<IStream> stream;
    stream.attach(SHCreateMemStream(nullptr, 0));
    wil::com_ptr_nothrow<IWICBitmapEncoder> encoder;
    if (!stream || FAILED(factory->CreateEncoder(GUID_ContainerFormatPng, nullptr, encoder.put()))) return {};
    if (FAILED(encoder->Initialize(stream.get(), WICBitmapEncoderNoCache))) return {};
    wil::com_ptr_nothrow<IWICBitmapFrameEncode> frame;
    if (FAILED(encoder->CreateNewFrame(frame.put(), nullptr)) || FAILED(frame->Initialize(nullptr))) return {};
    UINT width = 0, height = 0;
    bitmap->GetSize(&width, &height);
    WICPixelFormatGUID format = GUID_WICPixelFormat32bppBGRA;
    frame->SetSize(width, height);
    frame->SetPixelFormat(&format);
    if (FAILED(frame->WriteSource(bitmap.get(), nullptr)) || FAILED(frame->Commit()) || FAILED(encoder->Commit())) return {};
    ULARGE_INTEGER size{};
    if (FAILED(IStream_Size(stream.get(), &size)) || FAILED(IStream_Reset(stream.get()))) return {};
    std::vector<BYTE> bytes(static_cast<std::size_t>(size.QuadPart));
    if (FAILED(IStream_Read(stream.get(), bytes.data(), static_cast<ULONG>(bytes.size())))) return {};
    return "data:image/png;base64," + base64(bytes);
}

// Processes that own a visible, uncloaked top-level window with a title: what Task Manager lists as Apps.
std::set<DWORD> processesWithWindows() {
    std::set<DWORD> pids;
    EnumWindows(
        [](HWND window, LPARAM param) -> BOOL {
            if (!IsWindowVisible(window) || GetWindow(window, GW_OWNER) || GetWindowTextLengthW(window) == 0) return TRUE;
            if (GetWindowLongPtrW(window, GWL_EXSTYLE) & WS_EX_TOOLWINDOW) return TRUE;
            BOOL cloaked = FALSE;
            DwmGetWindowAttribute(window, DWMWA_CLOAKED, &cloaked, sizeof(cloaked));
            if (cloaked) return TRUE;
            DWORD pid = 0;
            GetWindowThreadProcessId(window, &pid);
            reinterpret_cast<std::set<DWORD>*>(param)->insert(pid);
            return TRUE;
        },
        reinterpret_cast<LPARAM>(&pids));
    return pids;
}

// TCP ports each process listens on, so a dev server shows where it can be opened: localhost:3000.
std::map<DWORD, std::set<int>> listeningPorts() {
    std::map<DWORD, std::set<int>> ports;
    for (const ULONG family : {AF_INET, AF_INET6}) {
        DWORD size = 0;
        GetExtendedTcpTable(nullptr, &size, FALSE, family, TCP_TABLE_OWNER_PID_LISTENER, 0);
        std::vector<std::byte> buffer(size);
        if (GetExtendedTcpTable(buffer.data(), &size, FALSE, family, TCP_TABLE_OWNER_PID_LISTENER, 0) != NO_ERROR) continue;
        if (family == AF_INET) {
            const auto* table = reinterpret_cast<const MIB_TCPTABLE_OWNER_PID*>(buffer.data());
            for (DWORD i = 0; i < table->dwNumEntries; ++i) ports[table->table[i].dwOwningPid].insert(ntohs(static_cast<u_short>(table->table[i].dwLocalPort)));
        } else {
            const auto* table = reinterpret_cast<const MIB_TCP6TABLE_OWNER_PID*>(buffer.data());
            for (DWORD i = 0; i < table->dwNumEntries; ++i) ports[table->table[i].dwOwningPid].insert(ntohs(static_cast<u_short>(table->table[i].dwLocalPort)));
        }
    }
    return ports;
}

std::string windowsDirectory() {
    wchar_t buffer[MAX_PATH]{};
    GetWindowsDirectoryW(buffer, MAX_PATH);
    return toLower(toUtf8(buffer)) + "\\";
}

}  // namespace

// ---- System ----

struct SystemMonitor::Impl {
    std::mutex mutex;
    PDH_HQUERY query = nullptr;
    Counter cpuTotal, cpuCores, cpuPerformance, diskIdle, diskRead, diskWrite, netReceived, netSent, netBandwidth, gpuEngines, gpuMemory;
    std::string cpuName;
    double baseMhz = 0;
    int logical = 0;
    int cores = 0;
    std::vector<GpuInfo> gpus;

    Impl() {
        cpuName = trim(toUtf8(registryString(HKEY_LOCAL_MACHINE, L"HARDWARE\\DESCRIPTION\\System\\CentralProcessor\\0", L"ProcessorNameString")));
        baseMhz = registryDword(HKEY_LOCAL_MACHINE, L"HARDWARE\\DESCRIPTION\\System\\CentralProcessor\\0", L"~MHz");
        SYSTEM_INFO system{};
        GetSystemInfo(&system);
        logical = static_cast<int>(system.dwNumberOfProcessors);
        cores = physicalCores();
        gpus = graphicsAdapters();

        if (PdhOpenQueryW(nullptr, 0, &query) != ERROR_SUCCESS) return;
        // "Processor Utility" is what Task Manager shows; "% Processor Time" reads lower on modern CPUs.
        cpuTotal.add(query, L"\\Processor Information(_Total)\\% Processor Utility");
        cpuCores.add(query, L"\\Processor Information(*)\\% Processor Utility");
        cpuPerformance.add(query, L"\\Processor Information(_Total)\\% Processor Performance");
        diskIdle.add(query, L"\\PhysicalDisk(*)\\% Idle Time");
        diskRead.add(query, L"\\PhysicalDisk(*)\\Disk Read Bytes/sec");
        diskWrite.add(query, L"\\PhysicalDisk(*)\\Disk Write Bytes/sec");
        netReceived.add(query, L"\\Network Interface(*)\\Bytes Received/sec");
        netSent.add(query, L"\\Network Interface(*)\\Bytes Sent/sec");
        netBandwidth.add(query, L"\\Network Interface(*)\\Current Bandwidth");
        gpuEngines.add(query, L"\\GPU Engine(*)\\Utilization Percentage");
        gpuMemory.add(query, L"\\GPU Adapter Memory(*)\\Dedicated Usage");
        PdhCollectQueryData(query);
    }

    ~Impl() {
        if (query) PdhCloseQuery(query);
    }
};

SystemMonitor::SystemMonitor() : impl_(std::make_unique<Impl>()) {}
SystemMonitor::~SystemMonitor() = default;

json SystemMonitor::sample() {
    auto& m = *impl_;
    const std::lock_guard lock(m.mutex);
    if (m.query) PdhCollectQueryData(m.query);

    // CPU
    json cores = json::array();
    for (const auto& [name, value] : m.cpuCores.values()) {
        if (name.find(L"_Total") == std::wstring::npos) cores.push_back(std::min(100.0, value));
    }
    const double performance = m.cpuPerformance.total();
    json cpu{{"name", m.cpuName},
             {"usage", std::min(100.0, m.cpuTotal.total())},
             {"cores", cores},
             {"logical", m.logical},
             {"physical", m.cores},
             {"baseMhz", m.baseMhz},
             {"currentMhz", performance > 0 ? m.baseMhz * performance / 100.0 : m.baseMhz}};

    // Memory
    MEMORYSTATUSEX memory{sizeof(memory)};
    GlobalMemoryStatusEx(&memory);
    PERFORMANCE_INFORMATION perf{sizeof(perf)};
    GetPerformanceInfo(&perf, sizeof(perf));
    const auto pages = [&](SIZE_T count) { return static_cast<std::uint64_t>(count) * perf.PageSize; };
    json mem{{"total", memory.ullTotalPhys},
             {"available", memory.ullAvailPhys},
             {"used", memory.ullTotalPhys - memory.ullAvailPhys},
             {"load", memory.dwMemoryLoad},
             {"committed", pages(perf.CommitTotal)},
             {"commitLimit", pages(perf.CommitLimit)},
             {"cached", pages(perf.SystemCache)}};

    // GPU
    const double gpuUsage = busiestEngine(m.gpuEngines.values(), nullptr);
    json gpus = json::array();
    for (const auto& gpu : m.gpus) gpus.push_back({{"name", gpu.name}, {"dedicated", gpu.dedicated}});
    json gpu{{"usage", gpuUsage}, {"adapters", gpus}, {"memoryUsed", m.gpuMemory.total()}};

    // Disks: activity per physical disk, space per volume.
    const auto idle = m.diskIdle.values();
    const auto reads = m.diskRead.values();
    const auto writes = m.diskWrite.values();
    json disks = json::array();
    for (const auto& [name, value] : idle) {
        if (name == L"_Total") continue;
        disks.push_back({{"name", toUtf8(name)},
                         {"active", std::clamp(100.0 - value, 0.0, 100.0)},
                         {"read", reads.contains(name) ? reads.at(name) : 0.0},
                         {"write", writes.contains(name) ? writes.at(name) : 0.0}});
    }
    json volumes = json::array();
    wchar_t drives[512]{};
    GetLogicalDriveStringsW(ARRAYSIZE(drives), drives);
    for (const wchar_t* root = drives; *root; root += wcslen(root) + 1) {
        const UINT type = GetDriveTypeW(root);
        if (type != DRIVE_FIXED && type != DRIVE_REMOVABLE) continue;
        ULARGE_INTEGER free{}, total{};
        if (!GetDiskFreeSpaceExW(root, nullptr, &total, &free)) continue;
        wchar_t label[MAX_PATH]{};
        wchar_t fileSystem[MAX_PATH]{};
        GetVolumeInformationW(root, label, MAX_PATH, nullptr, nullptr, nullptr, fileSystem, MAX_PATH);
        volumes.push_back({{"root", toUtf8(root)}, {"label", toUtf8(label)}, {"fileSystem", toUtf8(fileSystem)},
                           {"total", total.QuadPart}, {"free", free.QuadPart}, {"removable", type == DRIVE_REMOVABLE}});
    }

    // Network: adapters that have a link, busiest first.
    const auto received = m.netReceived.values();
    const auto sent = m.netSent.values();
    json adapters = json::array();
    for (const auto& [name, bandwidth] : m.netBandwidth.values()) {
        if (bandwidth <= 0) continue;
        adapters.push_back({{"name", toUtf8(name)},
                            {"bandwidth", bandwidth},
                            {"received", received.contains(name) ? received.at(name) : 0.0},
                            {"sent", sent.contains(name) ? sent.at(name) : 0.0}});
    }
    std::sort(adapters.begin(), adapters.end(), [](const json& a, const json& b) {
        return a.value("received", 0.0) + a.value("sent", 0.0) > b.value("received", 0.0) + b.value("sent", 0.0);
    });

    // Power
    SYSTEM_POWER_STATUS power{};
    GetSystemPowerStatus(&power);
    const bool battery = power.BatteryFlag != 128 && power.BatteryFlag != 255;
    json powerJson{{"battery", battery},
                   {"percent", battery && power.BatteryLifePercent <= 100 ? power.BatteryLifePercent : -1},
                   {"pluggedIn", power.ACLineStatus == 1}};

    return {{"timeMs", nowUnixMs()},
            {"uptimeMs", GetTickCount64()},
            {"cpu", cpu},
            {"memory", mem},
            {"gpu", gpu},
            {"disks", disks},
            {"volumes", volumes},
            {"network", adapters},
            {"power", powerJson},
            {"processes", perf.ProcessCount},
            {"threads", perf.ThreadCount},
            {"handles", perf.HandleCount}};
}

// ---- Processes ----

struct ProcessMonitor::Impl {
    std::mutex mutex;
    PDH_HQUERY query = nullptr;
    Counter gpuEngines;
    struct Previous {
        ULONGLONG cpu = 0;
        ULONGLONG io = 0;
    };
    std::map<DWORD, Previous> previous;
    ULONGLONG previousMs = 0;
    std::string windowsDir = windowsDirectory();

    Impl() {
        if (PdhOpenQueryW(nullptr, 0, &query) != ERROR_SUCCESS) return;
        gpuEngines.add(query, L"\\GPU Engine(*)\\Utilization Percentage");
        PdhCollectQueryData(query);
    }
    ~Impl() {
        if (query) PdhCloseQuery(query);
    }
};

ProcessMonitor::ProcessMonitor() : impl_(std::make_unique<Impl>()) {}
ProcessMonitor::~ProcessMonitor() = default;

json ProcessMonitor::sample() {
    auto& m = *impl_;
    const std::lock_guard lock(m.mutex);
    const ULONGLONG nowMs = GetTickCount64();
    const double elapsedMs = m.previousMs ? static_cast<double>(nowMs - m.previousMs) : 0.0;
    SYSTEM_INFO system{};
    GetSystemInfo(&system);
    const double logical = system.dwNumberOfProcessors ? system.dwNumberOfProcessors : 1;

    std::map<DWORD, double> gpuByProcess;
    if (m.query && PdhCollectQueryData(m.query) == ERROR_SUCCESS) busiestEngine(m.gpuEngines.values(), &gpuByProcess);
    const auto withWindows = processesWithWindows();
    const auto ports = listeningPorts();

    std::map<DWORD, Impl::Previous> current;
    json list = json::array();
    wil::unique_handle snapshot(CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0));
    if (!snapshot || snapshot.get() == INVALID_HANDLE_VALUE) return list;
    PROCESSENTRY32W entry{};
    entry.dwSize = sizeof(entry);
    for (BOOL more = Process32FirstW(snapshot.get(), &entry); more; more = Process32NextW(snapshot.get(), &entry)) {
        const DWORD pid = entry.th32ProcessID;
        if (pid == 0) continue;
        json item{{"pid", pid}, {"parent", entry.th32ParentProcessID}, {"name", toUtf8(entry.szExeFile)}, {"threads", entry.cntThreads},
                  {"path", ""}, {"memory", 0}, {"cpu", 0.0}, {"disk", 0.0},
                  {"gpu", gpuByProcess.contains(pid) ? gpuByProcess.at(pid) : 0.0}};

        wil::unique_handle process(OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid));
        std::string path;
        if (process) {
            wchar_t image[MAX_PATH]{};
            DWORD size = MAX_PATH;
            if (QueryFullProcessImageNameW(process.get(), 0, image, &size)) path = toUtf8(image);
            // Task Manager shows the private working set; the plain working set counts shared pages too.
            PROCESS_MEMORY_COUNTERS_EX memory{};
            if (GetProcessMemoryInfo(process.get(), reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&memory), sizeof(memory))) {
                item["memory"] = memory.PrivateUsage;
            }
            Impl::Previous now;
            FILETIME created{}, exited{}, kernel{}, user{};
            if (GetProcessTimes(process.get(), &created, &exited, &kernel, &user)) {
                now.cpu = ULARGE_INTEGER{kernel.dwLowDateTime, kernel.dwHighDateTime}.QuadPart +
                          ULARGE_INTEGER{user.dwLowDateTime, user.dwHighDateTime}.QuadPart;
            }
            IO_COUNTERS io{};
            if (GetProcessIoCounters(process.get(), &io)) now.io = io.ReadTransferCount + io.WriteTransferCount;
            if (const auto before = m.previous.find(pid); elapsedMs > 0 && before != m.previous.end()) {
                // Process times count 100 ns units; elapsed is in milliseconds.
                if (now.cpu >= before->second.cpu) item["cpu"] = (now.cpu - before->second.cpu) / 10'000.0 / elapsedMs / logical * 100.0;
                if (now.io >= before->second.io) item["disk"] = (now.io - before->second.io) * 1000.0 / elapsedMs;
            }
            current[pid] = now;
        }
        item["path"] = path;
        item["ports"] = ports.contains(pid) ? json(ports.at(pid)) : json::array();
        const bool windows = path.empty() || toLower(path).starts_with(m.windowsDir);
        // Development tools come first in WHY?: they are what it watches for crashes when you build something.
        item["group"] = isDevRuntime(toUtf8(entry.szExeFile)) ? "dev" : withWindows.contains(pid) ? "app" : windows ? "windows" : "background";
        list.push_back(std::move(item));
    }
    m.previous = std::move(current);
    m.previousMs = nowMs;
    return list;
}

json programIcons(const std::vector<std::wstring>& paths) {
    json icons = json::object();
    for (const auto& path : paths) {
        SHFILEINFOW info{};
        if (!SHGetFileInfoW(path.c_str(), 0, &info, sizeof(info), SHGFI_ICON | SHGFI_LARGEICON) || !info.hIcon) continue;
        wil::unique_hicon icon(info.hIcon);
        if (auto png = iconAsPng(icon.get()); !png.empty()) icons[toUtf8(path)] = std::move(png);
    }
    return icons;
}

}  // namespace why::app

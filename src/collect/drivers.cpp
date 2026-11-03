#include "collect/drivers.h"

#include <windows.h>
#include <initguid.h>
#include <devpkey.h>
#include <setupapi.h>
#include <wil/resource.h>

#include <map>
#include <string>
#include <vector>

#include "core/text.h"
#include "engine/winutil.h"

namespace why {
namespace {

struct DeviceFacts {
    std::string name;
    std::string deviceClass;
    std::string provider;
    std::string version;
    std::string date;
};

std::wstring deviceString(HDEVINFO set, SP_DEVINFO_DATA& device, const DEVPROPKEY& key) {
    wchar_t buffer[512]{};
    DEVPROPTYPE type = 0;
    if (!SetupDiGetDevicePropertyW(set, &device, &key, &type, reinterpret_cast<PBYTE>(buffer), sizeof(buffer) - sizeof(wchar_t), nullptr, 0)) {
        return {};
    }
    return type == DEVPROP_TYPE_STRING ? std::wstring(buffer) : std::wstring{};
}

std::string deviceDate(HDEVINFO set, SP_DEVINFO_DATA& device) {
    FILETIME value{};
    DEVPROPTYPE type = 0;
    if (!SetupDiGetDevicePropertyW(set, &device, &DEVPKEY_Device_DriverDate, &type, reinterpret_cast<PBYTE>(&value), sizeof(value), nullptr, 0) ||
        type != DEVPROP_TYPE_FILETIME) {
        return {};
    }
    SYSTEMTIME st{};
    if (!FileTimeToSystemTime(&value, &st)) return {};
    char text[16];
    snprintf(text, sizeof(text), "%04d-%02d-%02d", st.wYear, st.wMonth, st.wDay);
    return text;
}

// Present devices, by the driver service that runs them. They give the friendly name and the driver version.
std::map<std::string, DeviceFacts> readDevices() {
    std::map<std::string, DeviceFacts> devices;
    const HDEVINFO set = SetupDiGetClassDevsW(nullptr, nullptr, nullptr, DIGCF_ALLCLASSES | DIGCF_PRESENT);
    if (set == INVALID_HANDLE_VALUE) return devices;
    const auto destroy = wil::scope_exit([&] { SetupDiDestroyDeviceInfoList(set); });

    SP_DEVINFO_DATA device{};
    device.cbSize = sizeof(device);
    for (DWORD index = 0; SetupDiEnumDeviceInfo(set, index, &device); ++index) {
        const std::string service = toLower(toUtf8(deviceString(set, device, DEVPKEY_Device_Service)));
        if (service.empty() || devices.contains(service)) continue;
        DeviceFacts facts;
        facts.name = toUtf8(deviceString(set, device, DEVPKEY_Device_FriendlyName));
        if (facts.name.empty()) facts.name = toUtf8(deviceString(set, device, DEVPKEY_Device_DeviceDesc));
        facts.deviceClass = toUtf8(deviceString(set, device, DEVPKEY_Device_Class));
        facts.provider = toUtf8(deviceString(set, device, DEVPKEY_Device_DriverProvider));
        facts.version = toUtf8(deviceString(set, device, DEVPKEY_Device_DriverVersion));
        facts.date = deviceDate(set, device);
        devices.emplace(service, std::move(facts));
    }
    return devices;
}

// Service image paths come in several forms: \SystemRoot\..., System32\..., \??\C:\...
std::wstring resolveImagePath(std::wstring path, const std::wstring& windowsDir) {
    if (path.size() >= 2 && path.front() == L'"') {
        const auto end = path.find(L'"', 1);
        path = path.substr(1, end == std::wstring::npos ? std::wstring::npos : end - 1);
    }
    constexpr std::wstring_view kRoot = L"\\SystemRoot\\";
    constexpr std::wstring_view kNt = L"\\??\\";
    if (_wcsnicmp(path.c_str(), kRoot.data(), kRoot.size()) == 0) return windowsDir + L"\\" + path.substr(kRoot.size());
    if (path.compare(0, kNt.size(), kNt) == 0) return path.substr(kNt.size());
    if (path.size() > 1 && path[1] == L':') return path;
    return windowsDir + L"\\" + path;
}

std::wstring fileVersionValue(const std::vector<BYTE>& block, const wchar_t* name) {
    struct Translation {
        WORD language;
        WORD codePage;
    };
    Translation* translation = nullptr;
    UINT size = 0;
    if (!VerQueryValueW(block.data(), L"\\VarFileInfo\\Translation", reinterpret_cast<void**>(&translation), &size) || size < sizeof(Translation)) {
        return {};
    }
    wchar_t query[128];
    swprintf_s(query, L"\\StringFileInfo\\%04x%04x\\%s", translation->language, translation->codePage, name);
    wchar_t* value = nullptr;
    if (!VerQueryValueW(block.data(), query, reinterpret_cast<void**>(&value), &size) || size == 0) return {};
    return value;
}

void readFileFacts(const std::wstring& path, DriverInfo& driver) {
    DWORD ignored = 0;
    const DWORD size = GetFileVersionInfoSizeW(path.c_str(), &ignored);
    if (size == 0) return;
    std::vector<BYTE> block(size);
    if (!GetFileVersionInfoW(path.c_str(), 0, size, block.data())) return;
    if (driver.provider.empty()) driver.provider = toUtf8(fileVersionValue(block, L"CompanyName"));
    if (driver.version.empty()) {
        VS_FIXEDFILEINFO* fixed = nullptr;
        UINT length = 0;
        if (VerQueryValueW(block.data(), L"\\", reinterpret_cast<void**>(&fixed), &length) && fixed) {
            driver.version = std::to_string(HIWORD(fixed->dwFileVersionMS)) + "." + std::to_string(LOWORD(fixed->dwFileVersionMS)) + "." +
                             std::to_string(HIWORD(fixed->dwFileVersionLS)) + "." + std::to_string(LOWORD(fixed->dwFileVersionLS));
        }
    }
}

std::int64_t creationTimeMs(const std::wstring& path) {
    WIN32_FILE_ATTRIBUTE_DATA data{};
    if (!GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &data)) return 0;
    return fileTimeToUnixMs((static_cast<std::uint64_t>(data.ftCreationTime.dwHighDateTime) << 32) | data.ftCreationTime.dwLowDateTime);
}

// Each installed driver package gets its own folder in the driver store, created at install time.
std::int64_t estimateInstallTime(const std::wstring& path) {
    constexpr std::wstring_view kStore = L"\\driverstore\\filerepository\\";
    std::wstring lower = path;
    CharLowerBuffW(lower.data(), static_cast<DWORD>(lower.size()));
    if (const auto pos = lower.find(kStore); pos != std::wstring::npos) {
        const auto end = path.find(L'\\', pos + kStore.size());
        if (end != std::wstring::npos) return creationTimeMs(path.substr(0, end));
    }
    return creationTimeMs(path);
}

}  // namespace

std::vector<DriverInfo> readDrivers() {
    std::vector<DriverInfo> drivers;
    wil::unique_schandle manager(OpenSCManagerW(nullptr, nullptr, SC_MANAGER_ENUMERATE_SERVICE));
    if (!manager) return drivers;

    DWORD needed = 0;
    DWORD count = 0;
    DWORD resume = 0;
    EnumServicesStatusExW(manager.get(), SC_ENUM_PROCESS_INFO, SERVICE_DRIVER, SERVICE_STATE_ALL, nullptr, 0, &needed, &count, &resume, nullptr);
    std::vector<BYTE> buffer(needed + 4096);
    resume = 0;
    if (!EnumServicesStatusExW(manager.get(), SC_ENUM_PROCESS_INFO, SERVICE_DRIVER, SERVICE_STATE_ALL, buffer.data(),
                               static_cast<DWORD>(buffer.size()), &needed, &count, &resume, nullptr)) {
        return drivers;
    }

    const auto devices = readDevices();
    const std::wstring windowsDir = windowsDirectory();
    const auto* services = reinterpret_cast<const ENUM_SERVICE_STATUS_PROCESSW*>(buffer.data());
    for (DWORD i = 0; i < count; ++i) {
        const auto& service = services[i];
        const std::string serviceName = toUtf8(service.lpServiceName);
        const auto device = devices.find(toLower(serviceName));
        const bool running = service.ServiceStatusProcess.dwCurrentState == SERVICE_RUNNING;
        if (!running && device == devices.end()) continue;

        DriverInfo driver;
        driver.service = serviceName;
        driver.displayName = toUtf8(service.lpDisplayName);
        driver.running = running;
        if (device != devices.end()) {
            if (!device->second.name.empty()) driver.displayName = device->second.name;
            driver.deviceClass = device->second.deviceClass;
            driver.provider = device->second.provider;
            driver.version = device->second.version;
            driver.driverDate = device->second.date;
        }

        wil::unique_schandle handle(OpenServiceW(manager.get(), service.lpServiceName, SERVICE_QUERY_CONFIG));
        if (handle) {
            DWORD configSize = 0;
            QueryServiceConfigW(handle.get(), nullptr, 0, &configSize);
            std::vector<BYTE> config(configSize);
            auto* info = reinterpret_cast<QUERY_SERVICE_CONFIGW*>(config.data());
            if (configSize && QueryServiceConfigW(handle.get(), info, configSize, &configSize) && info->lpBinaryPathName) {
                const std::wstring path = resolveImagePath(info->lpBinaryPathName, windowsDir);
                driver.imagePath = toUtf8(path);
                const auto slash = path.find_last_of(L'\\');
                driver.module = toLower(toUtf8(slash == std::wstring::npos ? path : path.substr(slash + 1)));
                driver.installedMs = estimateInstallTime(path);
                readFileFacts(path, driver);
            }
        }
        if (driver.module.empty()) driver.module = toLower(serviceName) + ".sys";
        drivers.push_back(std::move(driver));
    }
    return drivers;
}

std::vector<Change> diffDrivers(const std::vector<DriverInfo>& before, const std::vector<DriverInfo>& after, std::int64_t nowMs) {
    std::vector<Change> changes;
    std::map<std::string, const DriverInfo*> previous;
    for (const auto& driver : before) previous[driver.service] = &driver;

    for (const auto& driver : after) {
        const auto it = previous.find(driver.service);
        if (it == previous.end()) {
            changes.push_back({nowMs, "driver_installed", driver.displayName, driver.module, "", driver.version, "snapshot", true});
            continue;
        }
        if (it->second->version != driver.version) {
            changes.push_back({nowMs, "driver_updated", driver.displayName, driver.module, it->second->version, driver.version, "snapshot", true});
        }
        previous.erase(it);
    }
    for (const auto& [service, driver] : previous) {
        changes.push_back({nowMs, "driver_removed", driver->displayName, driver->module, driver->version, "", "snapshot", true});
    }
    return changes;
}

std::vector<Change> changesFromInventory(const std::vector<DriverInfo>& drivers, std::int64_t sinceMs) {
    std::vector<Change> changes;
    for (const auto& driver : drivers) {
        if (driver.installedMs < sinceMs) continue;
        changes.push_back({driver.installedMs, "driver_installed", driver.displayName, driver.module, "", driver.version, "driver_store", true});
    }
    return changes;
}

}  // namespace why

#include "engine/service.h"

#include <windows.h>
#include <sddl.h>
#include <shlobj.h>
#include <wil/resource.h>

#include <atomic>
#include <filesystem>

#include "engine/engine.h"
#include "engine/pipe.h"
#include "engine/winutil.h"

namespace why {
namespace {

constexpr const wchar_t* kServiceName = L"WHYMonitor";
constexpr const wchar_t* kDisplayName = L"WHY? Monitor";
constexpr const wchar_t* kDescription = L"Watches Windows logs and driver changes and explains problems. It only observes and changes nothing.";
constexpr const wchar_t* kRunKey = L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Run";
constexpr const wchar_t* kRunValue = L"WHY";

std::wstring installDirectory() { return knownFolder(CSIDL_PROGRAM_FILES) + L"\\WHY"; }
std::wstring dataDirectory() { return knownFolder(CSIDL_COMMON_APPDATA) + L"\\WHY"; }

std::string lastError(const char* what) {
    return std::string(what) + " (error " + std::to_string(GetLastError()) + ").";
}

SERVICE_STATUS_HANDLE g_statusHandle = nullptr;
wil::unique_event g_stopRequested;

void reportStatus(DWORD state, DWORD exitCode = NO_ERROR) {
    SERVICE_STATUS status{};
    status.dwServiceType = SERVICE_WIN32_OWN_PROCESS;
    status.dwCurrentState = state;
    status.dwControlsAccepted = state == SERVICE_RUNNING ? SERVICE_ACCEPT_STOP | SERVICE_ACCEPT_SHUTDOWN : 0;
    status.dwWin32ExitCode = exitCode;
    status.dwWaitHint = state == SERVICE_RUNNING ? 0 : 15000;
    SetServiceStatus(g_statusHandle, &status);
}

DWORD WINAPI serviceControl(DWORD control, DWORD, LPVOID, LPVOID) {
    if (control == SERVICE_CONTROL_STOP || control == SERVICE_CONTROL_SHUTDOWN) {
        reportStatus(SERVICE_STOP_PENDING);
        g_stopRequested.SetEvent();
    }
    return NO_ERROR;
}

void WINAPI serviceMain(DWORD, LPWSTR*) {
    g_statusHandle = RegisterServiceCtrlHandlerExW(kServiceName, serviceControl, nullptr);
    if (!g_statusHandle) return;
    reportStatus(SERVICE_START_PENDING);
    g_stopRequested.create(wil::EventOptions::ManualReset);

    EngineOptions options;
    options.databasePath = toUtf8(dataDirectory() + L"\\why.db");
    options.mode = "live";
    // Pipe client threads hold a reference until the process exits, so the engine is never destroyed.
    auto* engine = new Engine(options);
    engine->start();
    startPipeServer(*engine, kServicePipe);
    reportStatus(SERVICE_RUNNING);

    WaitForSingleObject(g_stopRequested.get(), INFINITE);
    engine->stop();
    reportStatus(SERVICE_STOPPED);
}

}  // namespace

std::string runService() {
    SERVICE_TABLE_ENTRYW table[] = {{const_cast<LPWSTR>(kServiceName), serviceMain}, {nullptr, nullptr}};
    if (!StartServiceCtrlDispatcherW(table)) return lastError("This command is started by Windows, not by hand");
    return {};
}

bool isServiceInstalled() {
    wil::unique_schandle manager(OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CONNECT));
    if (!manager) return false;
    wil::unique_schandle service(OpenServiceW(manager.get(), kServiceName, SERVICE_QUERY_STATUS));
    return static_cast<bool>(service);
}

std::string installService() {
    // A privileged service must run from a folder ordinary users cannot write to.
    const std::wstring folder = installDirectory();
    const std::wstring target = folder + L"\\why.exe";
    std::error_code ignored;
    std::filesystem::create_directories(folder, ignored);
    if (_wcsicmp(executablePath().c_str(), target.c_str()) != 0 && !CopyFileW(executablePath().c_str(), target.c_str(), FALSE)) {
        return lastError("WHY? could not be copied to Program Files");
    }

    // The history is readable only by SYSTEM and administrators. The app reaches it through the monitor.
    wil::unique_hlocal_security_descriptor descriptor;
    if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(L"D:PAI(A;OICI;FA;;;SY)(A;OICI;FA;;;BA)", SDDL_REVISION_1,
                                                              wil::out_param_ptr<PSECURITY_DESCRIPTOR*>(descriptor), nullptr)) {
        return lastError("The data folder permissions could not be prepared");
    }
    SECURITY_ATTRIBUTES attributes{sizeof(attributes), descriptor.get(), FALSE};
    if (!CreateDirectoryW(dataDirectory().c_str(), &attributes) && GetLastError() != ERROR_ALREADY_EXISTS) {
        return lastError("The data folder could not be created");
    }

    wil::unique_schandle manager(OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CREATE_SERVICE));
    if (!manager) return lastError("Windows refused access to the service manager");

    const std::wstring command = L"\"" + target + L"\" service run";
    wil::unique_schandle service(CreateServiceW(manager.get(), kServiceName, kDisplayName, SERVICE_ALL_ACCESS, SERVICE_WIN32_OWN_PROCESS,
                                                SERVICE_AUTO_START, SERVICE_ERROR_NORMAL, command.c_str(), nullptr, nullptr, nullptr, nullptr, nullptr));
    if (!service) {
        if (GetLastError() != ERROR_SERVICE_EXISTS) return lastError("The service could not be created");
        service.reset(OpenServiceW(manager.get(), kServiceName, SERVICE_ALL_ACCESS));
        if (!service) return lastError("The existing service could not be opened");
    }

    SERVICE_DESCRIPTIONW description{const_cast<LPWSTR>(kDescription)};
    ChangeServiceConfig2W(service.get(), SERVICE_CONFIG_DESCRIPTION, &description);
    SERVICE_DELAYED_AUTO_START_INFO delayed{TRUE};
    ChangeServiceConfig2W(service.get(), SERVICE_CONFIG_DELAYED_AUTO_START_INFO, &delayed);

    if (!StartServiceW(service.get(), 0, nullptr) && GetLastError() != ERROR_SERVICE_ALREADY_RUNNING) {
        return lastError("The service was created but could not start");
    }

    // Notifications come from a small tray process in each person's session.
    const std::wstring tray = L"\"" + target + L"\" --tray";
    RegSetKeyValueW(HKEY_LOCAL_MACHINE, kRunKey, kRunValue, REG_SZ, tray.c_str(), static_cast<DWORD>((tray.size() + 1) * sizeof(wchar_t)));
    return {};
}

std::string uninstallService() {
    wil::unique_schandle manager(OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CONNECT));
    if (!manager) return lastError("Windows refused access to the service manager");

    if (wil::unique_schandle service{OpenServiceW(manager.get(), kServiceName, SERVICE_STOP | SERVICE_QUERY_STATUS | DELETE)}) {
        SERVICE_STATUS status{};
        ControlService(service.get(), SERVICE_CONTROL_STOP, &status);
        for (int i = 0; i < 40 && QueryServiceStatus(service.get(), &status) && status.dwCurrentState != SERVICE_STOPPED; ++i) Sleep(250);
        if (!DeleteService(service.get()) && GetLastError() != ERROR_SERVICE_MARKED_FOR_DELETE) {
            return lastError("The service could not be removed");
        }
    }

    RegDeleteKeyValueW(HKEY_LOCAL_MACHINE, kRunKey, kRunValue);
    std::error_code ignored;
    std::filesystem::remove_all(dataDirectory(), ignored);
    if (_wcsnicmp(executablePath().c_str(), installDirectory().c_str(), installDirectory().size()) != 0) {
        std::filesystem::remove_all(installDirectory(), ignored);
    }
    return {};
}

}  // namespace why

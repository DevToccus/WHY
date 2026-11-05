#include "app/tray.h"

#include <shellapi.h>
#include <wil/resource.h>

#include <atomic>
#include <nlohmann/json.hpp>
#include <string>
#include <thread>

#include "app/notify.h"
#include "engine/pipe.h"
#include "engine/winutil.h"

using nlohmann::json;

namespace why::app {
namespace {

constexpr UINT kTrayMessage = WM_APP + 1;
constexpr UINT kNotifyMessage = WM_APP + 2;
constexpr UINT kCommandOpen = 1;
constexpr UINT kCommandExit = 2;
constexpr const wchar_t* kRegistryKey = L"Software\\WHY";
constexpr const wchar_t* kLastNotified = L"LastNotifiedMs";

std::int64_t readLastNotified() {
    std::int64_t value = 0;
    DWORD size = sizeof(value);
    RegGetValueW(HKEY_CURRENT_USER, kRegistryKey, kLastNotified, RRF_RT_REG_QWORD, nullptr, &value, &size);
    return value;
}

void writeLastNotified(std::int64_t value) {
    RegSetKeyValueW(HKEY_CURRENT_USER, kRegistryKey, kLastNotified, REG_QWORD, &value, sizeof(value));
}

void openApp() {
    ShellExecuteW(nullptr, L"open", executablePath().c_str(), nullptr, nullptr, SW_SHOWNORMAL);
}

// Long-polls the monitor. A case notifies once per new occurrence, and only if it qualifies for an alert.
void watchMonitor(HWND window, std::atomic<bool>& running) {
    std::uint64_t rev = 0;
    std::int64_t lastNotified = readLastNotified();
    while (running) {
        const auto reply = pipeRequest(json{{"cmd", "wait"}, {"rev", rev}}.dump());
        if (!reply) {
            Sleep(30'000);  // The monitor may be restarting or still starting after boot.
            continue;
        }
        const auto wait = json::parse(*reply, nullptr, false);
        const auto next = wait.is_object() ? wait.value("rev", rev) : rev;
        if (next == rev) continue;
        rev = next;

        const auto stateReply = pipeRequest(R"({"cmd":"state"})");
        if (!stateReply) continue;
        auto alert = freshAlert(*stateReply, lastNotified, loadSettings());
        if (!alert) continue;
        writeLastNotified(lastNotified);

        auto* note = new Alert(std::move(*alert));
        if (!PostMessageW(window, kNotifyMessage, 0, reinterpret_cast<LPARAM>(note))) delete note;
    }
}

LRESULT CALLBACK trayProc(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
    switch (message) {
        case kTrayMessage:
            if (LOWORD(lparam) == WM_LBUTTONUP || LOWORD(lparam) == NIN_BALLOONUSERCLICK) {
                openApp();
            } else if (LOWORD(lparam) == WM_RBUTTONUP) {
                HMENU menu = CreatePopupMenu();
                AppendMenuW(menu, MF_STRING, kCommandOpen, L"Open WHY?");
                AppendMenuW(menu, MF_STRING, kCommandExit, L"Hide this icon until next sign-in");
                POINT point{};
                GetCursorPos(&point);
                SetForegroundWindow(window);
                const UINT chosen = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_NONOTIFY, point.x, point.y, 0, window, nullptr);
                DestroyMenu(menu);
                if (chosen == kCommandOpen) openApp();
                if (chosen == kCommandExit) DestroyWindow(window);
            }
            return 0;
        case kNotifyMessage: {
            std::unique_ptr<Alert> note(reinterpret_cast<Alert*>(lparam));
            showAlert(window, *note);
            return 0;
        }
        case WM_DESTROY:
            removeNotifyIcon(window);
            PostQuitMessage(0);
            return 0;
        default:
            return DefWindowProcW(window, message, wparam, lparam);
    }
}

}  // namespace

int runTray(HINSTANCE instance) {
    wil::unique_mutex_nothrow single;
    bool alreadyRunning = false;
    if (!single.try_create(L"Local\\WHY-Tray", 0, MUTEX_ALL_ACCESS, nullptr, &alreadyRunning) || alreadyRunning) return 0;

    WNDCLASSW windowClass{};
    windowClass.lpfnWndProc = trayProc;
    windowClass.hInstance = instance;
    windowClass.lpszClassName = L"WHYTray";
    RegisterClassW(&windowClass);
    const HWND window = CreateWindowExW(0, windowClass.lpszClassName, L"WHY?", 0, 0, 0, 0, 0, HWND_MESSAGE, nullptr, instance, nullptr);
    if (!window) return 1;

    addNotifyIcon(window, kTrayMessage, L"WHY? is watching this PC");

    std::atomic<bool> running{true};
    std::thread([window, &running] { watchMonitor(window, running); }).detach();

    MSG message{};
    while (GetMessageW(&message, nullptr, 0, 0) > 0) DispatchMessageW(&message);
    running = false;
    // The watcher may be blocked in a long poll. The process exit ends it.
    ExitProcess(0);
}

}  // namespace why::app

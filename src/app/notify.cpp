#include "app/notify.h"

#include <shellapi.h>

#include <algorithm>
#include <nlohmann/json.hpp>
#include <vector>

#include "engine/winutil.h"

using nlohmann::json;

namespace why::app {
namespace {

constexpr UINT kIconId = 1;
constexpr std::size_t kTitlesListed = 3;

}  // namespace

std::optional<Alert> freshAlert(const std::string& stateJson, std::int64_t& watermark, const Settings& settings) {
    const auto state = json::parse(stateJson, nullptr, false);
    if (!state.is_object() || !state.contains("cases") || !state["cases"].is_array()) return std::nullopt;

    // The engine lists the most serious cases first, so the first fresh one leads the banner.
    std::vector<const json*> fresh;
    int serious = 0;
    std::int64_t newest = watermark;
    for (const auto& item : state["cases"]) {
        if (item.value("alert", "").empty()) continue;
        const auto last = item.value("lastMs", std::int64_t{0});
        if (last <= watermark) continue;
        newest = std::max(newest, last);
        if (!settings.focusKind.empty() && !matchesFocus(settings, item.value("signal", ""), item.value("subject", ""))) continue;
        fresh.push_back(&item);
        if (item.value("severity", "") == "critical") ++serious;
    }
    watermark = newest;
    if (fresh.empty()) return std::nullopt;

    Alert alert;
    alert.serious = serious > 0;
    if (fresh.size() == 1) {
        alert.title = toWide((alert.serious ? "Serious problem: " : "Warning: ") + fresh.front()->value("title", ""));
        alert.body = toWide(fresh.front()->value("summary", ""));
        return alert;
    }
    alert.title = std::to_wstring(fresh.size()) + L" problems need a look";
    if (serious > 0) alert.title += L", " + std::to_wstring(serious) + L" serious";
    for (std::size_t i = 0; i < std::min(fresh.size(), kTitlesListed); ++i) {
        if (i > 0) alert.body += L"\n";
        alert.body += toWide(fresh[i]->value("title", ""));
    }
    if (fresh.size() > kTitlesListed) alert.body += L"\nand " + std::to_wstring(fresh.size() - kTitlesListed) + L" more.";
    return alert;
}

void addNotifyIcon(HWND window, UINT callbackMessage, const wchar_t* tip) {
    NOTIFYICONDATAW data{};
    data.cbSize = sizeof(data);
    data.hWnd = window;
    data.uID = kIconId;
    data.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    data.uCallbackMessage = callbackMessage;
    data.hIcon = LoadIconW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(1));
    wcsncpy_s(data.szTip, tip, _TRUNCATE);
    Shell_NotifyIconW(NIM_ADD, &data);
}

void removeNotifyIcon(HWND window) {
    NOTIFYICONDATAW data{};
    data.cbSize = sizeof(data);
    data.hWnd = window;
    data.uID = kIconId;
    Shell_NotifyIconW(NIM_DELETE, &data);
}

void showAlert(HWND window, const Alert& alert) {
    NOTIFYICONDATAW data{};
    data.cbSize = sizeof(data);
    data.hWnd = window;
    data.uID = kIconId;
    data.uFlags = NIF_INFO;
    // The system error and warning icons, so the banner says how bad it is before it is read.
    data.dwInfoFlags = (alert.info ? NIIF_USER : alert.serious ? NIIF_ERROR : NIIF_WARNING) | NIIF_LARGE_ICON;
    wcsncpy_s(data.szInfoTitle, alert.title.c_str(), _TRUNCATE);
    wcsncpy_s(data.szInfo, alert.body.c_str(), _TRUNCATE);
    Shell_NotifyIconW(NIM_MODIFY, &data);
}

int runNotifyTest(HINSTANCE instance) {
    WNDCLASSW windowClass{};
    windowClass.lpfnWndProc = DefWindowProcW;
    windowClass.hInstance = instance;
    windowClass.lpszClassName = L"WHYNotifyTest";
    RegisterClassW(&windowClass);
    const HWND window = CreateWindowExW(0, windowClass.lpszClassName, L"WHY?", 0, 0, 0, 0, 0, HWND_MESSAGE, nullptr, instance, nullptr);
    if (!window) return 1;

    addNotifyIcon(window, WM_APP + 1, L"WHY? notification test");
    showAlert(window, {L"Serious problem: Blue screen: DRIVER_IRQL_NOT_LESS_OR_EQUAL",
                       L"A driver accessed memory it was not allowed to touch, and Windows stopped to protect itself. (Example)", true});
    Sleep(1500);
    showAlert(window, {L"Warning: Graphics driver stopped responding: nvlddmkm",
                       L"Windows or the graphics driver reported that the GPU stopped responding. (Example)", false});
    // The banners belong to the icon, so it stays while they are on screen.
    Sleep(8000);
    removeNotifyIcon(window);
    DestroyWindow(window);
    return 0;
}

}  // namespace why::app

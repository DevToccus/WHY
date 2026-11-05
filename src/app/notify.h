// Windows notification banners for problems that need a look, shared by the tray notifier and the window.
#pragma once

#include <windows.h>

#include <cstdint>
#include <optional>
#include <string>

#include "app/settings.h"

namespace why::app {

struct Alert {
    std::wstring title;
    std::wstring body;
    bool serious = false;
    bool info = false;  // A plain message from WHY? itself, not a problem.
};

// The banner for cases that need a look and happened after `watermark`, which moves forward past them.
// With a Focus set, only cases about the program or folder in focus count.
std::optional<Alert> freshAlert(const std::string& stateJson, std::int64_t& watermark, const Settings& settings);

// The notification area icon a banner is attached to. Windows shows banners only for an icon that exists.
void addNotifyIcon(HWND window, UINT callbackMessage, const wchar_t* tip);
void removeNotifyIcon(HWND window);
void showAlert(HWND window, const Alert& alert);

// Shows one serious and one warning banner, to see what notifications look like on this PC.
int runNotifyTest(HINSTANCE instance);

}  // namespace why::app

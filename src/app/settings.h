// What the person chose during setup: how WHY? starts, whether it stays in the background, and what it focuses on.
#pragma once

#include <nlohmann/json.hpp>
#include <string>

namespace why::app {

struct Settings {
    bool setupDone = false;
    bool runAsAdmin = false;
    bool startAtLogin = false;
    bool keepInBackground = false;
    // Focus narrows WHY? to one program or one folder of programs. Empty kind means the whole PC.
    std::string focusKind;    // "", "folder" or "program"
    std::string focusTarget;  // A folder path, or a program's path or file name.
};

Settings loadSettings();
void saveSettings(const Settings& settings);
nlohmann::json toJson(const Settings& settings);
Settings settingsFromJson(const nlohmann::json& value);

// Makes Windows match the settings: a shortcut in the Startup folder when WHY? starts at sign-in.
// Running as administrator is not stored in Windows: WHY? asks for it itself each time it opens.
// Returns an error to show, or empty.
std::string applyToWindows(const Settings& settings);

// True when a crash or hang case concerns the program or folder in focus.
bool matchesFocus(const Settings& settings, const std::string& signal, const std::string& subject);

}  // namespace why::app

#include "app/settings.h"

#include <windows.h>
#include <shlobj.h>
#include <wil/com.h>
#include <wil/resource.h>

#include "core/text.h"
#include "engine/winutil.h"

using nlohmann::json;

namespace why::app {
namespace {

constexpr const wchar_t* kRegistryKey = L"Software\\WHY";
constexpr const wchar_t* kSettingsValue = L"Settings";
// Earlier builds wrote these. Security software reads them as a program trying to persist and elevate itself,
// so they are removed whenever settings are applied.
constexpr const wchar_t* kLegacyRunKey = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
constexpr const wchar_t* kLegacyRunValue = L"WHY";
constexpr const wchar_t* kLegacyLayersKey = L"Software\\Microsoft\\Windows NT\\CurrentVersion\\AppCompatFlags\\Layers";
constexpr const wchar_t* kShortcutName = L"\\WHY.lnk";

// The person's Startup folder: the place Windows documents for programs that start at sign-in,
// visible in File Explorer and in Settings > Apps > Startup.
std::wstring startupShortcutPath() {
    wil::unique_cotaskmem_string folder;
    if (FAILED(SHGetKnownFolderPath(FOLDERID_Startup, 0, nullptr, &folder))) return L"";
    return std::wstring(folder.get()) + kShortcutName;
}

bool writeStartupShortcut(const std::wstring& path) {
    auto link = wil::CoCreateInstanceNoThrow<IShellLinkW>(CLSID_ShellLink);
    if (!link) return false;
    const std::wstring self = executablePath();
    link->SetPath(self.c_str());
    link->SetArguments(L"--background");
    link->SetDescription(L"Starts WHY? in the notification area");
    link->SetIconLocation(self.c_str(), 0);
    const auto file = link.try_query<IPersistFile>();
    return file && SUCCEEDED(file->Save(path.c_str(), TRUE));
}

void setString(HKEY root, const wchar_t* key, const wchar_t* name, const std::wstring& value) {
    RegSetKeyValueW(root, key, name, REG_SZ, value.c_str(), static_cast<DWORD>((value.size() + 1) * sizeof(wchar_t)));
}

void deleteValue(HKEY root, const wchar_t* key, const wchar_t* name) {
    HKEY opened = nullptr;
    if (RegOpenKeyExW(root, key, 0, KEY_SET_VALUE, &opened) != ERROR_SUCCESS) return;
    RegDeleteValueW(opened, name);
    RegCloseKey(opened);
}

std::string fileName(const std::string& path) {
    const auto slash = path.find_last_of("\\/");
    return toLower(slash == std::string::npos ? path : path.substr(slash + 1));
}

}  // namespace

json toJson(const Settings& settings) {
    return {{"setupDone", settings.setupDone},         {"runAsAdmin", settings.runAsAdmin},
            {"startAtLogin", settings.startAtLogin},   {"keepInBackground", settings.keepInBackground},
            {"focusKind", settings.focusKind},         {"focusTarget", settings.focusTarget}};
}

Settings settingsFromJson(const json& value) {
    Settings settings;
    if (!value.is_object()) return settings;
    settings.setupDone = value.value("setupDone", false);
    settings.runAsAdmin = value.value("runAsAdmin", false);
    settings.startAtLogin = value.value("startAtLogin", false);
    settings.keepInBackground = value.value("keepInBackground", false);
    settings.focusKind = value.value("focusKind", "");
    settings.focusTarget = value.value("focusTarget", "");
    if (settings.focusKind != "folder" && settings.focusKind != "program") settings.focusKind.clear();
    if (settings.focusKind.empty() || settings.focusTarget.empty()) settings.focusKind.clear(), settings.focusTarget.clear();
    return settings;
}

Settings loadSettings() {
    wchar_t buffer[4096]{};
    DWORD size = sizeof(buffer);
    if (RegGetValueW(HKEY_CURRENT_USER, kRegistryKey, kSettingsValue, RRF_RT_REG_SZ, nullptr, buffer, &size) != ERROR_SUCCESS) {
        return {};
    }
    return settingsFromJson(json::parse(toUtf8(buffer), nullptr, false));
}

void saveSettings(const Settings& settings) {
    setString(HKEY_CURRENT_USER, kRegistryKey, kSettingsValue, toWide(toJson(settings).dump()));
}

std::string applyToWindows(const Settings& settings) {
    const std::wstring self = executablePath();
    deleteValue(HKEY_CURRENT_USER, kLegacyLayersKey, self.c_str());
    deleteValue(HKEY_CURRENT_USER, kLegacyRunKey, kLegacyRunValue);

    const std::wstring shortcut = startupShortcutPath();
    if (shortcut.empty()) return settings.startAtLogin ? "Windows did not give the location of the Startup folder." : "";
    if (!settings.startAtLogin) {
        DeleteFileW(shortcut.c_str());
        return {};
    }
    return writeStartupShortcut(shortcut) ? "" : "The shortcut in the Startup folder could not be created.";
}

bool matchesFocus(const Settings& settings, const std::string& signal, const std::string& subject) {
    // Defender can quarantine the very program being built, so its detections are never hidden.
    if (signal == "av_detection" || signal == "av_action_failed") return true;
    // A dev server or command belongs to the folder it ran in: its subject is "label@folder".
    if (signal == "dev_crash" || signal == "dev_failed") {
        if (settings.focusKind != "folder") return false;
        std::string folder = toLower(settings.focusTarget);
        if (!folder.empty() && folder.back() != '\\') folder += '\\';
        const auto at = subject.find('@');
        return at != std::string::npos && (toLower(subject.substr(at + 1)) + "\\").starts_with(folder);
    }
    if (signal != "app_crash" && signal != "app_hang") return false;
    if (settings.focusKind == "program") return fileName(subject) == fileName(settings.focusTarget);
    if (settings.focusKind == "folder") {
        std::string folder = toLower(settings.focusTarget);
        if (!folder.empty() && folder.back() != '\\') folder += '\\';
        return toLower(subject).starts_with(folder);
    }
    return false;
}

}  // namespace why::app

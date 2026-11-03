#include "engine/winutil.h"

#include <windows.h>
#include <shlobj.h>
#include <wil/resource.h>

#include <vector>

#include "core/text.h"

#pragma comment(lib, "shell32.lib")

namespace why {

std::string toUtf8(std::wstring_view text) {
    if (text.empty()) return {};
    const int size = WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
    std::string out(static_cast<std::size_t>(size), '\0');
    WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), out.data(), size, nullptr, nullptr);
    return out;
}

std::wstring toWide(std::string_view text) {
    if (text.empty()) return {};
    const int size = MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0);
    std::wstring out(static_cast<std::size_t>(size), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), out.data(), size);
    return out;
}

std::int64_t nowUnixMs() {
    FILETIME now{};
    GetSystemTimeAsFileTime(&now);
    return fileTimeToUnixMs((static_cast<std::uint64_t>(now.dwHighDateTime) << 32) | now.dwLowDateTime);
}

bool isElevated() {
    wil::unique_handle token;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, token.put())) return false;
    TOKEN_ELEVATION elevation{};
    DWORD size = sizeof(elevation);
    return GetTokenInformation(token.get(), TokenElevation, &elevation, size, &size) && elevation.TokenIsElevated != 0;
}

std::wstring executablePath() {
    std::wstring path(MAX_PATH, L'\0');
    for (;;) {
        const DWORD written = GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
        if (written < path.size()) {
            path.resize(written);
            return path;
        }
        path.resize(path.size() * 2);
    }
}

std::wstring windowsDirectory() {
    wchar_t buffer[MAX_PATH]{};
    const UINT size = GetWindowsDirectoryW(buffer, MAX_PATH);
    return std::wstring(buffer, size);
}

std::wstring knownFolder(int csidl) {
    wchar_t buffer[MAX_PATH]{};
    if (FAILED(SHGetFolderPathW(nullptr, csidl, nullptr, SHGFP_TYPE_CURRENT, buffer))) return {};
    return buffer;
}

std::string computerName() {
    wchar_t buffer[MAX_COMPUTERNAME_LENGTH + 1]{};
    DWORD size = MAX_COMPUTERNAME_LENGTH + 1;
    if (!GetComputerNameW(buffer, &size)) return {};
    return toUtf8(std::wstring_view(buffer, size));
}

std::string osDescription() {
    wil::unique_hkey key;
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion", 0, KEY_READ, key.put()) != ERROR_SUCCESS) {
        return "Windows";
    }
    const auto readValue = [&](const wchar_t* name) {
        wchar_t buffer[128]{};
        DWORD size = sizeof(buffer) - sizeof(wchar_t);
        if (RegGetValueW(key.get(), nullptr, name, RRF_RT_REG_SZ, nullptr, buffer, &size) != ERROR_SUCCESS) return std::wstring{};
        return std::wstring(buffer);
    };
    const std::wstring build = readValue(L"CurrentBuildNumber");
    std::wstring product = readValue(L"ProductName");
    // ProductName still says "Windows 10" on Windows 11 builds.
    if (!build.empty() && _wtoi(build.c_str()) >= 22000) {
        if (const auto pos = product.find(L"Windows 10"); pos != std::wstring::npos) product.replace(pos, 10, L"Windows 11");
    }
    return toUtf8(product) + " (build " + toUtf8(build) + ")";
}

}  // namespace why

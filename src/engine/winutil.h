// Small Windows helpers shared by the collectors, the engine and the app.
#pragma once

#include <cstdint>
#include <string>
#include <string_view>

namespace why {

std::string toUtf8(std::wstring_view text);
std::wstring toWide(std::string_view text);
std::int64_t nowUnixMs();
bool isElevated();
std::wstring executablePath();
std::wstring windowsDirectory();
std::wstring knownFolder(int csidl);
std::string computerName();
std::string osDescription();

}  // namespace why

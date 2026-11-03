#include "core/text.h"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <cstdio>
#include <ctime>

namespace why {

std::string toLower(std::string_view text) {
    std::string out(text);
    std::transform(out.begin(), out.end(), out.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return out;
}

bool endsWithNoCase(std::string_view text, std::string_view suffix) {
    if (suffix.size() > text.size()) return false;
    return toLower(text.substr(text.size() - suffix.size())) == toLower(suffix);
}

bool containsNoCase(std::string_view text, std::string_view needle) {
    return toLower(text).find(toLower(needle)) != std::string::npos;
}

std::string trim(std::string_view text) {
    const auto isSpace = [](unsigned char c) { return std::isspace(c) != 0; };
    while (!text.empty() && isSpace(static_cast<unsigned char>(text.front()))) text.remove_prefix(1);
    while (!text.empty() && isSpace(static_cast<unsigned char>(text.back()))) text.remove_suffix(1);
    return std::string(text);
}

std::string moduleStem(std::string_view text) {
    if (const auto slash = text.find_last_of("\\/"); slash != std::string_view::npos) {
        text.remove_prefix(slash + 1);
    }
    std::string name = trim(toLower(text));
    if (endsWithNoCase(name, ".sys") || endsWithNoCase(name, ".dll") || endsWithNoCase(name, ".exe")) {
        name.resize(name.size() - 4);
    }
    return name;
}

namespace {

bool readInt(std::string_view text, std::size_t pos, std::size_t len, int& out) {
    if (pos + len > text.size()) return false;
    const char* first = text.data() + pos;
    const auto [ptr, ec] = std::from_chars(first, first + len, out);
    return ec == std::errc{} && ptr == first + len;
}

}  // namespace

std::int64_t parseIsoUtcMs(std::string_view text) {
    int year, month, day, hour, minute, second;
    if (!readInt(text, 0, 4, year) || !readInt(text, 5, 2, month) || !readInt(text, 8, 2, day) ||
        !readInt(text, 11, 2, hour) || !readInt(text, 14, 2, minute) || !readInt(text, 17, 2, second)) {
        return 0;
    }
    std::tm tm{};
    tm.tm_year = year - 1900;
    tm.tm_mon = month - 1;
    tm.tm_mday = day;
    tm.tm_hour = hour;
    tm.tm_min = minute;
    tm.tm_sec = second;
    const std::time_t seconds = _mkgmtime(&tm);
    if (seconds < 0) return 0;
    int millis = 0;
    if (text.size() >= 23 && text[19] == '.') readInt(text, 20, 3, millis);
    return static_cast<std::int64_t>(seconds) * 1000 + millis;
}

std::int64_t fileTimeToUnixMs(std::uint64_t fileTime) {
    constexpr std::uint64_t kEpochDiff = 116444736000000000ULL;
    if (fileTime < kEpochDiff) return 0;
    return static_cast<std::int64_t>((fileTime - kEpochDiff) / 10000);
}

std::string hexCode(std::uint32_t value) {
    char buffer[16];
    std::snprintf(buffer, sizeof(buffer), "0x%08X", value);
    return buffer;
}

}  // namespace why

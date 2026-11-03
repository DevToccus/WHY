// Small string and time helpers used across the core.
#pragma once

#include <cstdint>
#include <string>
#include <string_view>

namespace why {

std::string toLower(std::string_view text);
bool endsWithNoCase(std::string_view text, std::string_view suffix);
bool containsNoCase(std::string_view text, std::string_view needle);
std::string trim(std::string_view text);
// Reduces \Driver\foo, C:\x\foo.sys or foo to the lower-case name foo.
std::string moduleStem(std::string_view text);
// Parses 2026-10-28T15:52:44.4668527Z to Unix milliseconds. Returns 0 when malformed.
std::int64_t parseIsoUtcMs(std::string_view text);
// Windows FILETIME ticks to Unix milliseconds.
std::int64_t fileTimeToUnixMs(std::uint64_t fileTime);
std::string hexCode(std::uint32_t value);

}  // namespace why

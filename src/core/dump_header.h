// Reads the fixed header of a 64-bit Windows kernel crash dump.
#pragma once

#include <cstdint>
#include <optional>
#include <span>

namespace why {

struct DumpHeader {
    std::uint32_t bugCheck = 0;
    std::uint64_t params[4]{};
    std::int64_t timeMs = 0;
};

constexpr std::size_t kDumpHeaderSize = 0x2000;

std::optional<DumpHeader> parseDumpHeader(std::span<const std::byte> bytes);

}  // namespace why

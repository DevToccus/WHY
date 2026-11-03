#include "core/dump_header.h"

#include <cstring>

#include "core/text.h"

namespace why {
namespace {

constexpr std::size_t kBugCheckOffset = 0x38;
constexpr std::size_t kParamsOffset = 0x40;
constexpr std::size_t kSystemTimeOffset = 0xFA8;

template <typename T>
T readAt(std::span<const std::byte> bytes, std::size_t offset) {
    T value{};
    std::memcpy(&value, bytes.data() + offset, sizeof(T));
    return value;
}

}  // namespace

std::optional<DumpHeader> parseDumpHeader(std::span<const std::byte> bytes) {
    if (bytes.size() < kDumpHeaderSize) return std::nullopt;
    if (std::memcmp(bytes.data(), "PAGEDU64", 8) != 0) return std::nullopt;

    DumpHeader header;
    header.bugCheck = readAt<std::uint32_t>(bytes, kBugCheckOffset);
    for (std::size_t i = 0; i < 4; ++i) {
        header.params[i] = readAt<std::uint64_t>(bytes, kParamsOffset + i * 8);
    }
    header.timeMs = fileTimeToUnixMs(readAt<std::uint64_t>(bytes, kSystemTimeOffset));
    return header;
}

}  // namespace why

#include <catch2/catch_test_macros.hpp>

#include <cstring>
#include <vector>

#include "core/bugcheck.h"
#include "core/dump_header.h"

using namespace why;

namespace {

std::vector<std::byte> makeHeader() {
    std::vector<std::byte> bytes(kDumpHeaderSize);
    std::memcpy(bytes.data(), "PAGEDU64", 8);
    const std::uint32_t code = 0xD1;
    std::memcpy(bytes.data() + 0x38, &code, sizeof(code));
    const std::uint64_t param = 0xFFFFF80012345678ULL;
    std::memcpy(bytes.data() + 0x40 + 3 * 8, &param, sizeof(param));
    const std::uint64_t fileTime = 134000000000000000ULL;
    std::memcpy(bytes.data() + 0xFA8, &fileTime, sizeof(fileTime));
    return bytes;
}

}  // namespace

TEST_CASE("reads stop code, parameters and time") {
    const auto header = parseDumpHeader(makeHeader());
    REQUIRE(header);
    CHECK(header->bugCheck == 0xD1);
    CHECK(header->params[3] == 0xFFFFF80012345678ULL);
    CHECK(header->timeMs == (134000000000000000LL - 116444736000000000LL) / 10000);
}

TEST_CASE("rejects anything that is not a full 64-bit dump header") {
    auto bytes = makeHeader();
    CHECK_FALSE(parseDumpHeader(std::span<const std::byte>(bytes.data(), 100)));
    CHECK_FALSE(parseDumpHeader({}));
    std::memcpy(bytes.data(), "PAGEDUMP", 8);
    CHECK_FALSE(parseDumpHeader(bytes));
}

TEST_CASE("stop codes have a name and an explanation, even unknown ones") {
    const auto known = bugCheckInfo(0xD1);
    CHECK(known.name == "DRIVER_IRQL_NOT_LESS_OR_EQUAL");
    CHECK(known.addressParam == 3);
    const auto unknown = bugCheckInfo(0x12345);
    CHECK(unknown.name == "0x00012345");
    CHECK_FALSE(unknown.plain.empty());
}

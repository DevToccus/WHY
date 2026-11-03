// Feeds damaged input to every parser. Meant to run under AddressSanitizer (preset "asan"),
// where any out-of-bounds read stops the run. The seed is fixed so a failure can be reproduced.
#include <catch2/catch_test_macros.hpp>

#include <cstring>
#include <nlohmann/json.hpp>
#include <random>
#include <string>
#include <vector>

#include "core/dump_header.h"
#include "core/event_xml.h"
#include "core/redact.h"
#include "core/signals.h"
#include "core/text.h"

using namespace why;

namespace {

constexpr int kRounds = 30'000;

const std::string kEvent =
    "<Event xmlns='http://schemas.microsoft.com/win/2004/08/events/event'><System>"
    "<Provider Name='Service Control Manager' Guid='{x}' EventSourceName='Service Control Manager'/>"
    "<EventID Qualifiers='49152'>7045</EventID><Level>4</Level>"
    "<TimeCreated SystemTime='2026-10-28T15:52:44.4668527Z'/><EventRecordID>229066</EventRecordID>"
    "<Channel>System</Channel></System><EventData><Data Name='ServiceName'>a &amp; b</Data>"
    "<Data Name='ImagePath'>\\SystemRoot\\drv.sys</Data><Data/><Data>x</Data></EventData></Event>";

std::string damage(std::string text, std::mt19937& random) {
    switch (random() % 4) {
        case 0:
            text.resize(random() % (text.size() + 1));
            break;
        case 1:
            for (int i = 0; i < 4 && !text.empty(); ++i) text[random() % text.size()] = static_cast<char>(random() % 256);
            break;
        case 2:
            if (!text.empty()) text.erase(random() % text.size(), random() % 64);
            break;
        default:
            if (!text.empty()) text.insert(random() % text.size(), text.substr(random() % text.size(), random() % 64));
            break;
    }
    return text;
}

}  // namespace

TEST_CASE("damaged event XML never reads out of bounds") {
    std::mt19937 random(12345);
    int parsed = 0;
    for (int round = 0; round < kRounds; ++round) {
        const std::string text = damage(kEvent, random);
        if (auto event = parseEventXml(text)) {
            ++parsed;
            classify(*event);
        }
    }
    CHECK(parsed > 0);  // Otherwise the damage is too heavy and the parser is barely exercised.
}

TEST_CASE("damaged dump headers never read out of bounds") {
    std::vector<std::byte> valid(kDumpHeaderSize);
    std::memcpy(valid.data(), "PAGEDU64", 8);
    std::mt19937 random(54321);
    for (int round = 0; round < kRounds; ++round) {
        auto bytes = valid;
        for (int i = 0; i < 16; ++i) bytes[random() % bytes.size()] = static_cast<std::byte>(random() % 256);
        (void)parseDumpHeader(std::span<const std::byte>(bytes.data(), random() % (bytes.size() + 1)));
    }
    SUCCEED();
}

TEST_CASE("damaged text never breaks the small helpers") {
    std::mt19937 random(777);
    const std::string base = "2026-10-28T15:52:44.466Z 0x000000d1 C:\\Users\\mario\\x.sys NVIDIA - Display - 32.0 9NRZT3Q9R3DL-App";
    for (int round = 0; round < kRounds; ++round) {
        const std::string text = damage(base, random);
        (void)parseIsoUtcMs(text);
        (void)bugCheckFromReport(text);
        (void)moduleStem(text);
        (void)redactText(text);
        (void)isDriverUpdateTitle(text);
        (void)isRoutineUpdateTitle(text);
    }
    SUCCEED();
}

#include <catch2/catch_test_macros.hpp>

#include "core/signals.h"

using namespace why;

namespace {

Event make(const char* provider, int id) {
    Event event;
    event.provider = provider;
    event.eventId = id;
    event.timeMs = 1;
    return event;
}

}  // namespace

TEST_CASE("an event ID only counts together with its provider") {
    // All of these reuse IDs of real signals and were found on a real PC.
    for (const auto& [provider, id] : {std::pair{"Microsoft-Windows-Kernel-Boot", 153}, std::pair{"Microsoft-Windows-Kernel-Processor-Power", 55},
                                       std::pair{"Microsoft-Windows-Hyper-V-VmSwitch", 7}, std::pair{"Microsoft-Windows-Kernel-General", 11},
                                       std::pair{"TPM", 18}, std::pair{"Microsoft-Windows-Wininit", 19}, std::pair{"Microsoft-Windows-Hyper-V-Hypervisor", 129}}) {
        auto event = make(provider, id);
        CHECK_FALSE(classify(event));
    }
    auto disk = make("disk", 153);
    REQUIRE(classify(disk));
    CHECK(disk.signal == "disk_retry");
}

TEST_CASE("common harmless driver failures are filtered") {
    auto event = make("Microsoft-Windows-Kernel-PnP", 219);
    event.data["DriverName"] = "\\Driver\\WudfRd";
    CHECK_FALSE(classify(event));

    auto real = make("Microsoft-Windows-Kernel-PnP", 219);
    real.data["DriverName"] = "\\Driver\\mydevice";
    REQUIRE(classify(real));
    CHECK(real.subject == "mydevice");
}

TEST_CASE("only kernel drivers count as driver installs") {
    auto service = make("Service Control Manager", 7045);
    service.data["ServiceName"] = "SomeApp";
    service.data["ImagePath"] = "C:\\Program Files\\App\\app.exe";
    CHECK_FALSE(classify(service));

    auto driver = make("Service Control Manager", 7045);
    driver.data["ServiceName"] = "newdrv";
    driver.data["ImagePath"] = "\\SystemRoot\\System32\\drivers\\newdrv.sys";
    REQUIRE(classify(driver));
    CHECK(driver.role == Role::Change);
    CHECK(driver.subject == "newdrv");
}

TEST_CASE("the graphics driver is named from the event") {
    auto display = make("Display", 4101);
    display.data["param1"] = "nvlddmkm";
    REQUIRE(classify(display));
    CHECK(display.subject == "nvlddmkm");

    auto direct = make("nvlddmkm", 153);
    REQUIRE(classify(direct));
    CHECK(direct.subject == "nvlddmkm");
}

TEST_CASE("the stop code is read from a WER report") {
    CHECK(bugCheckFromReport("0x000000d1 (0xffff, 0x2, 0x0, 0xfffff)") == 0xD1);
    CHECK(bugCheckFromReport("no code here") == 0);
}

TEST_CASE("routine updates seen on a real PC are filtered") {
    CHECK(isRoutineUpdateTitle("Aggiornamento dell'intelligence sulla sicurezza per Microsoft Defender Antivirus - KB2267602 (versione 1.4)"));
    CHECK(isRoutineUpdateTitle("9NRZT3Q9R3DL-Microsoft.WindowsAppRuntime.2"));
    CHECK_FALSE(isRoutineUpdateTitle("2026-10 Cumulative Update for Windows 11 (KB5040000)"));
    CHECK_FALSE(isRoutineUpdateTitle("NVIDIA - Display - 32.0.15.6094"));
    CHECK_FALSE(isRoutineUpdateTitle("PowerShell v7.5.0 (x64)"));

    auto event = make("Microsoft-Windows-WindowsUpdateClient", 19);
    event.data["updateTitle"] = "9PLM9XGG6VKS-OpenAI.Codex";
    CHECK_FALSE(classify(event));
}

TEST_CASE("driver update titles are told apart from other updates") {
    CHECK(isDriverUpdateTitle("NVIDIA - Display - 32.0.15.6094"));
    CHECK_FALSE(isDriverUpdateTitle("2026-10 Cumulative Update for Windows 11 (KB5040000)"));
    CHECK_FALSE(isDriverUpdateTitle("Security Intelligence Update - KB2267602 - Version 1.4"));
}

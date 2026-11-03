#include <catch2/catch_test_macros.hpp>

#include <algorithm>

#include "core/investigator.h"
#include "core/signals.h"

using namespace why;

namespace {

constexpr std::int64_t kDay = 24 * 3600 * 1000LL;
constexpr std::int64_t kNow = 1000 * kDay;

Event make(const char* provider, int id, std::int64_t timeMs, std::map<std::string, std::string> data = {}) {
    static std::uint64_t record = 0;
    Event event;
    event.channel = "System";
    event.recordId = ++record;
    event.timeMs = timeMs;
    event.provider = provider;
    event.eventId = id;
    event.data = std::move(data);
    REQUIRE(classify(event));
    return event;
}

Event gpuReset(std::int64_t timeMs) { return make("Display", 4101, timeMs, {{"param1", "nvlddmkm"}}); }

InvestigationInput input(std::vector<Event> events) {
    InvestigationInput in;
    in.events = std::move(events);
    in.nowMs = kNow;
    in.historyStartMs = kNow - 180 * kDay;
    return in;
}

bool hasFinding(const Case& c, Certainty certainty, const char* text) {
    return std::any_of(c.findings.begin(), c.findings.end(), [&](const Finding& finding) {
        return finding.certainty == certainty && finding.text.find(text) != std::string::npos;
    });
}

Change driverUpdate(std::int64_t timeMs, const char* module) {
    return {timeMs, "driver_updated", "NVIDIA display driver", module, "1.0", "1.1", "snapshot", false};
}

}  // namespace

TEST_CASE("one blue screen with all its records is one incident") {
    auto in = input({make("Microsoft-Windows-Kernel-Power", 41, kNow - kDay, {{"BugcheckCode", "209"}}),
                     make("EventLog", 6008, kNow - kDay + 5000),
                     make("Microsoft-Windows-WER-SystemErrorReporting", 1001, kNow - kDay + 60000, {{"param1", "0x000000d1 (0x0, 0x2, 0x0, 0x0)"}})});
    const auto cases = investigate(in);
    REQUIRE(cases.size() == 1);
    CHECK(cases[0].signal == "bsod");
    CHECK(cases[0].incidents == 1);
    CHECK(cases[0].chain.size() == 3);
    CHECK(cases[0].title == "Blue screen: DRIVER_IRQL_NOT_LESS_OR_EQUAL");
    CHECK(cases[0].alert == "severe");
}

TEST_CASE("a restart without a stop code never claims a cause it cannot know") {
    auto in = input({make("Microsoft-Windows-Kernel-Power", 41, kNow - kDay, {{"BugcheckCode", "0"}, {"PowerButtonTimestamp", "0"}})});
    const auto cases = investigate(in);
    REQUIRE(cases.size() == 1);
    CHECK(cases[0].signal == "unexpected_restart");
    CHECK_FALSE(cases[0].causeSupported);
    CHECK(hasFinding(cases[0], Certainty::Correlated, "cannot tell these apart"));
}

TEST_CASE("a held power button is reported as the probable reason") {
    auto in = input({make("Microsoft-Windows-Kernel-Power", 41, kNow - kDay, {{"BugcheckCode", "0"}, {"LongPowerButtonPressDetected", "true"}})});
    const auto cases = investigate(in);
    REQUIRE(cases.size() == 1);
    CHECK(cases[0].causeSupported);
    CHECK(hasFinding(cases[0], Certainty::Probable, "power button"));
}

TEST_CASE("a dump module is graded by how directly it was found") {
    DumpInfo dump;
    dump.fileName = "a.dmp";
    dump.timeMs = kNow - kDay;
    dump.bugCheck = 0xD1;
    dump.module = "nvlddmkm.sys";
    dump.moduleEvidence = "parameter";

    auto direct = input({});
    direct.dumps = {dump};
    auto cases = investigate(direct);
    REQUIRE(cases.size() == 1);
    CHECK(cases[0].causeSupported);
    CHECK(hasFinding(cases[0], Certainty::Probable, "points directly at nvlddmkm.sys"));

    dump.moduleEvidence = "stack";
    auto stack = input({});
    stack.dumps = {dump};
    cases = investigate(stack);
    CHECK_FALSE(cases[0].causeSupported);
    CHECK(hasFinding(cases[0], Certainty::Correlated, "found on the kernel stack"));
}

TEST_CASE("a matching driver change before the problem is probable, not proven") {
    auto in = input({gpuReset(kNow - 2 * kDay)});
    in.changes = {driverUpdate(kNow - 5 * kDay, "nvlddmkm.sys")};
    const auto cases = investigate(in);
    REQUIRE(cases.size() == 1);
    CHECK(cases[0].causeSupported);
    CHECK(hasFinding(cases[0], Certainty::Probable, "no earlier occurrence"));
    REQUIRE(cases[0].changesBefore.size() == 1);
}

TEST_CASE("a change after the problem began cannot be its cause") {
    auto in = input({gpuReset(kNow - 10 * kDay), gpuReset(kNow - 2 * kDay)});
    in.changes = {driverUpdate(kNow - 5 * kDay, "nvlddmkm.sys")};
    const auto cases = investigate(in);
    REQUIRE(cases.size() == 1);
    CHECK_FALSE(cases[0].causeSupported);
    CHECK(hasFinding(cases[0], Certainty::Correlated, "did not start it"));
}

TEST_CASE("a change near the start of the logs is only correlated") {
    auto in = input({gpuReset(kNow - 2 * kDay)});
    in.historyStartMs = kNow - 6 * kDay;
    in.changes = {driverUpdate(kNow - 5 * kDay, "nvlddmkm.sys")};
    const auto cases = investigate(in);
    CHECK_FALSE(cases[0].causeSupported);
    CHECK(hasFinding(cases[0], Certainty::Correlated, "do not reach far enough back"));
}

TEST_CASE("unrelated changes are only ever correlated") {
    auto in = input({gpuReset(kNow - 2 * kDay)});
    in.changes = {driverUpdate(kNow - 5 * kDay, "other.sys")};
    const auto cases = investigate(in);
    CHECK_FALSE(cases[0].causeSupported);
    CHECK(hasFinding(cases[0], Certainty::Correlated, "Timing alone"));
}

TEST_CASE("a steady, long-running complaint stays quiet") {
    std::vector<Event> events;
    // Every day for 120 days, like the boot-time failures seen on a real PC.
    for (int day = 120; day >= 1; --day) events.push_back(make("Service Control Manager", 7026, kNow - day * kDay, {{"param1", "somedrv"}}));
    const auto cases = investigate(input(events));
    REQUIRE(cases.size() == 1);
    CHECK(cases[0].longRunning);
    CHECK(cases[0].alert.empty());
}

TEST_CASE("a long-running problem that suddenly gets worse does alert") {
    std::vector<Event> events;
    for (int week = 12; week >= 2; --week) events.push_back(gpuReset(kNow - week * 7 * kDay));
    for (int hours = 1; hours <= 6; ++hours) events.push_back(gpuReset(kNow - hours * 3600 * 1000LL));
    const auto cases = investigate(input(events));
    REQUIRE(cases.size() == 1);
    CHECK(cases[0].longRunning);
    CHECK(cases[0].alert == "increased");
}

TEST_CASE("a new warning alerts once, an old quiet one does not") {
    auto fresh = investigate(input({gpuReset(kNow - kDay)}));
    CHECK(fresh[0].alert == "new");
    auto old = investigate(input({gpuReset(kNow - 40 * kDay)}));
    CHECK(old[0].alert.empty());
    CHECK(old[0].status == CaseStatus::Quiet);
}

TEST_CASE("a fix attempt is followed: watching, recurred, resolved") {
    auto in = input({gpuReset(kNow - 2 * kDay)});
    in.overrides = {{"gpu_reset:nvlddmkm", "tried_fix", kNow - kDay}};
    auto cases = investigate(in);
    CHECK(cases[0].status == CaseStatus::Watching);
    CHECK(cases[0].alert.empty());

    in.events.push_back(gpuReset(kNow - 3600 * 1000));
    cases = investigate(in);
    CHECK(cases[0].status == CaseStatus::Recurred);
    CHECK(cases[0].alert == "repeating");

    auto quiet = input({gpuReset(kNow - 20 * kDay)});
    quiet.overrides = {{"gpu_reset:nvlddmkm", "tried_fix", kNow - 15 * kDay}};
    cases = investigate(quiet);
    CHECK(cases[0].status == CaseStatus::Resolved);
}

TEST_CASE("the latest statement about a case wins") {
    auto in = input({gpuReset(kNow - 2 * kDay)});
    in.overrides = {{"gpu_reset:nvlddmkm", "tried_fix", kNow - kDay}, {"gpu_reset:nvlddmkm", "reopen", kNow - 1000}};
    CHECK(investigate(in)[0].status == CaseStatus::Open);
}

TEST_CASE("several kinds of blue screen raise a shared-cause suspicion, as correlation only") {
    auto in = input({});
    for (const auto code : {0xD1u, 0x50u, 0x1Au}) {
        DumpInfo dump;
        dump.fileName = std::to_string(code);
        dump.timeMs = kNow - (code + 1) * 3600 * 1000LL / 4;
        dump.bugCheck = code;
        in.dumps.push_back(dump);
    }
    const auto cases = investigate(in);
    REQUIRE(cases.size() == 3);
    CHECK(hasFinding(cases[0], Certainty::Correlated, "share one cause"));
}

TEST_CASE("bursts of the same complaint count as one incident") {
    const auto cases = investigate(input({make("disk", 153, kNow - kDay, {{"param1", "D0"}}), make("disk", 153, kNow - kDay + 1000, {{"param1", "D0"}}),
                                          make("disk", 153, kNow - 3600 * 1000, {{"param1", "D0"}})}));
    REQUIRE(cases.size() == 1);
    CHECK(cases[0].incidents == 2);
    CHECK(cases[0].chain.size() == 3);
}

TEST_CASE("a second restart without shutdown is worth an alert, even weeks after the first") {
    const auto restart = [](std::int64_t time) { return make("Microsoft-Windows-Kernel-Power", 41, time, {{"BugcheckCode", "0"}}); };
    auto cases = investigate(input({restart(kNow - 50 * kDay), restart(kNow - 6 * kDay)}));
    REQUIRE(cases.size() == 1);
    CHECK(cases[0].alert == "repeating");

    cases = investigate(input({restart(kNow - 50 * kDay)}));
    CHECK(cases[0].alert.empty());
}

TEST_CASE("a driver that reinstalls itself all the time is not a lead") {
    auto in = input({gpuReset(kNow - 2 * kDay)});
    for (int day = 3; day < 8; ++day) in.changes.push_back({kNow - day * kDay, "driver_installed", "CorsairLLAccess", "", "", "", "service_install", false});
    const auto cases = investigate(in);
    CHECK(cases[0].changesBefore.empty());
}

TEST_CASE("a bulk refresh of driver folders is not a lead") {
    auto in = input({gpuReset(kNow - 2 * kDay)});
    for (int i = 0; i < 12; ++i) in.changes.push_back({kNow - 5 * kDay, "driver_installed", "drv" + std::to_string(i), "", "", "", "driver_store", true});
    CHECK(investigate(in)[0].changesBefore.empty());

    auto single = input({gpuReset(kNow - 2 * kDay)});
    single.changes.push_back({kNow - 5 * kDay, "driver_installed", "drv", "", "", "", "driver_store", true});
    CHECK(investigate(single)[0].changesBefore.size() == 1);
}

TEST_CASE("a crashing service is not linked to unrelated driver changes") {
    auto in = input({make("Service Control Manager", 7034, kNow - 2 * kDay, {{"param1", "Some Service"}})});
    in.changes = {driverUpdate(kNow - 5 * kDay, "other.sys")};
    CHECK(investigate(in)[0].changesBefore.empty());
}

TEST_CASE("changes are read from events") {
    const auto changes = changesFromEvents({make("Service Control Manager", 7045, 5, {{"ServiceName", "drv"}, {"ImagePath", "\\SystemRoot\\System32\\drivers\\Drv.sys"}}),
                                            make("Microsoft-Windows-WindowsUpdateClient", 19, 6, {{"updateTitle", "NVIDIA - Display - 32.0.15.6094"}}),
                                            make("Microsoft-Windows-WindowsUpdateClient", 19, 7, {{"updateTitle", "Cumulative Update (KB500)"}})});
    REQUIRE(changes.size() == 3);
    CHECK(changes[0].kind == "driver_installed");
    CHECK(changes[0].module == "drv.sys");
    CHECK(changes[1].kind == "driver_updated");
    CHECK(changes[2].kind == "update_installed");
}

#include <catch2/catch_test_macros.hpp>

#include "core/signals.h"
#include "core/store.h"

using namespace why;

namespace {

Event diskEvent(std::uint64_t record, std::int64_t time) {
    Event event;
    event.channel = "System";
    event.recordId = record;
    event.timeMs = time;
    event.provider = "disk";
    event.eventId = 7;
    event.data["param1"] = "\\Device\\Harddisk0\\DR0";
    event.message = "Bad block";
    return event;
}

}  // namespace

TEST_CASE("events are stored once and classified on load") {
    Store store(":memory:");
    CHECK(store.addEvents({diskEvent(1, 100), diskEvent(2, 200)}) == 2);
    CHECK(store.addEvents({diskEvent(2, 200), diskEvent(3, 300)}) == 1);

    const auto events = store.loadEvents();
    REQUIRE(events.size() == 3);
    CHECK(events[0].signal == "disk_error");
    CHECK(events[0].subject == "\\Device\\Harddisk0\\DR0");
    CHECK(events[2].timeMs == 300);
}

TEST_CASE("pruning keeps the store bounded") {
    Store store(":memory:");
    store.addEvents({diskEvent(1, 100), diskEvent(2, 900)});
    store.addChanges({{50, "driver_updated", "a", "a.sys", "1", "2", "snapshot", true}, {950, "driver_updated", "b", "b.sys", "1", "2", "snapshot", true}});
    store.prune(500);
    CHECK(store.loadEvents().size() == 1);
    REQUIRE(store.loadChanges().size() == 1);
    CHECK(store.loadChanges()[0].name == "b");
}

TEST_CASE("drivers, overrides, activity and recorder round trip") {
    Store store(":memory:");
    DriverInfo driver;
    driver.service = "nvlddmkm";
    driver.module = "nvlddmkm.sys";
    driver.version = "32.0";
    store.saveDrivers({driver});
    REQUIRE(store.loadDrivers().size() == 1);
    CHECK(store.loadDrivers()[0].version == "32.0");

    store.addOverride({"case", "tried_fix", 5});
    REQUIRE(store.loadOverrides().size() == 1);
    CHECK(store.loadOverrides()[0].action == "tried_fix");

    store.saveActivity("case", {{10, "process_start", "\\Windows\\a.exe", 4}});
    CHECK(store.loadActivity().at("case").at(0).image == "\\Windows\\a.exe");

    store.saveRecorder({{20, "driver_load", "\\Windows\\b.sys", 4}});
    CHECK(store.loadRecorder().at(0).kind == "driver_load");

    CHECK_FALSE(store.getMeta("missing"));
    store.setMeta("k", "v");
    CHECK(store.getMeta("k") == "v");
}

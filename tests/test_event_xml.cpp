#include <catch2/catch_test_macros.hpp>

#include "core/event_xml.h"

using namespace why;

TEST_CASE("reads a real Kernel-Power 41 event") {
    const auto event = parseEventXml(
        "<Event xmlns='http://schemas.microsoft.com/win/2004/08/events/event'><System>"
        "<Provider Name='Microsoft-Windows-Kernel-Power' Guid='{331c3b3a-2005-44c2-ac5e-77220c37d6b4}'/>"
        "<EventID>41</EventID><Version>10</Version><Level>1</Level><Task>63</Task>"
        "<TimeCreated SystemTime='2026-10-28T15:52:44.4668527Z'/><EventRecordID>229066</EventRecordID>"
        "<Channel>System</Channel><Computer>PC</Computer></System>"
        "<EventData><Data Name='BugcheckCode'>0</Data><Data Name='SleepInProgress'>0</Data>"
        "<Data Name='LongPowerButtonPressDetected'>false</Data></EventData></Event>");
    REQUIRE(event);
    CHECK(event->provider == "Microsoft-Windows-Kernel-Power");
    CHECK(event->eventId == 41);
    CHECK(event->level == 1);
    CHECK(event->recordId == 229066);
    CHECK(event->channel == "System");
    CHECK(event->timeMs == 1793202764466);
    CHECK(event->data.at("BugcheckCode") == "0");
    CHECK(event->data.at("LongPowerButtonPressDetected") == "false");
}

TEST_CASE("handles qualifiers, source names, unnamed data and entities") {
    const auto event = parseEventXml(
        "<Event><System><Provider Name='Service Control Manager' Guid='{x}' EventSourceName='Service Control Manager'/>"
        "<EventID Qualifiers='49152'>7026</EventID><Level>2</Level>"
        "<TimeCreated SystemTime='2026-01-01T00:00:00.000Z'/><EventRecordID>5</EventRecordID><Channel>System</Channel></System>"
        "<EventData><Data>a &amp; b</Data><Data/><Data>third</Data><Binary>00</Binary></EventData></Event>");
    REQUIRE(event);
    CHECK(event->provider == "Service Control Manager");
    CHECK(event->eventId == 7026);
    CHECK(event->data.at("param1") == "a & b");
    CHECK(event->data.at("param2") == "");
    CHECK(event->data.at("param3") == "third");
}

TEST_CASE("does not confuse DataItem with Data") {
    const auto event = parseEventXml(
        "<Event><System><Provider Name='p'/><EventID>1</EventID><Level>1</Level>"
        "<TimeCreated SystemTime='2026-01-01T00:00:00Z'/><EventRecordID>1</EventRecordID><Channel>S</Channel></System>"
        "<UserData><DataItem>nope</DataItem></UserData></Event>");
    REQUIRE(event);
    CHECK(event->data.empty());
}

TEST_CASE("rejects malformed input without reading out of bounds") {
    CHECK_FALSE(parseEventXml(""));
    CHECK_FALSE(parseEventXml("<Event>"));
    CHECK_FALSE(parseEventXml("<Event><System></System></Event>"));
    CHECK_FALSE(parseEventXml("<Event><System><Provider Name='"));
    const auto truncated = parseEventXml(
        "<Event><System><Provider Name='p'/><EventID>1</EventID><TimeCreated SystemTime='2026-01-01T00:00:00Z'/></System>"
        "<EventData><Data Name='x'>unterminated");
    REQUIRE(truncated);
    CHECK(truncated->data.empty());
}

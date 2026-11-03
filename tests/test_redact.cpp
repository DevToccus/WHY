#include <catch2/catch_test_macros.hpp>

#include "core/redact.h"

using namespace why;

TEST_CASE("account names are removed from paths") {
    CHECK(redactText("C:\\Users\\mario\\AppData\\x.dll") == "C:\\Users\\<user>\\AppData\\x.dll");
    CHECK(redactText("c:\\users\\Mario Rossi\\a") == "c:\\users\\<user> Rossi\\a");
    CHECK(redactText("C:\\Users\\mario") == "C:\\Users\\<user>");
    CHECK(redactText("first C:\\Users\\a\\x and C:\\Users\\b\\y") == "first C:\\Users\\<user>\\x and C:\\Users\\<user>\\y");
}

TEST_CASE("shared folders and other paths stay readable") {
    CHECK(redactText("C:\\Users\\Public\\x") == "C:\\Users\\Public\\x");
    CHECK(redactText("C:\\Windows\\System32\\drivers\\a.sys") == "C:\\Windows\\System32\\drivers\\a.sys");
}

TEST_CASE("a whole report is cleaned, including the computer name") {
    nlohmann::json report{{"machine", {{"computerName", "DESKTOP-ABC"}}},
                          {"cases", {{{"message", "Failure on desktop-abc at C:\\Users\\mario\\a.exe"}, {"count", 3}}}}};
    redactJson(report, "DESKTOP-ABC");
    CHECK(report["machine"]["computerName"] == "<computer>");
    CHECK(report["cases"][0]["message"] == "Failure on <computer> at C:\\Users\\<user>\\a.exe");
    CHECK(report["cases"][0]["count"] == 3);
}

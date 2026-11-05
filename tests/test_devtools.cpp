#include <catch2/catch_test_macros.hpp>

#include "core/devtools.h"

using namespace why;

TEST_CASE("dev runtimes are recognised by file name") {
    CHECK(isDevRuntime("node.exe"));
    CHECK(isDevRuntime("Python.EXE"));
    CHECK_FALSE(isDevRuntime("chrome.exe"));
    CHECK_FALSE(isDevRuntime("Code.exe"));
}

TEST_CASE("the label names the tool and its subcommand") {
    CHECK(devLabel("\"C:\\Program Files\\nodejs\\node.exe\" C:\\dev\\shop\\node_modules\\next\\dist\\bin\\next dev") == "next dev");
    CHECK(devLabel("node \"C:\\dev\\app\\node_modules\\vite\\bin\\vite.js\" --port 5173") == "vite");
    CHECK(devLabel("node C:\\dev\\app\\node_modules\\@nestjs\\cli\\bin\\nest.js start") == "@nestjs/cli start");
    CHECK(devLabel("C:\\Python312\\python.exe -m uvicorn main:app --reload") == "uvicorn");
    CHECK(devLabel("node server.js") == "server");
    CHECK(devLabel("\"C:\\Program Files\\dotnet\\dotnet.exe\" watch run") == "dotnet watch");
    CHECK(devLabel("node.exe") == "node");
}

TEST_CASE("launchers and bundled runtimes are not projects") {
    CHECK(isLauncher("node \"C:\\Program Files\\nodejs\\node_modules\\npm\\bin\\npm-cli.js\" run dev"));
    CHECK_FALSE(isLauncher("node C:\\dev\\shop\\node_modules\\next\\dist\\bin\\next dev"));
    CHECK(isOutsideProjects("node C:\\Users\\me\\AppData\\Local\\npm-cache\\_npx\\abc\\node_modules\\server\\index.js"));
    // The runtime's own install folder does not count, only what it runs.
    CHECK_FALSE(isOutsideProjects("\"C:\\Program Files\\nodejs\\node.exe\" C:\\dev\\shop\\server.js"));
}

TEST_CASE("Ctrl+C is not a crash") {
    CHECK(isDeliberateExit(0xC000013A));
    CHECK(isDeliberateExit(130));
    CHECK_FALSE(isDeliberateExit(1));
    CHECK_FALSE(isDeliberateExit(0xC0000409));
}

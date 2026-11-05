// Recognising development tools from their process: which runtimes count, which command a process is running,
// and which processes only launch others. No Windows dependency, so it is tested directly.
#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace why {

// Node, Bun, Deno, Python, .NET, Java, Go, Cargo, Ruby, PHP. Takes a file name such as "node.exe", any case.
bool isDevRuntime(std::string_view imageName);

// Splits a Windows command line the way programs receive it, honouring quotes.
std::vector<std::string> splitCommandLine(std::string_view commandLine);

// A short name for what the process runs: "next dev", "vite build", "uvicorn", "dotnet watch".
std::string devLabel(std::string_view commandLine);

// npm, npx, pnpm, yarn and corepack only start the real tool, which is watched instead.
bool isLauncher(std::string_view commandLine);

// Tools that ship their own copy of a runtime, or run from global caches, are not the person's project.
bool isOutsideProjects(std::string_view commandLine);

// Exit codes that mean the person stopped it: Ctrl+C, or a debugger detaching.
bool isDeliberateExit(std::uint32_t code);

}  // namespace why

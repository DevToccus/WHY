#include "core/devtools.h"

#include <algorithm>
#include <array>

#include "core/text.h"

namespace why {
namespace {

constexpr std::array kRuntimes{"node.exe", "bun.exe",  "deno.exe", "python.exe", "python3.exe", "pythonw.exe", "py.exe",
                               "dotnet.exe", "java.exe", "javaw.exe", "go.exe",  "cargo.exe",   "ruby.exe",    "php.exe"};

// Words worth keeping after a tool name: "next dev", "vite build", "jest --watch" stays "jest".
bool isSubcommand(std::string_view word) {
    return !word.empty() && word.size() <= 16 && word.front() != '-' && word.find_first_of("\\/.:=") == std::string_view::npos;
}

std::string baseName(std::string_view path) {
    const auto slash = path.find_last_of("\\/");
    std::string name(slash == std::string_view::npos ? path : path.substr(slash + 1));
    for (const char* extension : {".exe", ".js", ".cjs", ".mjs", ".ts", ".py", ".dll", ".jar"}) {
        if (endsWithNoCase(name, extension)) name.resize(name.size() - std::string_view(extension).size());
    }
    return name;
}

// The package a script lives in: "...\node_modules\@scope\name\bin\x.js" is "@scope/name".
std::string packageOf(std::string_view path) {
    const std::string lower = toLower(path);
    const auto at = lower.rfind("node_modules\\");
    const auto atSlash = lower.rfind("node_modules/");
    const auto start = at == std::string::npos ? atSlash : (atSlash == std::string::npos ? at : std::max(at, atSlash));
    if (start == std::string::npos) return {};
    std::string rest(path.substr(start + 13));
    std::replace(rest.begin(), rest.end(), '\\', '/');
    auto end = rest.find('/');
    if (!rest.empty() && rest.front() == '@' && end != std::string::npos) end = rest.find('/', end + 1);
    return rest.substr(0, end);
}

}  // namespace

bool isDevRuntime(std::string_view imageName) {
    const std::string name = toLower(imageName);
    return std::find(kRuntimes.begin(), kRuntimes.end(), name) != kRuntimes.end();
}

std::vector<std::string> splitCommandLine(std::string_view commandLine) {
    std::vector<std::string> parts;
    std::string current;
    bool quoted = false;
    bool any = false;
    for (const char c : commandLine) {
        if (c == '"') {
            quoted = !quoted;
            any = true;
        } else if ((c == ' ' || c == '\t') && !quoted) {
            if (any) parts.push_back(std::move(current));
            current.clear();
            any = false;
        } else {
            current += c;
            any = true;
        }
    }
    if (any) parts.push_back(std::move(current));
    return parts;
}

std::string devLabel(std::string_view commandLine) {
    const auto parts = splitCommandLine(commandLine);
    if (parts.empty()) return {};
    const std::string runtime = toLower(baseName(parts[0]));

    for (std::size_t i = 1; i < parts.size(); ++i) {
        const std::string& part = parts[i];
        if (part.starts_with("-")) {
            // "python -m uvicorn app:main" runs the module after -m.
            if ((part == "-m" || part == "-jar") && i + 1 < parts.size()) return baseName(parts[i + 1]);
            continue;
        }
        std::string tool = packageOf(part);
        if (tool.empty()) tool = baseName(part);
        if (tool.empty()) continue;
        // .NET and Go take a verb first: "dotnet watch", "go run".
        if ((runtime == "dotnet" || runtime == "go" || runtime == "cargo") && isSubcommand(part)) return runtime + " " + part;
        if (i + 1 < parts.size() && isSubcommand(parts[i + 1])) return tool + " " + parts[i + 1];
        return tool;
    }
    return runtime;
}

bool isLauncher(std::string_view commandLine) {
    const std::string lower = toLower(commandLine);
    for (const char* launcher : {"npm-cli.js", "npx-cli.js", "pnpm.cjs", "pnpm.js", "yarn.js", "yarn.cjs", "corepack"}) {
        if (lower.find(launcher) != std::string::npos) return true;
    }
    return false;
}

bool isOutsideProjects(std::string_view commandLine) {
    // The runtime itself is usually installed under Program Files; only what it runs matters.
    const auto parts = splitCommandLine(commandLine);
    std::string rest;
    for (std::size_t i = 1; i < parts.size(); ++i) rest += parts[i] + " ";
    const std::string lower = toLower(rest);
    for (const char* place : {"\\npm-cache\\_npx\\", "\\appdata\\roaming\\npm\\node_modules\\", "\\.vscode\\extensions\\",
                              "\\appdata\\local\\programs\\", "\\program files\\", "\\program files (x86)\\", "\\windowsapps\\"}) {
        if (lower.find(place) != std::string::npos) return true;
    }
    return false;
}

bool isDeliberateExit(std::uint32_t code) {
    // 130 is SIGINT for Node; 0xC000013A is Ctrl+C for Windows programs; 0x40010004 is a debugger ending the process.
    return code == 0 || code == 130 || code == 0xC000013A || code == 0x40010004;
}

}  // namespace why

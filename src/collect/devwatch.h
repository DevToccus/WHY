// Development tools: dev servers and commands that exit with an error, seen two ways.
// The watcher follows Node, Python, .NET and similar processes and notes when one ends with an error code.
// why-run, a separate console program, runs a command, keeps the end of its error output and leaves a record
// in the person's folder, which the watcher picks up.
#pragma once

#include <functional>
#include <memory>
#include <string>

#include "core/types.h"

namespace why {

// %LOCALAPPDATA%\WHY\runs, where why-run leaves one JSON file per failed command.
std::wstring userRunFolder();

class DevWatcher {
public:
    using Callback = std::function<void(Event)>;
    explicit DevWatcher(Callback callback);
    ~DevWatcher();
    DevWatcher(const DevWatcher&) = delete;
    DevWatcher& operator=(const DevWatcher&) = delete;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace why

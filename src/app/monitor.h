// Live performance of the PC and of each process, as Task Manager shows them. Read only, nothing is kept.
#pragma once

#include <memory>
#include <nlohmann/json.hpp>
#include <string>
#include <vector>

namespace why::app {

// CPU, memory, GPU, disks, network, power and counts. Rates are measured between two calls,
// so the first sample reports them as zero.
class SystemMonitor {
public:
    SystemMonitor();
    ~SystemMonitor();
    nlohmann::json sample();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

// Every running process with CPU, memory, disk and GPU use, and whether it is an app with a window,
// a background process or part of Windows.
class ProcessMonitor {
public:
    ProcessMonitor();
    ~ProcessMonitor();
    nlohmann::json sample();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

// The icon of each program file, as a PNG data URI. Files Windows cannot read are left out.
nlohmann::json programIcons(const std::vector<std::wstring>& paths);

}  // namespace why::app

// The signal table: which Windows events matter and what they mean.
// Anything not listed here is treated as noise and never reaches a case.
#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "core/types.h"

namespace why {

struct SignalInfo {
    const char* key;
    Area area;
    Severity severity;
    Role role;
};

const SignalInfo* signalInfo(std::string_view key);

// One provider and the event IDs to read from it.
struct ProviderQuery {
    std::string channel;
    std::string provider;
    std::vector<int> eventIds;
};
const std::vector<ProviderQuery>& signalQueries();

// Sets signal, role and subject. Returns false when the event is noise.
bool classify(Event& event);

// Records WHY? writes itself about development tools. Event 1 is a process seen exiting with an error;
// event 2 is a command run through why-run, with the end of its error output.
inline constexpr const char* kDevProvider = "WHY";
inline constexpr const char* kDevChannel = "WHY";
inline constexpr int kDevExitEvent = 1;
inline constexpr int kDevRunEvent = 2;

// A dev process that ran at least this long was serving something; shorter ones were one-off commands.
inline constexpr std::int64_t kDevServerMinRuntimeMs = 20'000;

// Reads the bug check code from the text of a WER 1001 event, or 0.
std::uint32_t bugCheckFromReport(std::string_view text);

// True for a Windows Update title that names a driver, such as "NVIDIA - Display - 32.0.15.6094".
bool isDriverUpdateTitle(std::string_view title);

// True for updates that install constantly and say nothing about a problem.
bool isRoutineUpdateTitle(std::string_view title);

}  // namespace why

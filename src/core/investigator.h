// Turns classified events, crash dumps and changes into cases with graded findings.
// Pure logic: same input, same output, no I/O.
#pragma once

#include <map>
#include <vector>

#include "core/types.h"

namespace why {

struct InvestigationInput {
    std::vector<Event> events;
    std::vector<DumpInfo> dumps;
    std::vector<Change> changes;
    std::vector<DriverInfo> drivers;
    std::vector<CaseOverride> overrides;
    std::map<std::string, std::vector<ActivityRecord>> activity;  // By case key.
    std::int64_t nowMs = 0;
    std::int64_t historyStartMs = 0;  // Oldest moment the logs reach.
};

std::vector<Case> investigate(const InvestigationInput& input);

// Changes that can be read directly from classified events (driver installs, Windows Update).
std::vector<Change> changesFromEvents(const std::vector<Event>& events);

}  // namespace why

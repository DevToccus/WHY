// The signal table: which Windows events matter and what they mean.
// Anything not listed here is treated as noise and never reaches a case.
#pragma once

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
    std::string provider;
    std::vector<int> eventIds;
};
const std::vector<ProviderQuery>& signalQueries();

// Sets signal, role and subject. Returns false when the event is noise.
bool classify(Event& event);

// Reads the bug check code from the text of a WER 1001 event, or 0.
std::uint32_t bugCheckFromReport(std::string_view text);

// True for a Windows Update title that names a driver, such as "NVIDIA - Display - 32.0.15.6094".
bool isDriverUpdateTitle(std::string_view title);

// True for updates that install constantly and say nothing about a problem.
bool isRoutineUpdateTitle(std::string_view title);

}  // namespace why

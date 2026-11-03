// Reads classified events from the Windows Event Log, past and future.
#pragma once

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "core/types.h"

namespace why {

struct EventLogHistory {
    std::vector<Event> events;
    std::int64_t oldestLogMs = 0;  // Oldest record in the System log, which limits how far back anything can go.
    std::string error;
};

EventLogHistory readEventHistory(int days);

// Delivers new events as Windows writes them, with no polling.
class EventWatcher {
public:
    using Callback = std::function<void(Event)>;
    explicit EventWatcher(Callback callback);
    ~EventWatcher();
    EventWatcher(const EventWatcher&) = delete;
    EventWatcher& operator=(const EventWatcher&) = delete;
    bool active() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace why

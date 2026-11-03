// The black box: a fixed-size memory of recent process starts, stops and driver loads, fed by ETW.
// Needs administrator rights.
#pragma once

#include <memory>
#include <string>
#include <vector>

#include "core/types.h"

namespace why {

class ActivityRecorder {
public:
    ActivityRecorder();
    ~ActivityRecorder();
    ActivityRecorder(const ActivityRecorder&) = delete;
    ActivityRecorder& operator=(const ActivityRecorder&) = delete;

    bool start();
    void stop();
    bool running() const;
    const std::string& error() const;
    // Every event delivered by Windows, including those not kept. Shows the session is alive.
    std::uint64_t eventsSeen() const;
    // Windows' own view of the session, for diagnostics.
    std::string sessionStats() const;

    // Newest last. Records older than maxAgeMs before nowMs are left out when maxAgeMs is positive.
    std::vector<ActivityRecord> snapshot(std::int64_t nowMs = 0, std::int64_t maxAgeMs = 0) const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace why

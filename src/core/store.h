// SQLite storage. A path of ":memory:" keeps everything in RAM, which is how the portable mode leaves no history.
#pragma once

#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "core/types.h"

struct sqlite3;

namespace why {

class Store {
public:
    explicit Store(const std::string& path);
    ~Store();
    Store(const Store&) = delete;
    Store& operator=(const Store&) = delete;

    // Each returns how many rows were new. Repeated records are ignored.
    int addEvents(const std::vector<Event>& events);
    int addChanges(const std::vector<Change>& changes);
    std::vector<Event> loadEvents() const;
    std::vector<Change> loadChanges() const;

    void saveDrivers(const std::vector<DriverInfo>& drivers);
    std::vector<DriverInfo> loadDrivers() const;

    void addOverride(const CaseOverride& item);
    std::vector<CaseOverride> loadOverrides() const;

    void saveActivity(const std::string& caseKey, const std::vector<ActivityRecord>& records);
    std::map<std::string, std::vector<ActivityRecord>> loadActivity() const;

    // The recorder's latest contents, kept on disk so they survive a crash.
    void saveRecorder(const std::vector<ActivityRecord>& records);
    std::vector<ActivityRecord> loadRecorder() const;

    void setMeta(const std::string& key, const std::string& value);
    std::optional<std::string> getMeta(const std::string& key) const;

    // Drops everything older than the cutoff, keeping the store bounded.
    void prune(std::int64_t olderThanMs);

private:
    void exec(const char* sql) const;
    sqlite3* db_ = nullptr;
};

}  // namespace why

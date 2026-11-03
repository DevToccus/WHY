#include "core/store.h"

#include <sqlite3.h>

#include <nlohmann/json.hpp>
#include <stdexcept>

#include "core/serialize.h"
#include "core/signals.h"

using nlohmann::json;

namespace why {
namespace {

class Statement {
public:
    Statement(sqlite3* db, const char* sql) {
        if (sqlite3_prepare_v2(db, sql, -1, &stmt_, nullptr) != SQLITE_OK) {
            throw std::runtime_error(sqlite3_errmsg(db));
        }
    }
    ~Statement() { sqlite3_finalize(stmt_); }
    Statement(const Statement&) = delete;
    Statement& operator=(const Statement&) = delete;

    void bind(int index, const std::string& value) {
        sqlite3_bind_text(stmt_, index, value.data(), static_cast<int>(value.size()), SQLITE_TRANSIENT);
    }
    void bind(int index, std::int64_t value) { sqlite3_bind_int64(stmt_, index, value); }

    bool step() {
        const int rc = sqlite3_step(stmt_);
        if (rc == SQLITE_ROW) return true;
        if (rc == SQLITE_DONE) return false;
        throw std::runtime_error(sqlite3_errmsg(sqlite3_db_handle(stmt_)));
    }
    void reset() {
        sqlite3_reset(stmt_);
        sqlite3_clear_bindings(stmt_);
    }
    std::string text(int column) const {
        const auto* value = sqlite3_column_text(stmt_, column);
        return value ? std::string(reinterpret_cast<const char*>(value)) : std::string();
    }
    std::int64_t integer(int column) const { return sqlite3_column_int64(stmt_, column); }

private:
    sqlite3_stmt* stmt_ = nullptr;
};

class Transaction {
public:
    explicit Transaction(sqlite3* db) : db_(db) { sqlite3_exec(db_, "BEGIN", nullptr, nullptr, nullptr); }
    ~Transaction() { sqlite3_exec(db_, committed_ ? "COMMIT" : "ROLLBACK", nullptr, nullptr, nullptr); }
    void commit() { committed_ = true; }

private:
    sqlite3* db_;
    bool committed_ = false;
};

constexpr const char* kSchema = R"sql(
CREATE TABLE IF NOT EXISTS events(
    channel TEXT NOT NULL, record_id INTEGER NOT NULL, time_ms INTEGER NOT NULL, provider TEXT NOT NULL,
    event_id INTEGER NOT NULL, level INTEGER NOT NULL, message TEXT NOT NULL, data TEXT NOT NULL,
    PRIMARY KEY(channel, record_id)) WITHOUT ROWID;
CREATE INDEX IF NOT EXISTS events_time ON events(time_ms);
CREATE TABLE IF NOT EXISTS changes(
    time_ms INTEGER NOT NULL, kind TEXT NOT NULL, name TEXT NOT NULL, source TEXT NOT NULL, body TEXT NOT NULL,
    PRIMARY KEY(time_ms, kind, name, source)) WITHOUT ROWID;
CREATE TABLE IF NOT EXISTS case_overrides(key TEXT NOT NULL, action TEXT NOT NULL, at_ms INTEGER NOT NULL);
CREATE TABLE IF NOT EXISTS case_activity(key TEXT PRIMARY KEY, body TEXT NOT NULL) WITHOUT ROWID;
CREATE TABLE IF NOT EXISTS meta(key TEXT PRIMARY KEY, value TEXT NOT NULL) WITHOUT ROWID;
)sql";

}  // namespace

Store::Store(const std::string& path) {
    if (sqlite3_open(path.c_str(), &db_) != SQLITE_OK) {
        const std::string error = db_ ? sqlite3_errmsg(db_) : "out of memory";
        sqlite3_close(db_);
        db_ = nullptr;
        throw std::runtime_error(error);
    }
    sqlite3_busy_timeout(db_, 5000);
    exec("PRAGMA journal_mode=WAL; PRAGMA synchronous=NORMAL; PRAGMA temp_store=MEMORY;");
    exec(kSchema);
}

Store::~Store() { sqlite3_close(db_); }

void Store::exec(const char* sql) const {
    char* error = nullptr;
    if (sqlite3_exec(db_, sql, nullptr, nullptr, &error) != SQLITE_OK) {
        const std::string message = error ? error : "sqlite error";
        sqlite3_free(error);
        throw std::runtime_error(message);
    }
}

int Store::addEvents(const std::vector<Event>& events) {
    if (events.empty()) return 0;
    Transaction tx(db_);
    Statement insert(db_, "INSERT OR IGNORE INTO events VALUES(?,?,?,?,?,?,?,?)");
    int added = 0;
    for (const auto& event : events) {
        insert.bind(1, event.channel);
        insert.bind(2, static_cast<std::int64_t>(event.recordId));
        insert.bind(3, event.timeMs);
        insert.bind(4, event.provider);
        insert.bind(5, static_cast<std::int64_t>(event.eventId));
        insert.bind(6, static_cast<std::int64_t>(event.level));
        insert.bind(7, event.message);
        insert.bind(8, json(event.data).dump(-1, ' ', false, json::error_handler_t::replace));
        insert.step();
        added += sqlite3_changes(db_);
        insert.reset();
    }
    tx.commit();
    return added;
}

std::vector<Event> Store::loadEvents() const {
    Statement select(db_, "SELECT channel,record_id,time_ms,provider,event_id,level,message,data FROM events ORDER BY time_ms");
    std::vector<Event> events;
    while (select.step()) {
        Event event;
        event.channel = select.text(0);
        event.recordId = static_cast<std::uint64_t>(select.integer(1));
        event.timeMs = select.integer(2);
        event.provider = select.text(3);
        event.eventId = static_cast<int>(select.integer(4));
        event.level = static_cast<int>(select.integer(5));
        event.message = select.text(6);
        event.data = json::parse(select.text(7)).get<std::map<std::string, std::string>>();
        // Classifying again on load means improved rules also apply to history.
        if (classify(event)) events.push_back(std::move(event));
    }
    return events;
}

int Store::addChanges(const std::vector<Change>& changes) {
    if (changes.empty()) return 0;
    Transaction tx(db_);
    Statement insert(db_, "INSERT OR IGNORE INTO changes VALUES(?,?,?,?,?)");
    int added = 0;
    for (const auto& change : changes) {
        insert.bind(1, change.timeMs);
        insert.bind(2, change.kind);
        insert.bind(3, change.name);
        insert.bind(4, change.source);
        insert.bind(5, json(change).dump(-1, ' ', false, json::error_handler_t::replace));
        insert.step();
        added += sqlite3_changes(db_);
        insert.reset();
    }
    tx.commit();
    return added;
}

std::vector<Change> Store::loadChanges() const {
    Statement select(db_, "SELECT body FROM changes ORDER BY time_ms DESC");
    std::vector<Change> changes;
    while (select.step()) changes.push_back(json::parse(select.text(0)).get<Change>());
    return changes;
}

void Store::saveDrivers(const std::vector<DriverInfo>& drivers) {
    setMeta("drivers", json(drivers).dump(-1, ' ', false, json::error_handler_t::replace));
}

std::vector<DriverInfo> Store::loadDrivers() const {
    const auto body = getMeta("drivers");
    if (!body) return {};
    return json::parse(*body).get<std::vector<DriverInfo>>();
}

void Store::addOverride(const CaseOverride& item) {
    Statement insert(db_, "INSERT INTO case_overrides VALUES(?,?,?)");
    insert.bind(1, item.key);
    insert.bind(2, item.action);
    insert.bind(3, item.atMs);
    insert.step();
}

std::vector<CaseOverride> Store::loadOverrides() const {
    Statement select(db_, "SELECT key,action,at_ms FROM case_overrides ORDER BY at_ms");
    std::vector<CaseOverride> items;
    while (select.step()) items.push_back({select.text(0), select.text(1), select.integer(2)});
    return items;
}

void Store::saveActivity(const std::string& caseKey, const std::vector<ActivityRecord>& records) {
    Statement insert(db_, "INSERT OR REPLACE INTO case_activity VALUES(?,?)");
    insert.bind(1, caseKey);
    insert.bind(2, json(records).dump(-1, ' ', false, json::error_handler_t::replace));
    insert.step();
}

std::map<std::string, std::vector<ActivityRecord>> Store::loadActivity() const {
    Statement select(db_, "SELECT key,body FROM case_activity");
    std::map<std::string, std::vector<ActivityRecord>> out;
    while (select.step()) out[select.text(0)] = json::parse(select.text(1)).get<std::vector<ActivityRecord>>();
    return out;
}

void Store::saveRecorder(const std::vector<ActivityRecord>& records) {
    setMeta("recorder", json(records).dump(-1, ' ', false, json::error_handler_t::replace));
}

std::vector<ActivityRecord> Store::loadRecorder() const {
    const auto body = getMeta("recorder");
    if (!body) return {};
    return json::parse(*body).get<std::vector<ActivityRecord>>();
}

void Store::setMeta(const std::string& key, const std::string& value) {
    Statement insert(db_, "INSERT OR REPLACE INTO meta VALUES(?,?)");
    insert.bind(1, key);
    insert.bind(2, value);
    insert.step();
}

std::optional<std::string> Store::getMeta(const std::string& key) const {
    Statement select(db_, "SELECT value FROM meta WHERE key=?");
    select.bind(1, key);
    if (!select.step()) return std::nullopt;
    return select.text(0);
}

void Store::prune(std::int64_t olderThanMs) {
    Transaction tx(db_);
    Statement events(db_, "DELETE FROM events WHERE time_ms < ?");
    events.bind(1, olderThanMs);
    events.step();
    Statement changes(db_, "DELETE FROM changes WHERE time_ms < ?");
    changes.bind(1, olderThanMs);
    changes.step();
    tx.commit();
}

}  // namespace why

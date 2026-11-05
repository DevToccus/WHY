#include "engine/engine.h"

#include <windows.h>

#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <filesystem>
#include <fstream>
#include <map>
#include <mutex>
#include <nlohmann/json.hpp>
#include <set>
#include <thread>
#include <vector>

#include "collect/devwatch.h"
#include "collect/drivers.h"
#include "collect/dumps.h"
#include "collect/etw.h"
#include "collect/eventlog.h"
#include "collect/usn.h"
#include "core/investigator.h"
#include "core/redact.h"
#include "core/serialize.h"
#include "core/signals.h"
#include "core/store.h"
#include "core/text.h"
#include "engine/winutil.h"

using nlohmann::json;

namespace why {
namespace {

constexpr std::int64_t kMinuteMs = 60'000;
constexpr std::int64_t kDayMs = 24 * 60 * kMinuteMs;
constexpr auto kDebounce = std::chrono::seconds(2);
constexpr auto kRecorderFlushEvery = std::chrono::minutes(1);
constexpr auto kInventoryEvery = std::chrono::hours(6);
// A busy PC can overwrite the whole journal in about an hour, so it has to be read more often than that.
constexpr auto kJournalEvery = std::chrono::minutes(30);
constexpr std::size_t kMaxTimeline = 5000;  // Enough records for the Home charts to cover a year.
constexpr std::size_t kMaxChanges = 500;
constexpr std::int64_t kActivityWindowMs = 10 * kMinuteMs;
constexpr std::size_t kMaxActivityPerCase = 200;

int changeRank(const Change& change) {
    if (change.source == "snapshot") return 0;
    if (change.source == "service_install") return 1;
    if (change.source == "windows_update") return 2;
    if (change.source == "usn") return 3;
    return 4;
}

std::string lastSegment(const std::string& title) {
    const auto pos = title.rfind(" - ");
    return pos == std::string::npos ? title : trim(title.substr(pos + 3));
}

// Windows Update titles name a vendor and a version, not a file. Matching the version links the two.
void linkUpdatesToDrivers(std::vector<Change>& changes, const std::vector<DriverInfo>& drivers) {
    for (auto& change : changes) {
        if (change.kind != "driver_updated" || !change.module.empty()) continue;
        const std::string version = lastSegment(change.name);
        change.newVersion = version;
        for (const auto& driver : drivers) {
            if (!driver.version.empty() && driver.version == version) {
                change.module = driver.module;
                break;
            }
        }
    }
}

// The same driver change can be seen by several sources. One entry per driver per day, from the most exact source.
std::vector<Change> mergeChanges(std::vector<Change> changes) {
    std::stable_sort(changes.begin(), changes.end(), [](const Change& a, const Change& b) { return changeRank(a) < changeRank(b); });
    std::set<std::pair<std::string, std::int64_t>> seen;
    std::vector<Change> merged;
    for (auto& change : changes) {
        const std::string identity = change.module.empty() ? change.name : moduleStem(change.module);
        if (seen.insert({identity, change.timeMs / kDayMs}).second) merged.push_back(std::move(change));
    }
    std::sort(merged.begin(), merged.end(), [](const Change& a, const Change& b) { return a.timeMs > b.timeMs; });
    return merged;
}

}  // namespace

struct Engine::Impl {
    EngineOptions options;
    std::unique_ptr<Store> store;
    std::unique_ptr<ActivityRecorder> recorder;
    std::unique_ptr<EventWatcher> watcher;
    std::unique_ptr<DevWatcher> devWatcher;

    // Everything below is touched only by the worker thread, except where a mutex says otherwise.
    std::vector<DumpInfo> dumps;
    std::map<std::wstring, DumpInfo> analyzedDumps;
    std::vector<DriverInfo> drivers;
    std::vector<Coverage> coverage;
    std::map<std::string, std::int64_t> caseLastSeen;
    std::vector<ActivityRecord> previousSessionActivity;
    bool previousSessionCrashed = false;
    std::int64_t historyStartMs = 0;
    std::uint64_t usnJournal = 0;
    std::int64_t usnCursor = 0;
    Coverage usnCoverage;

    mutable std::mutex stateMutex;
    mutable std::condition_variable stateChanged;
    std::shared_ptr<const std::string> stateJson = std::make_shared<const std::string>("{}");
    std::string stateFingerprint;
    std::uint64_t rev = 0;
    std::set<std::string> knownCases;

    std::mutex queueMutex;
    std::condition_variable queueSignal;
    std::vector<Event> incoming;
    std::vector<CaseOverride> incomingOverrides;
    bool stopping = false;
    bool scanning = true;
    std::thread worker;

    explicit Impl(EngineOptions opts) : options(std::move(opts)), store(std::make_unique<Store>(options.databasePath)) {
        if (const auto cursor = store->getMeta("usn_cursor")) usnCursor = std::stoll(*cursor);
        if (const auto journal = store->getMeta("usn_journal")) usnJournal = std::stoull(*journal);
        loadKeptOverrides();
    }

    // Without Live monitoring the database lives in memory; the person's fixes must outlive the window.
    void loadKeptOverrides() {
        if (options.overridesPath.empty()) return;
        std::ifstream file(std::filesystem::path(options.overridesPath));
        std::string line;
        while (std::getline(file, line)) {
            const auto item = json::parse(line, nullptr, false);
            if (!item.is_object()) continue;
            CaseOverride entry{item.value("key", ""), item.value("action", ""), item.value("atMs", std::int64_t{0})};
            if (!entry.key.empty() && (entry.action == "tried_fix" || entry.action == "reopen")) store->addOverride(entry);
        }
    }

    void keepOverride(const CaseOverride& entry) {
        if (options.overridesPath.empty()) return;
        const std::filesystem::path path(options.overridesPath);
        std::error_code ignored;
        std::filesystem::create_directories(path.parent_path(), ignored);
        std::ofstream(path, std::ios::app) << json{{"key", entry.key}, {"action", entry.action}, {"atMs", entry.atMs}}.dump() << "\n";
    }

    void scanEvents(int days) {
        const auto history = readEventHistory(days);
        store->addEvents(history.events);
        if (history.oldestLogMs) historyStartMs = history.oldestLogMs;

        Coverage entry{"event_log", "Windows event logs", history.error.empty() ? "ok" : "limited", history.error, 0, history.oldestLogMs};
        if (entry.detail.empty()) entry.detail = "Blue screens, restarts, driver, disk and hardware errors, programs that crash or hang, and Microsoft Defender detections.";
        upsertCoverage(entry);
    }

    void scanDumps() {
        const auto listing = listDumps();
        std::map<std::wstring, DumpInfo> kept;
        for (const auto& path : listing.paths) {
            if (const auto it = analyzedDumps.find(path); it != analyzedDumps.end()) {
                kept.insert(*it);
            } else if (auto info = analyzeDump(path)) {
                kept.emplace(path, std::move(*info));
            }
        }
        analyzedDumps = std::move(kept);
        dumps.clear();
        std::int64_t oldest = 0;
        for (const auto& [path, info] : analyzedDumps) {
            dumps.push_back(info);
            if (!oldest || info.timeMs < oldest) oldest = info.timeMs;
        }

        Coverage entry{"crash_dumps", "Crash dumps", "ok", "", static_cast<int>(dumps.size()), oldest};
        if (listing.accessDenied || !isElevated()) {
            entry.status = "limited";
            entry.detail = "Run as administrator to read crash dumps, which can name the responsible driver.";
        } else if (dumps.empty()) {
            entry.detail = "Windows has kept no crash dumps.";
        } else {
            entry.detail = "Read locally. No symbols are downloaded.";
        }
        upsertCoverage(entry);
    }

    void scanDrivers(std::int64_t now) {
        auto current = readDrivers();
        const auto previous = store->loadDrivers();
        if (!previous.empty()) store->addChanges(diffDrivers(previous, current, now));
        store->saveDrivers(current);
        drivers = std::move(current);

        const bool hasHistory = options.mode == "live";
        upsertCoverage({"drivers", "Installed drivers", "ok", "Drivers running or attached to a present device.", static_cast<int>(drivers.size()), 0});
        upsertCoverage({"snapshots", "Earlier snapshots", hasHistory ? "ok" : "limited",
                        hasHistory ? "Driver versions are compared every 6 hours."
                                   : "Install Live monitoring to compare with earlier states. Until then, changes come from what Windows has kept.",
                        0, 0});
    }

    void scanUsn() {
        const auto result = readDriverFileChanges(usnJournal, usnCursor);
        if (result.available) {
            store->addChanges(result.changes);
            usnJournal = result.journalId;
            usnCursor = result.nextUsn;
            store->setMeta("usn_journal", std::to_string(usnJournal));
            store->setMeta("usn_cursor", std::to_string(usnCursor));
            if (usnCoverage.oldestMs == 0 || (result.oldestMs && result.oldestMs < usnCoverage.oldestMs)) usnCoverage.oldestMs = result.oldestMs;
            upsertCoverage({"usn", "File change journal", "ok", "Driver files created, changed or deleted. It does not say which program did it.",
                            static_cast<int>(result.changes.size()), usnCoverage.oldestMs});
        } else {
            upsertCoverage({"usn", "File change journal", "limited", result.error, 0, 0});
        }
    }

    void startRecorder() {
        recorder = std::make_unique<ActivityRecorder>();
        recorder->start();
        updateRecorderCoverage();
    }

    void updateRecorderCoverage() {
        if (recorder && recorder->running()) {
            upsertCoverage({"recorder", "Activity recorder", "ok", "Remembers recent process starts, stops and driver loads.", static_cast<int>(recorder->snapshot().size()), 0});
        } else if (recorder) {
            upsertCoverage({"recorder", "Activity recorder", "limited", recorder->error(), 0, 0});
        } else {
            upsertCoverage({"recorder", "Activity recorder", "limited", "Only runs while WHY? is open or Live monitoring is installed.", 0, 0});
        }
    }

    void upsertCoverage(Coverage entry) {
        const auto it = std::find_if(coverage.begin(), coverage.end(), [&](const Coverage& c) { return c.id == entry.id; });
        if (it == coverage.end()) coverage.push_back(std::move(entry));
        else *it = std::move(entry);
    }

    // A case that gained an incident gets a copy of what the recorder saw just before it.
    void captureActivity(const std::vector<Case>& cases, std::int64_t now) {
        for (const auto& c : cases) {
            auto [it, inserted] = caseLastSeen.try_emplace(c.key, c.lastMs);
            const bool changed = !inserted && it->second != c.lastMs;
            it->second = c.lastMs;

            const bool crashType = c.signal == "bsod" || c.signal == "unexpected_restart";
            if (previousSessionCrashed && crashType && !previousSessionActivity.empty() &&
                c.lastMs >= previousSessionActivity.back().timeMs) {
                store->saveActivity(c.key, previousSessionActivity);
                previousSessionActivity.clear();
                continue;
            }
            if (changed && c.severity != Severity::Notice && recorder && recorder->running()) {
                auto records = recorder->snapshot(now, kActivityWindowMs);
                if (records.size() > kMaxActivityPerCase) records.erase(records.begin(), records.end() - kMaxActivityPerCase);
                if (!records.empty()) store->saveActivity(c.key, records);
            }
        }
    }

    void recompute(bool isScanning) {
        const auto now = nowUnixMs();

        InvestigationInput input;
        input.events = store->loadEvents();
        input.dumps = dumps;
        input.drivers = drivers;
        input.overrides = store->loadOverrides();
        input.nowMs = now;
        input.historyStartMs = historyStartMs;

        auto changes = store->loadChanges();
        auto fromEvents = changesFromEvents(input.events);
        changes.insert(changes.end(), fromEvents.begin(), fromEvents.end());
        auto fromInventory = changesFromInventory(drivers, now - static_cast<std::int64_t>(options.historyDays) * kDayMs);
        changes.insert(changes.end(), fromInventory.begin(), fromInventory.end());
        linkUpdatesToDrivers(changes, drivers);
        input.changes = mergeChanges(std::move(changes));

        input.activity = store->loadActivity();
        auto cases = investigate(input);
        captureActivity(cases, now);
        if (const auto activity = store->loadActivity(); activity.size() != input.activity.size()) {
            input.activity = activity;
            cases = investigate(input);
        }

        json timeline = json::array();
        std::vector<const Event*> newest;
        for (const auto& event : input.events) newest.push_back(&event);
        std::sort(newest.begin(), newest.end(), [](const Event* a, const Event* b) { return a->timeMs > b->timeMs; });
        if (newest.size() > kMaxTimeline) newest.resize(kMaxTimeline);
        for (const auto* event : newest) timeline.push_back(*event);

        json integrity = json::array();
        for (const auto& event : input.events) {
            if (event.role == Role::Integrity) integrity.push_back(event);
        }

        auto changeList = input.changes;
        if (changeList.size() > kMaxChanges) changeList.resize(kMaxChanges);

        updateRecorderCoverage();
        if (!integrity.empty()) {
            auto events = coverage.begin();
            for (; events != coverage.end(); ++events) {
                if (events->id == "event_log") break;
            }
            if (events != coverage.end()) {
                events->status = "limited";
                events->detail = "The System log was cleared at some point, so older events are gone.";
            }
        }

        json body{{"schema", 1},
                  {"appVersion", WHY_VERSION},
                  {"releaseName", WHY_RELEASE_NAME},
                  {"mode", options.mode},
                  {"scanning", isScanning},
                  {"elevated", isElevated()},
                  {"machine", {{"computerName", computerName()}, {"os", osDescription()}}},
                  {"historyStartMs", historyStartMs},
                  {"coverage", coverage},
                  {"cases", cases},
                  {"timeline", timeline},
                  {"changes", changeList},
                  {"drivers", drivers},
                  {"integrity", integrity}};

        const std::string fingerprint = body.dump(-1, ' ', false, json::error_handler_t::replace);
        const std::lock_guard lock(stateMutex);
        if (fingerprint == stateFingerprint) return;
        stateFingerprint = fingerprint;
        knownCases.clear();
        for (const auto& c : cases) knownCases.insert(c.key);
        ++rev;
        body["generatedMs"] = now;
        body["rev"] = rev;
        stateJson = std::make_shared<const std::string>(body.dump(-1, ' ', false, json::error_handler_t::replace));
        stateChanged.notify_all();
    }

    void fullScan() {
        const auto now = nowUnixMs();
        scanEvents(options.historyDays);
        scanDrivers(now);
        recompute(true);  // Show what is known so far: dumps can take longer.
        scanDumps();
        scanUsn();
        store->prune(now - static_cast<std::int64_t>(options.historyDays) * kDayMs);
        recompute(false);
    }

    void flushRecorder() {
        if (!recorder || !recorder->running()) return;
        store->saveRecorder(recorder->snapshot());
    }

    void run() {
        // Lowers CPU and I/O priority: the monitor must never compete with what the person is doing.
        SetThreadPriority(GetCurrentThread(), THREAD_MODE_BACKGROUND_BEGIN);

        previousSessionCrashed = store->getMeta("clean_stop").value_or("1") == "0";
        if (previousSessionCrashed) previousSessionActivity = store->loadRecorder();
        store->setMeta("clean_stop", "0");

        startRecorder();
        fullScan();
        watcher = std::make_unique<EventWatcher>([this](Event event) {
            const std::lock_guard lock(queueMutex);
            incoming.push_back(std::move(event));
            queueSignal.notify_one();
        });
        devWatcher = std::make_unique<DevWatcher>([this](Event event) {
            const std::lock_guard lock(queueMutex);
            incoming.push_back(std::move(event));
            queueSignal.notify_one();
        });
        upsertCoverage({"dev", "Development tools", "ok",
                        "Dev servers and commands (Node, Python, .NET, Java, Go and others) that exit with an error, and commands run "
                        "through why-run with their error output.",
                        0, 0});
        recompute(false);

        auto lastFlush = std::chrono::steady_clock::now();
        auto lastInventory = lastFlush;
        auto lastJournal = lastFlush;
        for (;;) {
            std::vector<Event> events;
            std::vector<CaseOverride> overrides;
            {
                std::unique_lock lock(queueMutex);
                queueSignal.wait_for(lock, kRecorderFlushEvery, [&] { return stopping || !incoming.empty() || !incomingOverrides.empty(); });
                if (stopping) break;
                if (!incoming.empty()) {
                    // Crashes and bursts arrive in clusters. Waiting a moment handles them together.
                    queueSignal.wait_for(lock, kDebounce, [&] { return stopping; });
                }
                events.swap(incoming);
                overrides.swap(incomingOverrides);
            }

            bool dirty = store->addEvents(events) > 0;
            for (const auto& item : overrides) {
                store->addOverride(item);
                dirty = true;
            }

            const auto clock = std::chrono::steady_clock::now();
            if (clock - lastInventory >= kInventoryEvery) {
                lastInventory = clock;
                scanDrivers(nowUnixMs());
                dirty = true;
            }
            if (clock - lastJournal >= kJournalEvery) {
                lastJournal = clock;
                scanUsn();
                dirty = true;
            }
            if (std::any_of(events.begin(), events.end(), [](const Event& e) { return e.signal == "bsod"; })) {
                scanDumps();
                dirty = true;
            }
            if (dirty) recompute(false);
            if (clock - lastFlush >= kRecorderFlushEvery) {
                lastFlush = clock;
                flushRecorder();
            }
        }

        watcher.reset();
        devWatcher.reset();
        flushRecorder();
        if (recorder) recorder->stop();
        store->setMeta("clean_stop", "1");
        SetThreadPriority(GetCurrentThread(), THREAD_MODE_BACKGROUND_END);
    }
};

Engine::Engine(EngineOptions options) : impl_(std::make_unique<Impl>(std::move(options))) {}

Engine::~Engine() { stop(); }

void Engine::start() {
    impl_->worker = std::thread([impl = impl_.get()] { impl->run(); });
}

void Engine::stop() {
    {
        const std::lock_guard lock(impl_->queueMutex);
        impl_->stopping = true;
        impl_->queueSignal.notify_all();
    }
    if (impl_->worker.joinable()) impl_->worker.join();
}

void Engine::scanOnce() {
    impl_->fullScan();
}

std::shared_ptr<const std::string> Engine::state() const {
    const std::lock_guard lock(impl_->stateMutex);
    return impl_->stateJson;
}

std::uint64_t Engine::revision() const {
    const std::lock_guard lock(impl_->stateMutex);
    return impl_->rev;
}

std::uint64_t Engine::waitForChange(std::uint64_t known, int timeoutMs) const {
    std::unique_lock lock(impl_->stateMutex);
    impl_->stateChanged.wait_for(lock, std::chrono::milliseconds(timeoutMs), [&] { return impl_->rev != known; });
    return impl_->rev;
}

bool Engine::applyCaseAction(const std::string& key, const std::string& action) {
    if (action != "tried_fix" && action != "reopen") return false;
    {
        const std::lock_guard lock(impl_->stateMutex);
        if (!impl_->knownCases.contains(key)) return false;
    }
    const CaseOverride entry{key, action, nowUnixMs()};
    const std::lock_guard lock(impl_->queueMutex);
    impl_->keepOverride(entry);
    impl_->incomingOverrides.push_back(entry);
    impl_->queueSignal.notify_one();
    return true;
}

std::string makeReport(const std::string& stateJson) {
    json report = json::parse(stateJson);
    const std::string name = report.value("/machine/computerName"_json_pointer, std::string{});
    report["mode"] = "report";
    report["scanning"] = false;
    report["generatedMs"] = nowUnixMs();
    redactJson(report, name);
    return report.dump(2, ' ', false, json::error_handler_t::replace);
}

}  // namespace why

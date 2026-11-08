// Plain data shared by every layer of WHY?. No Windows dependencies.
#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace why {

enum class Area { Hardware, Driver, Software, System, Security, Development };
enum class Severity { Notice, Warning, Critical };

// How much a statement can be trusted. Everything WHY? says carries one of these.
enum class Certainty { Verified, Probable, Correlated };

enum class Role {
    Problem,    // Something went wrong; feeds a case.
    Change,     // Something changed on the system; feeds What Changed.
    Integrity,  // The evidence itself was touched, such as a cleared log.
};

// One thing Windows recorded, already classified by the signal table.
struct Event {
    std::string channel;
    std::uint64_t recordId = 0;
    std::int64_t timeMs = 0;
    std::string provider;
    int eventId = 0;
    int level = 0;
    std::string signal;
    Role role = Role::Problem;
    std::string subject;
    std::string message;
    std::map<std::string, std::string> data;
};

// What a crash dump revealed about a blue screen.
struct DumpInfo {
    std::string fileName;
    std::int64_t timeMs = 0;
    std::uint32_t bugCheck = 0;
    std::uint64_t params[4]{};
    std::string module;          // Empty when the dump does not point at one.
    std::string moduleEvidence;  // "parameter" or "stack".
};

struct DriverInfo {
    std::string service;
    std::string module;  // Lower-case file name, such as nvlddmkm.sys.
    std::string displayName;
    std::string provider;
    std::string version;
    std::string driverDate;
    std::string imagePath;
    std::string deviceClass;
    std::int64_t installedMs = 0;  // Estimated from the driver store folder or the file.
    bool running = false;
};

struct Change {
    std::int64_t timeMs = 0;
    std::string kind;  // driver_installed, driver_updated, driver_removed, file_changed, update_installed.
    std::string name;
    std::string module;
    std::string oldVersion;
    std::string newVersion;
    std::string source;  // snapshot, driver_store, service_install, windows_update, usn.
    bool estimated = false;
};

struct Finding {
    Certainty certainty = Certainty::Verified;
    std::string text;
};

struct Advice {
    std::string text;
    std::string tool;  // Optional allowlisted Windows tool the UI can open.
};

// A process or driver load seen shortly before a problem.
struct ActivityRecord {
    std::int64_t timeMs = 0;
    std::string kind;  // process_start, process_stop, driver_load.
    std::string image;
    std::uint32_t pid = 0;
};

enum class CaseStatus { Open, Quiet, Watching, Recurred, Resolved };

struct Case {
    std::string key;
    std::string signal;
    Area area = Area::System;
    Severity severity = Severity::Warning;
    CaseStatus status = CaseStatus::Open;
    std::string title;
    std::string summary;
    std::string subject;
    std::int64_t firstMs = 0;
    std::int64_t lastMs = 0;
    int incidents = 0;
    int incidentsLast7Days = 0;
    std::string alert;  // severe, repeating, new, increased; empty when the case should stay quiet.
    bool longRunning = false;
    bool causeSupported = false;  // At least one probable cause rests on direct evidence.
    std::vector<Event> chain;
    std::vector<Finding> findings;
    std::vector<Change> changesBefore;
    std::vector<Advice> advice;
    std::vector<ActivityRecord> activity;
    std::string output;  // The last lines a failed command printed, when why-run captured them.
    std::string image;   // The program the case is about, when there is one, so the UI can show its icon.
    // Whether this was a real error or a forced closure: error, forced or unclear. Empty when the question does not apply.
    std::string origin;
    std::string originText;  // Why WHY? says so, in one or two sentences.
    std::int64_t fixedMs = 0;  // When the person last marked it as fixed, 0 if never.
};

// A person's statement about a case, kept apart from the facts.
struct CaseOverride {
    std::string key;
    std::string action;  // tried_fix or reopen.
    std::int64_t atMs = 0;
};

// What a source could and could not provide. Shown in the UI and the report.
struct Coverage {
    std::string id;
    std::string label;
    std::string status;  // ok, limited, unavailable.
    std::string detail;
    int count = 0;
    std::int64_t oldestMs = 0;
};

}  // namespace why

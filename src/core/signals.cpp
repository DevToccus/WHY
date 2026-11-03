#include "core/signals.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <charconv>

#include "core/text.h"

namespace why {
namespace {

constexpr std::array kSignals{
    SignalInfo{"bsod", Area::System, Severity::Critical, Role::Problem},
    SignalInfo{"unexpected_restart", Area::System, Severity::Warning, Role::Problem},
    SignalInfo{"gpu_reset", Area::Driver, Severity::Warning, Role::Problem},
    SignalInfo{"whea_fatal", Area::Hardware, Severity::Critical, Role::Problem},
    SignalInfo{"whea_corrected", Area::Hardware, Severity::Warning, Role::Problem},
    SignalInfo{"disk_failure_predicted", Area::Hardware, Severity::Critical, Role::Problem},
    SignalInfo{"disk_error", Area::Hardware, Severity::Critical, Role::Problem},
    SignalInfo{"disk_retry", Area::Hardware, Severity::Warning, Role::Problem},
    SignalInfo{"storage_reset", Area::Hardware, Severity::Warning, Role::Problem},
    SignalInfo{"filesystem_corruption", Area::Hardware, Severity::Critical, Role::Problem},
    SignalInfo{"driver_load_failed", Area::Driver, Severity::Notice, Role::Problem},
    SignalInfo{"service_crash", Area::Software, Severity::Notice, Role::Problem},
    SignalInfo{"driver_service_installed", Area::Driver, Severity::Notice, Role::Change},
    SignalInfo{"update_installed", Area::System, Severity::Notice, Role::Change},
    SignalInfo{"log_cleared", Area::System, Severity::Notice, Role::Integrity},
};

struct Rule {
    const char* provider;
    int eventId;
    const char* signal;
};

// IDs are only meaningful together with their provider: 153 from "disk" is a retried read,
// while 153 from Kernel-Boot is a routine boot record.
constexpr std::array kRules{
    Rule{"Microsoft-Windows-WER-SystemErrorReporting", 1001, "bsod"},
    Rule{"BugCheck", 1001, "bsod"},
    Rule{"Microsoft-Windows-Kernel-Power", 41, "unexpected_restart"},
    Rule{"EventLog", 6008, "unexpected_restart"},
    Rule{"Display", 4101, "gpu_reset"},
    Rule{"nvlddmkm", 13, "gpu_reset"},
    Rule{"nvlddmkm", 14, "gpu_reset"},
    Rule{"nvlddmkm", 153, "gpu_reset"},
    Rule{"amdkmdag", 4101, "gpu_reset"},
    Rule{"Microsoft-Windows-WHEA-Logger", 1, "whea_fatal"},
    Rule{"Microsoft-Windows-WHEA-Logger", 18, "whea_fatal"},
    Rule{"Microsoft-Windows-WHEA-Logger", 20, "whea_fatal"},
    Rule{"Microsoft-Windows-WHEA-Logger", 46, "whea_fatal"},
    Rule{"Microsoft-Windows-WHEA-Logger", 17, "whea_corrected"},
    Rule{"Microsoft-Windows-WHEA-Logger", 19, "whea_corrected"},
    Rule{"Microsoft-Windows-WHEA-Logger", 47, "whea_corrected"},
    Rule{"disk", 52, "disk_failure_predicted"},
    Rule{"disk", 7, "disk_error"},
    Rule{"disk", 11, "disk_error"},
    Rule{"disk", 51, "disk_error"},
    Rule{"disk", 153, "disk_retry"},
    Rule{"stornvme", 129, "storage_reset"},
    Rule{"storahci", 129, "storage_reset"},
    Rule{"iaStorAC", 129, "storage_reset"},
    Rule{"iaStorAVC", 129, "storage_reset"},
    Rule{"iaStorVD", 129, "storage_reset"},
    Rule{"Ntfs", 55, "filesystem_corruption"},
    Rule{"Microsoft-Windows-Ntfs", 55, "filesystem_corruption"},
    Rule{"Microsoft-Windows-Kernel-PnP", 219, "driver_load_failed"},
    Rule{"Service Control Manager", 7026, "driver_load_failed"},
    Rule{"Service Control Manager", 7031, "service_crash"},
    Rule{"Service Control Manager", 7034, "service_crash"},
    Rule{"Service Control Manager", 7045, "driver_service_installed"},
    Rule{"Microsoft-Windows-WindowsUpdateClient", 19, "update_installed"},
    Rule{"Microsoft-Windows-Eventlog", 104, "log_cleared"},
};

const std::string& dataOf(const Event& event, const char* key) {
    static const std::string kEmpty;
    const auto it = event.data.find(key);
    return it == event.data.end() ? kEmpty : it->second;
}

// Windows logs these on almost every PC with no effect on the device.
bool isKnownHarmlessDriverFailure(std::string_view stem) {
    return stem == "wudfrd" || stem == "wudfpf";
}

}  // namespace

const SignalInfo* signalInfo(std::string_view key) {
    for (const auto& signal : kSignals) {
        if (key == signal.key) return &signal;
    }
    return nullptr;
}

const std::vector<ProviderQuery>& signalQueries() {
    static const std::vector<ProviderQuery> queries = [] {
        std::vector<ProviderQuery> out;
        for (const auto& rule : kRules) {
            auto it = std::find_if(out.begin(), out.end(),
                                   [&](const ProviderQuery& q) { return q.provider == rule.provider; });
            if (it == out.end()) {
                out.push_back({rule.provider, {}});
                it = out.end() - 1;
            }
            it->eventIds.push_back(rule.eventId);
        }
        return out;
    }();
    return queries;
}

bool classify(Event& event) {
    const Rule* match = nullptr;
    for (const auto& rule : kRules) {
        if (rule.eventId == event.eventId && event.provider == rule.provider) {
            match = &rule;
            break;
        }
    }
    if (!match) return false;

    const SignalInfo* info = signalInfo(match->signal);
    event.signal = info->key;
    event.role = info->role;

    const std::string_view signal = info->key;
    if (signal == "gpu_reset") {
        const auto& named = dataOf(event, "param1");
        event.subject = event.provider == "Display" && !named.empty() ? moduleStem(named) : toLower(event.provider);
    } else if (signal == "driver_load_failed") {
        const auto& driver = event.eventId == 219 ? dataOf(event, "DriverName") : dataOf(event, "param1");
        event.subject = moduleStem(trim(driver));
        if (isKnownHarmlessDriverFailure(event.subject)) return false;
    } else if (signal == "service_crash" || signal.starts_with("disk") || signal == "filesystem_corruption" ||
               signal == "storage_reset") {
        event.subject = trim(dataOf(event, "param1"));
        if (event.subject.empty()) event.subject = trim(dataOf(event, "DriveName"));
    } else if (signal == "driver_service_installed") {
        // 7045 covers every kind of service. Only kernel drivers concern What Changed.
        const auto& image = dataOf(event, "ImagePath");
        if (!endsWithNoCase(trim(image), ".sys")) return false;
        event.subject = dataOf(event, "ServiceName");
    } else if (signal == "update_installed") {
        event.subject = dataOf(event, "updateTitle");
        if (isRoutineUpdateTitle(event.subject)) return false;
    }
    return true;
}

bool isRoutineUpdateTitle(std::string_view title) {
    // Defender definitions and the malware removal tool arrive almost daily and never explain a failure.
    if (containsNoCase(title, "KB2267602") || containsNoCase(title, "KB4052623") || containsNoCase(title, "KB890830")) return true;
    // Microsoft Store apps are titled "9NRZT3Q9R3DL-Publisher.App".
    const auto dash = title.find('-');
    if (dash != 12) return false;
    return std::all_of(title.begin(), title.begin() + 12, [](unsigned char c) { return std::isdigit(c) || std::isupper(c); });
}

std::uint32_t bugCheckFromReport(std::string_view text) {
    const auto pos = text.find("0x");
    if (pos == std::string_view::npos) return 0;
    const char* first = text.data() + pos + 2;
    std::uint64_t value = 0;
    std::from_chars(first, text.data() + text.size(), value, 16);
    return static_cast<std::uint32_t>(value);
}

bool isDriverUpdateTitle(std::string_view title) {
    // Driver updates follow "Vendor - Class - Version". Other updates name a KB article instead.
    const auto first = title.find(" - ");
    if (first == std::string_view::npos) return false;
    const auto second = title.find(" - ", first + 3);
    if (second == std::string_view::npos) return false;
    return !containsNoCase(title, "KB");
}

}  // namespace why

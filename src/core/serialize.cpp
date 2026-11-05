#include "core/serialize.h"

using nlohmann::json;

namespace why {

const char* toString(Area area) {
    switch (area) {
        case Area::Hardware: return "hardware";
        case Area::Driver: return "driver";
        case Area::Software: return "software";
        case Area::System: return "system";
        case Area::Security: return "security";
        case Area::Development: return "development";
    }
    return "system";
}

const char* toString(Severity severity) {
    switch (severity) {
        case Severity::Notice: return "notice";
        case Severity::Warning: return "warning";
        case Severity::Critical: return "critical";
    }
    return "notice";
}

const char* toString(Certainty certainty) {
    switch (certainty) {
        case Certainty::Verified: return "verified";
        case Certainty::Probable: return "probable";
        case Certainty::Correlated: return "correlated";
    }
    return "correlated";
}

const char* toString(CaseStatus status) {
    switch (status) {
        case CaseStatus::Open: return "open";
        case CaseStatus::Quiet: return "quiet";
        case CaseStatus::Watching: return "watching";
        case CaseStatus::Recurred: return "recurred";
        case CaseStatus::Resolved: return "resolved";
    }
    return "open";
}

void to_json(json& j, const Event& v) {
    j = json{{"timeMs", v.timeMs},   {"provider", v.provider}, {"eventId", v.eventId}, {"signal", v.signal},
             {"subject", v.subject}, {"message", v.message},   {"channel", v.channel}};
}

void to_json(json& j, const Change& v) {
    j = json{{"timeMs", v.timeMs},         {"kind", v.kind},           {"name", v.name},
             {"module", v.module},         {"oldVersion", v.oldVersion}, {"newVersion", v.newVersion},
             {"source", v.source},         {"estimated", v.estimated}};
}

void from_json(const json& j, Change& v) {
    j.at("timeMs").get_to(v.timeMs);
    j.at("kind").get_to(v.kind);
    j.at("name").get_to(v.name);
    j.at("module").get_to(v.module);
    j.at("oldVersion").get_to(v.oldVersion);
    j.at("newVersion").get_to(v.newVersion);
    j.at("source").get_to(v.source);
    j.at("estimated").get_to(v.estimated);
}

void to_json(json& j, const DriverInfo& v) {
    j = json{{"service", v.service},       {"module", v.module},         {"displayName", v.displayName},
             {"provider", v.provider},     {"version", v.version},       {"driverDate", v.driverDate},
             {"imagePath", v.imagePath},   {"deviceClass", v.deviceClass}, {"installedMs", v.installedMs},
             {"running", v.running}};
}

void from_json(const json& j, DriverInfo& v) {
    j.at("service").get_to(v.service);
    j.at("module").get_to(v.module);
    j.at("displayName").get_to(v.displayName);
    j.at("provider").get_to(v.provider);
    j.at("version").get_to(v.version);
    j.at("driverDate").get_to(v.driverDate);
    j.at("imagePath").get_to(v.imagePath);
    j.at("deviceClass").get_to(v.deviceClass);
    j.at("installedMs").get_to(v.installedMs);
    j.at("running").get_to(v.running);
}

void to_json(json& j, const ActivityRecord& v) {
    j = json{{"timeMs", v.timeMs}, {"kind", v.kind}, {"image", v.image}, {"pid", v.pid}};
}

void from_json(const json& j, ActivityRecord& v) {
    j.at("timeMs").get_to(v.timeMs);
    j.at("kind").get_to(v.kind);
    j.at("image").get_to(v.image);
    j.at("pid").get_to(v.pid);
}

void to_json(json& j, const Coverage& v) {
    j = json{{"id", v.id},         {"label", v.label}, {"status", v.status},
             {"detail", v.detail}, {"count", v.count}, {"oldestMs", v.oldestMs}};
}

void to_json(json& j, const Case& v) {
    json findings = json::array();
    for (const auto& finding : v.findings) {
        findings.push_back({{"certainty", toString(finding.certainty)}, {"text", finding.text}});
    }
    json advice = json::array();
    for (const auto& item : v.advice) advice.push_back({{"text", item.text}, {"tool", item.tool}});

    j = json{{"key", v.key},
             {"signal", v.signal},
             {"area", toString(v.area)},
             {"severity", toString(v.severity)},
             {"status", toString(v.status)},
             {"title", v.title},
             {"summary", v.summary},
             {"subject", v.subject},
             {"firstMs", v.firstMs},
             {"lastMs", v.lastMs},
             {"incidents", v.incidents},
             {"incidentsLast7Days", v.incidentsLast7Days},
             {"alert", v.alert},
             {"longRunning", v.longRunning},
             {"causeSupported", v.causeSupported},
             {"chain", v.chain},
             {"findings", findings},
             {"changesBefore", v.changesBefore},
             {"advice", advice},
             {"activity", v.activity},
             {"output", v.output},
             {"image", v.image}};
}

}  // namespace why

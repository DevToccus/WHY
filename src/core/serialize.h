// JSON conversions for everything the store, the UI and reports exchange.
#pragma once

#include <nlohmann/json.hpp>

#include "core/types.h"

namespace why {

const char* toString(Area area);
const char* toString(Severity severity);
const char* toString(Certainty certainty);
const char* toString(CaseStatus status);

void to_json(nlohmann::json& j, const Event& v);
void to_json(nlohmann::json& j, const Change& v);
void from_json(const nlohmann::json& j, Change& v);
void to_json(nlohmann::json& j, const DriverInfo& v);
void from_json(const nlohmann::json& j, DriverInfo& v);
void to_json(nlohmann::json& j, const ActivityRecord& v);
void from_json(const nlohmann::json& j, ActivityRecord& v);
void to_json(nlohmann::json& j, const Coverage& v);
void to_json(nlohmann::json& j, const Case& v);

}  // namespace why

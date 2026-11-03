// Removes personal details from anything that leaves the machine in a report.
#pragma once

#include <nlohmann/json.hpp>
#include <string>
#include <string_view>

namespace why {

// Replaces the account name in \Users\name\ paths.
std::string redactText(std::string_view text);
// Applies redactText to every string in the document, and hides computerName wherever it appears.
void redactJson(nlohmann::json& document, std::string_view computerName);

}  // namespace why

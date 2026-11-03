// Parses the XML Windows renders for an event. Only the shape Windows produces is supported.
#pragma once

#include <optional>
#include <string_view>

#include "core/types.h"

namespace why {

// Fills everything except the classification. Unnamed <Data> elements become param1, param2...
std::optional<Event> parseEventXml(std::string_view xml);

}  // namespace why

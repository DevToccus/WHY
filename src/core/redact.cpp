#include "core/redact.h"

#include "core/text.h"

namespace why {
namespace {

std::string replaceAllNoCase(std::string text, std::string_view needle, std::string_view replacement) {
    if (needle.empty()) return text;
    const std::string lowerNeedle = toLower(needle);
    std::string lowerText = toLower(text);
    std::size_t pos = 0;
    while ((pos = lowerText.find(lowerNeedle, pos)) != std::string::npos) {
        text.replace(pos, needle.size(), replacement);
        lowerText.replace(pos, needle.size(), replacement);
        pos += replacement.size();
    }
    return text;
}

}  // namespace

std::string redactText(std::string_view text) {
    static constexpr std::string_view kMarker = "\\users\\";
    static constexpr std::string_view kPlaceholder = "<user>";
    std::string out(text);
    std::string lower = toLower(text);
    std::size_t pos = 0;
    while ((pos = lower.find(kMarker, pos)) != std::string::npos) {
        const auto start = pos + kMarker.size();
        auto end = lower.find_first_of("\\/\" ", start);
        if (end == std::string::npos) end = lower.size();
        const std::string_view name = std::string_view(lower).substr(start, end - start);
        const bool shared = name == "public" || name == "default" || name == "all users";
        if (!shared && end > start) {
            out.replace(start, end - start, kPlaceholder);
            lower.replace(start, end - start, kPlaceholder);
            end = start + kPlaceholder.size();
        }
        pos = end;
    }
    return out;
}

void redactJson(nlohmann::json& document, std::string_view computerName) {
    if (document.is_string()) {
        document = replaceAllNoCase(redactText(document.get<std::string>()), computerName, "<computer>");
    } else if (document.is_object() || document.is_array()) {
        for (auto& item : document) redactJson(item, computerName);
    }
}

}  // namespace why

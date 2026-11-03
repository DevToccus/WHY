#include "core/event_xml.h"

#include <charconv>

#include "core/text.h"

namespace why {
namespace {

std::string decodeEntities(std::string_view text) {
    std::string out;
    out.reserve(text.size());
    for (std::size_t i = 0; i < text.size(); ++i) {
        if (text[i] != '&') {
            out.push_back(text[i]);
            continue;
        }
        const auto end = text.find(';', i);
        if (end == std::string_view::npos) {
            out.push_back('&');
            continue;
        }
        const auto entity = text.substr(i + 1, end - i - 1);
        if (entity == "amp") out.push_back('&');
        else if (entity == "lt") out.push_back('<');
        else if (entity == "gt") out.push_back('>');
        else if (entity == "quot") out.push_back('"');
        else if (entity == "apos") out.push_back('\'');
        else out.append(text.substr(i, end - i + 1));
        i = end;
    }
    return out;
}

bool tagEndsAt(std::string_view xml, std::size_t pos) {
    return pos < xml.size() && (xml[pos] == '>' || xml[pos] == ' ' || xml[pos] == '/');
}

// Start of the first <tag> element, ignoring longer names that share the prefix.
std::size_t findTag(std::string_view xml, std::string_view tag, std::size_t from = 0) {
    const std::string open = "<" + std::string(tag);
    auto pos = from;
    while ((pos = xml.find(open, pos)) != std::string_view::npos) {
        if (tagEndsAt(xml, pos + open.size())) return pos;
        pos += open.size();
    }
    return std::string_view::npos;
}

std::string_view openingTag(std::string_view xml, std::string_view tag) {
    const auto pos = findTag(xml, tag);
    if (pos == std::string_view::npos) return {};
    const auto gt = xml.find('>', pos);
    if (gt == std::string_view::npos) return {};
    return xml.substr(pos, gt - pos);
}

std::string_view elementText(std::string_view xml, std::string_view tag) {
    const auto pos = findTag(xml, tag);
    if (pos == std::string_view::npos) return {};
    const auto gt = xml.find('>', pos);
    if (gt == std::string_view::npos || xml[gt - 1] == '/') return {};
    const auto close = xml.find("</", gt);
    if (close == std::string_view::npos) return {};
    return xml.substr(gt + 1, close - gt - 1);
}

std::string_view attribute(std::string_view tagText, std::string_view attr) {
    const std::string needle = " " + std::string(attr) + "=";
    auto pos = tagText.find(needle);
    if (pos == std::string_view::npos || pos + needle.size() >= tagText.size()) return {};
    pos += needle.size();
    const char quote = tagText[pos];
    if (quote != '\'' && quote != '"') return {};
    const auto end = tagText.find(quote, pos + 1);
    if (end == std::string_view::npos) return {};
    return tagText.substr(pos + 1, end - pos - 1);
}

template <typename T>
T parseNumber(std::string_view text) {
    T value{};
    std::from_chars(text.data(), text.data() + text.size(), value);
    return value;
}

void readData(std::string_view xml, Event& event) {
    int unnamed = 0;
    std::size_t pos = 0;
    while ((pos = findTag(xml, "Data", pos)) != std::string_view::npos) {
        const auto gt = xml.find('>', pos);
        if (gt == std::string_view::npos) return;
        const auto attr = attribute(xml.substr(pos, gt - pos), "Name");
        const std::string name = attr.empty() ? "param" + std::to_string(++unnamed) : decodeEntities(attr);
        if (xml[gt - 1] == '/') {
            event.data[name] = "";
            pos = gt + 1;
            continue;
        }
        const auto close = xml.find("</Data>", gt);
        if (close == std::string_view::npos) return;
        event.data[name] = decodeEntities(xml.substr(gt + 1, close - gt - 1));
        pos = close + 7;
    }
}

}  // namespace

std::optional<Event> parseEventXml(std::string_view xml) {
    const auto systemEnd = xml.find("</System>");
    if (systemEnd == std::string_view::npos) return std::nullopt;
    const auto system = xml.substr(0, systemEnd);

    Event event;
    event.provider = decodeEntities(attribute(openingTag(system, "Provider"), "Name"));
    event.eventId = parseNumber<int>(elementText(system, "EventID"));
    event.level = parseNumber<int>(elementText(system, "Level"));
    event.recordId = parseNumber<std::uint64_t>(elementText(system, "EventRecordID"));
    event.channel = decodeEntities(elementText(system, "Channel"));
    event.timeMs = parseIsoUtcMs(attribute(openingTag(system, "TimeCreated"), "SystemTime"));
    if (event.provider.empty() || event.eventId == 0 || event.timeMs == 0) return std::nullopt;

    readData(xml.substr(systemEnd), event);
    return event;
}

}  // namespace why

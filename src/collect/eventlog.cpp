#include "collect/eventlog.h"

#include <windows.h>
#include <winevt.h>
#include <wil/resource.h>

#include <map>
#include <optional>
#include <vector>

#include "core/event_xml.h"
#include "core/signals.h"
#include "core/text.h"
#include "engine/winutil.h"

namespace why {
namespace {

using EventHandle = wil::unique_any<EVT_HANDLE, decltype(&EvtClose), EvtClose>;

std::wstring buildQuery(int days) {
    std::wstring xml = L"<QueryList><Query Id=\"0\" Path=\"System\">";
    const std::wstring window = days > 0 ? L" and TimeCreated[timediff(@SystemTime) &lt;= " +
                                               std::to_wstring(static_cast<std::int64_t>(days) * 86400000) + L"]"
                                         : L"";
    for (const auto& query : signalQueries()) {
        if (query.channel == kDevChannel) continue;
        xml += L"<Select Path=\"" + toWide(query.channel) + L"\">*[System[Provider[@Name='" + toWide(query.provider) + L"'] and (";
        for (std::size_t i = 0; i < query.eventIds.size(); ++i) {
            if (i) xml += L" or ";
            xml += L"EventID=" + std::to_wstring(query.eventIds[i]);
        }
        xml += L")" + window + L"]]</Select>";
    }
    return xml + L"</Query></QueryList>";
}

std::optional<std::wstring> renderXml(EVT_HANDLE event) {
    DWORD used = 0;
    DWORD properties = 0;
    if (EvtRender(nullptr, event, EvtRenderEventXml, 0, nullptr, &used, &properties)) return std::nullopt;
    if (GetLastError() != ERROR_INSUFFICIENT_BUFFER) return std::nullopt;
    std::wstring xml(used / sizeof(wchar_t), L'\0');
    if (!EvtRender(nullptr, event, EvtRenderEventXml, used, xml.data(), &used, &properties)) return std::nullopt;
    if (!xml.empty() && xml.back() == L'\0') xml.pop_back();
    return xml;
}

// Windows formats messages from each provider's own resources, so they appear in the system language.
class MessageFormatter {
public:
    std::string format(EVT_HANDLE event, const std::string& provider) {
        auto [it, inserted] = publishers_.try_emplace(provider);
        if (inserted) it->second.reset(EvtOpenPublisherMetadata(nullptr, toWide(provider).c_str(), nullptr, 0, 0));
        if (!it->second) return {};
        DWORD used = 0;
        EvtFormatMessage(it->second.get(), event, 0, 0, nullptr, EvtFormatMessageEvent, 0, nullptr, &used);
        if (used == 0 || GetLastError() != ERROR_INSUFFICIENT_BUFFER) return {};
        std::wstring text(used, L'\0');
        if (!EvtFormatMessage(it->second.get(), event, 0, 0, nullptr, EvtFormatMessageEvent, used, text.data(), &used)) return {};
        if (!text.empty() && text.back() == L'\0') text.pop_back();
        return trim(toUtf8(text));
    }

private:
    std::map<std::string, EventHandle> publishers_;
};

std::optional<Event> readEvent(EVT_HANDLE handle, MessageFormatter& formatter) {
    const auto xml = renderXml(handle);
    if (!xml) return std::nullopt;
    auto event = parseEventXml(toUtf8(*xml));
    if (!event || !classify(*event)) return std::nullopt;
    if (event->role == Role::Problem) event->message = formatter.format(handle, event->provider);
    return event;
}

std::int64_t oldestSystemRecordMs() {
    EventHandle query(EvtQuery(nullptr, L"System", L"*", EvtQueryChannelPath | EvtQueryForwardDirection));
    if (!query) return 0;
    EVT_HANDLE handle = nullptr;
    DWORD returned = 0;
    if (!EvtNext(query.get(), 1, &handle, INFINITE, 0, &returned) || returned == 0) return 0;
    EventHandle event(handle);
    const auto xml = renderXml(event.get());
    if (!xml) return 0;
    const auto parsed = parseEventXml(toUtf8(*xml));
    return parsed ? parsed->timeMs : 0;
}

}  // namespace

EventLogHistory readEventHistory(int days) {
    EventLogHistory history;
    history.oldestLogMs = oldestSystemRecordMs();

    const std::wstring xml = buildQuery(days);
    EventHandle query(EvtQuery(nullptr, nullptr, xml.c_str(), EvtQueryChannelPath | EvtQueryTolerateQueryErrors));
    if (!query) {
        history.error = "The Windows logs could not be read (error " + std::to_string(GetLastError()) + ").";
        return history;
    }

    MessageFormatter formatter;
    EVT_HANDLE batch[64];
    DWORD returned = 0;
    while (EvtNext(query.get(), ARRAYSIZE(batch), batch, INFINITE, 0, &returned)) {
        for (DWORD i = 0; i < returned; ++i) {
            EventHandle event(batch[i]);
            if (auto parsed = readEvent(event.get(), formatter)) history.events.push_back(std::move(*parsed));
        }
    }
    if (const DWORD error = GetLastError(); error != ERROR_NO_MORE_ITEMS) {
        history.error = "Reading the Windows logs stopped early (error " + std::to_string(error) + ").";
    }
    return history;
}

struct EventWatcher::Impl {
    Callback callback;
    MessageFormatter formatter;
    EventHandle subscription;

    static DWORD WINAPI onEvent(EVT_SUBSCRIBE_NOTIFY_ACTION action, PVOID context, EVT_HANDLE handle) {
        auto* self = static_cast<Impl*>(context);
        if (action == EvtSubscribeActionDeliver) {
            if (auto event = readEvent(handle, self->formatter)) self->callback(std::move(*event));
        }
        return ERROR_SUCCESS;
    }
};

EventWatcher::EventWatcher(Callback callback) : impl_(std::make_unique<Impl>()) {
    impl_->callback = std::move(callback);
    const std::wstring xml = buildQuery(0);
    impl_->subscription.reset(EvtSubscribe(nullptr, nullptr, nullptr, xml.c_str(), nullptr, impl_.get(), &Impl::onEvent,
                                           EvtSubscribeToFutureEvents | EvtSubscribeTolerateQueryErrors));
}

EventWatcher::~EventWatcher() = default;

bool EventWatcher::active() const { return static_cast<bool>(impl_->subscription); }

}  // namespace why

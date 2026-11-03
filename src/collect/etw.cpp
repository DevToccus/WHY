#include "collect/etw.h"

// clang-format off
#include <windows.h>
#include <evntrace.h>
#include <evntcons.h>
#include <tdh.h>
// clang-format on

#include <atomic>
#include <deque>
#include <mutex>
#include <thread>
#include <vector>

#include "core/text.h"
#include "engine/winutil.h"

namespace why {
namespace {

// Microsoft-Windows-Kernel-Process
constexpr GUID kProcessProvider = {0x22FB2CD6, 0x0E7B, 0x422B, {0xA0, 0xC7, 0x2F, 0xAD, 0x1F, 0xD0, 0xE7, 0x16}};
constexpr ULONGLONG kKeywordProcess = 0x10;
constexpr ULONGLONG kKeywordImage = 0x40;
constexpr USHORT kEventProcessStart = 1;
constexpr USHORT kEventProcessStop = 2;
constexpr USHORT kEventImageLoad = 5;

constexpr std::size_t kCapacity = 2000;
constexpr const wchar_t* kSessionName = L"WHY-Recorder";

struct SessionProperties {
    EVENT_TRACE_PROPERTIES props;
    wchar_t name[64];
};

SessionProperties makeProperties() {
    SessionProperties session{};
    session.props.Wnode.BufferSize = sizeof(SessionProperties);
    session.props.Wnode.Flags = WNODE_FLAG_TRACED_GUID;
    session.props.Wnode.ClientContext = 1;
    session.props.LogFileMode = EVENT_TRACE_REAL_TIME_MODE;
    session.props.LoggerNameOffset = offsetof(SessionProperties, name);
    // Buffer counts stay at 0 so Windows sizes them. It needs at least two per processor, and a lower
    // maximum leaves a session that starts normally but never delivers anything.
    session.props.BufferSize = 64;  // KB per buffer.
    session.props.FlushTimer = 1;
    return session;
}

// ProcessStart and ImageLoad carry the name in UTF-16, but ProcessStop carries a bare ANSI file name.
std::wstring readImageName(PEVENT_RECORD record, bool ansi) {
    PROPERTY_DATA_DESCRIPTOR descriptor{};
    descriptor.PropertyName = reinterpret_cast<ULONGLONG>(L"ImageName");
    descriptor.ArrayIndex = ULONG_MAX;
    ULONG size = 0;
    if (TdhGetPropertySize(record, 0, nullptr, 1, &descriptor, &size) != ERROR_SUCCESS || size == 0 || size > 4096) return {};
    std::vector<BYTE> bytes(size);
    if (TdhGetProperty(record, 0, nullptr, 1, &descriptor, size, bytes.data()) != ERROR_SUCCESS) return {};

    std::wstring value;
    if (ansi) {
        const int length = MultiByteToWideChar(CP_ACP, 0, reinterpret_cast<const char*>(bytes.data()), static_cast<int>(size), nullptr, 0);
        value.resize(static_cast<std::size_t>(length));
        MultiByteToWideChar(CP_ACP, 0, reinterpret_cast<const char*>(bytes.data()), static_cast<int>(size), value.data(), length);
    } else {
        value.assign(reinterpret_cast<const wchar_t*>(bytes.data()), size / sizeof(wchar_t));
    }
    while (!value.empty() && value.back() == L'\0') value.pop_back();
    return value;
}

// \Device\HarddiskVolume3\Windows\x.exe becomes \Windows\x.exe. The volume number means nothing to a reader.
std::string cleanImagePath(const std::wstring& image) {
    constexpr std::wstring_view kDevice = L"\\Device\\";
    std::wstring_view view(image);
    if (view.starts_with(kDevice)) {
        const auto next = view.find(L'\\', kDevice.size());
        if (next != std::wstring_view::npos) view.remove_prefix(next);
    }
    return toUtf8(view);
}

}  // namespace

struct ActivityRecorder::Impl {
    mutable std::mutex mutex;
    std::deque<ActivityRecord> records;
    std::thread thread;
    TRACEHANDLE session = 0;
    TRACEHANDLE trace = INVALID_PROCESSTRACE_HANDLE;
    std::atomic<bool> running{false};
    std::atomic<std::uint64_t> seen{0};
    std::string error;

    void add(ActivityRecord record) {
        const std::lock_guard lock(mutex);
        records.push_back(std::move(record));
        if (records.size() > kCapacity) records.pop_front();
    }

    static VOID WINAPI onEvent(PEVENT_RECORD record) {
        auto* self = static_cast<Impl*>(record->UserContext);
        ++self->seen;
        const USHORT id = record->EventHeader.EventDescriptor.Id;
        if (id != kEventProcessStart && id != kEventProcessStop && id != kEventImageLoad) return;

        const std::wstring image = readImageName(record, id == kEventProcessStop);
        if (image.empty()) return;
        // DLL loads are constant and tell little here. Only kernel drivers are kept.
        if (id == kEventImageLoad && !endsWithNoCase(toUtf8(image), ".sys")) return;

        ActivityRecord out;
        out.timeMs = fileTimeToUnixMs(static_cast<std::uint64_t>(record->EventHeader.TimeStamp.QuadPart));
        out.kind = id == kEventProcessStart ? "process_start" : id == kEventProcessStop ? "process_stop" : "driver_load";
        out.image = cleanImagePath(image);
        out.pid = record->EventHeader.ProcessId;
        self->add(std::move(out));
    }
};

ActivityRecorder::ActivityRecorder() : impl_(std::make_unique<Impl>()) {}

ActivityRecorder::~ActivityRecorder() { stop(); }

bool ActivityRecorder::start() {
    if (running()) return true;

    auto properties = makeProperties();
    TRACEHANDLE session = 0;
    ULONG status = StartTraceW(&session, kSessionName, &properties.props);
    if (status == ERROR_ALREADY_EXISTS) {
        // Left behind by a previous run that did not exit cleanly.
        auto stale = makeProperties();
        ControlTraceW(0, kSessionName, &stale.props, EVENT_TRACE_CONTROL_STOP);
        properties = makeProperties();
        status = StartTraceW(&session, kSessionName, &properties.props);
    }
    if (status != ERROR_SUCCESS) {
        impl_->error = status == ERROR_ACCESS_DENIED ? "Needs administrator rights." : "The trace session could not start (error " + std::to_string(status) + ").";
        return false;
    }
    impl_->session = session;

    status = EnableTraceEx2(session, &kProcessProvider, EVENT_CONTROL_CODE_ENABLE_PROVIDER, TRACE_LEVEL_INFORMATION,
                            kKeywordProcess | kKeywordImage, 0, 0, nullptr);
    if (status != ERROR_SUCCESS) {
        impl_->error = status == ERROR_ACCESS_DENIED ? "Needs administrator rights." : "The process provider could not be enabled (error " + std::to_string(status) + ").";
        stop();
        return false;
    }

    EVENT_TRACE_LOGFILEW logfile{};
    logfile.LoggerName = const_cast<LPWSTR>(kSessionName);
    logfile.ProcessTraceMode = PROCESS_TRACE_MODE_REAL_TIME | PROCESS_TRACE_MODE_EVENT_RECORD;
    logfile.EventRecordCallback = &Impl::onEvent;
    logfile.Context = impl_.get();
    impl_->trace = OpenTraceW(&logfile);
    if (impl_->trace == INVALID_PROCESSTRACE_HANDLE) {
        impl_->error = "The trace session could not be opened (error " + std::to_string(GetLastError()) + ").";
        stop();
        return false;
    }

    impl_->running = true;
    impl_->thread = std::thread([impl = impl_.get()] {
        SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_BELOW_NORMAL);
        const ULONG result = ProcessTrace(&impl->trace, 1, nullptr, nullptr);
        if (result != ERROR_SUCCESS && result != ERROR_CANCELLED) impl->error = "The trace stopped (error " + std::to_string(result) + ").";
        impl->running = false;
    });
    return true;
}

void ActivityRecorder::stop() {
    if (impl_->session != 0) {
        auto properties = makeProperties();
        ControlTraceW(impl_->session, nullptr, &properties.props, EVENT_TRACE_CONTROL_STOP);
        impl_->session = 0;
    }
    if (impl_->trace != INVALID_PROCESSTRACE_HANDLE) {
        CloseTrace(impl_->trace);
        impl_->trace = INVALID_PROCESSTRACE_HANDLE;
    }
    if (impl_->thread.joinable()) impl_->thread.join();
    impl_->running = false;
}

bool ActivityRecorder::running() const { return impl_->running; }

const std::string& ActivityRecorder::error() const { return impl_->error; }

std::uint64_t ActivityRecorder::eventsSeen() const { return impl_->seen; }

std::string ActivityRecorder::sessionStats() const {
    auto properties = makeProperties();
    const ULONG status = ControlTraceW(0, kSessionName, &properties.props, EVENT_TRACE_CONTROL_QUERY);
    if (status != ERROR_SUCCESS) return "query failed: " + std::to_string(status);
    return "buffers=" + std::to_string(properties.props.NumberOfBuffers) + " written=" + std::to_string(properties.props.BuffersWritten) +
           " eventsLost=" + std::to_string(properties.props.EventsLost) + " realTimeLost=" + std::to_string(properties.props.RealTimeBuffersLost);
}

std::vector<ActivityRecord> ActivityRecorder::snapshot(std::int64_t nowMs, std::int64_t maxAgeMs) const {
    const std::lock_guard lock(impl_->mutex);
    std::vector<ActivityRecord> out;
    out.reserve(impl_->records.size());
    for (const auto& record : impl_->records) {
        if (maxAgeMs > 0 && nowMs - record.timeMs > maxAgeMs) continue;
        out.push_back(record);
    }
    return out;
}

}  // namespace why

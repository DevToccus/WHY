#include "collect/usn.h"

#include <windows.h>
#include <winioctl.h>
#include <wil/resource.h>

#include <map>
#include <vector>

#include "core/text.h"
#include "engine/winutil.h"

namespace why {
namespace {

constexpr std::int64_t kDayMs = 24 * 3600 * 1000;

const char* describeReason(DWORD reason) {
    if (reason & USN_REASON_FILE_DELETE) return "deleted";
    if (reason & USN_REASON_FILE_CREATE) return "created";
    if (reason & USN_REASON_RENAME_NEW_NAME) return "renamed";
    return "modified";
}

}  // namespace

UsnResult readDriverFileChanges(std::uint64_t journalId, std::int64_t fromUsn) {
    UsnResult result;
    const std::wstring volume = L"\\\\.\\" + windowsDirectory().substr(0, 2);
    wil::unique_hfile handle(CreateFileW(volume.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr));
    if (!handle) {
        result.error = GetLastError() == ERROR_ACCESS_DENIED ? "Needs administrator rights." : "The volume could not be opened.";
        return result;
    }

    USN_JOURNAL_DATA_V0 journal{};
    DWORD returned = 0;
    if (!DeviceIoControl(handle.get(), FSCTL_QUERY_USN_JOURNAL, nullptr, 0, &journal, sizeof(journal), &returned, nullptr)) {
        result.error = "This volume has no change journal.";
        return result;
    }

    result.available = true;
    result.journalId = journal.UsnJournalID;
    result.journalBytes = journal.NextUsn - journal.FirstUsn;

    READ_USN_JOURNAL_DATA_V0 request{};
    request.StartUsn = journalId == journal.UsnJournalID && fromUsn >= journal.FirstUsn ? fromUsn : journal.FirstUsn;
    request.ReasonMask = USN_REASON_FILE_CREATE | USN_REASON_FILE_DELETE | USN_REASON_RENAME_NEW_NAME | USN_REASON_DATA_OVERWRITE | USN_REASON_CLOSE;
    request.ReturnOnlyOnClose = TRUE;
    request.UsnJournalID = journal.UsnJournalID;

    // Same file on the same day counts once: installers touch a driver many times in a row.
    std::map<std::pair<std::string, std::int64_t>, Change> merged;
    std::vector<BYTE> buffer(64 * 1024);
    for (;;) {
        if (!DeviceIoControl(handle.get(), FSCTL_READ_USN_JOURNAL, &request, sizeof(request), buffer.data(), static_cast<DWORD>(buffer.size()), &returned, nullptr)) {
            break;
        }
        if (returned <= sizeof(USN)) break;

        DWORD offset = sizeof(USN);
        while (offset + sizeof(USN_RECORD_V2) <= returned) {
            const auto* record = reinterpret_cast<const USN_RECORD_V2*>(buffer.data() + offset);
            if (record->RecordLength < sizeof(USN_RECORD_V2) || offset + record->RecordLength > returned) break;
            offset += record->RecordLength;
            if (record->MajorVersion != 2) continue;
            ++result.recordsRead;

            const auto nameEnd = static_cast<std::size_t>(record->FileNameOffset) + record->FileNameLength;
            if (nameEnd > record->RecordLength) continue;
            const std::wstring_view name(reinterpret_cast<const wchar_t*>(reinterpret_cast<const BYTE*>(record) + record->FileNameOffset),
                                         record->FileNameLength / sizeof(wchar_t));
            const std::int64_t timeMs = fileTimeToUnixMs(static_cast<std::uint64_t>(record->TimeStamp.QuadPart));
            if (result.oldestMs == 0 || (timeMs != 0 && timeMs < result.oldestMs)) result.oldestMs = timeMs;

            const std::string file = toLower(toUtf8(name));
            if (!endsWithNoCase(file, ".sys") && !endsWithNoCase(file, ".inf")) continue;
            if (!(record->Reason & (USN_REASON_FILE_CREATE | USN_REASON_FILE_DELETE | USN_REASON_RENAME_NEW_NAME | USN_REASON_DATA_OVERWRITE))) continue;

            Change change;
            change.timeMs = timeMs;
            change.kind = "file_changed";
            change.name = file + " " + describeReason(record->Reason);
            change.module = file;
            change.source = "usn";
            merged.try_emplace({file, timeMs / kDayMs}, std::move(change));
        }
        request.StartUsn = *reinterpret_cast<const USN*>(buffer.data());
    }

    result.nextUsn = request.StartUsn;
    for (auto& [key, change] : merged) result.changes.push_back(std::move(change));
    return result;
}

}  // namespace why

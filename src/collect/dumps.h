// Reads Windows crash dumps without any network access or symbol downloads.
#pragma once

#include <optional>
#include <string>
#include <vector>

#include "core/types.h"

namespace why {

struct DumpListing {
    std::vector<std::wstring> paths;
    bool accessDenied = false;
};

DumpListing listDumps();

// Reads the header and asks the Windows debug engine which module the crash points at.
std::optional<DumpInfo> analyzeDump(const std::wstring& path);

struct ModuleHit {
    std::string module;
    std::string evidence;  // "parameter" or "stack".
};

// Opens any dump with the debug engine. Exposed separately so it can be tested with a dump made on demand.
std::optional<ModuleHit> findModuleInDump(const std::wstring& path, std::optional<std::uint64_t> address);

}  // namespace why

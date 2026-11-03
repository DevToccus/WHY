// Reads the NTFS change journal for driver files. Needs administrator rights.
// The journal records what changed and when, but not which process changed it.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "core/types.h"

namespace why {

struct UsnResult {
    std::vector<Change> changes;
    std::int64_t oldestMs = 0;
    std::int64_t recordsRead = 0;
    std::int64_t journalBytes = 0;
    std::uint64_t journalId = 0;
    std::int64_t nextUsn = 0;
    bool available = false;
    std::string error;
};

// Reads from fromUsn when journalId still matches; otherwise from the oldest record kept.
UsnResult readDriverFileChanges(std::uint64_t journalId, std::int64_t fromUsn);

}  // namespace why

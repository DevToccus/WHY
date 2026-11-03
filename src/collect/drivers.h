// Inventory of the kernel drivers that matter: those running or attached to a present device.
#pragma once

#include <vector>

#include "core/types.h"

namespace why {

std::vector<DriverInfo> readDrivers();

// Compares two inventories taken at different times.
std::vector<Change> diffDrivers(const std::vector<DriverInfo>& before, const std::vector<DriverInfo>& after, std::int64_t nowMs);

// Estimated install times from the driver store. Available on first run, with no earlier snapshot.
std::vector<Change> changesFromInventory(const std::vector<DriverInfo>& drivers, std::int64_t sinceMs);

}  // namespace why

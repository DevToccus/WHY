// Installs and runs the Live monitor as a Windows service.
#pragma once

#include <string>

namespace why {

// All three need administrator rights. Each returns an empty string on success, or what went wrong.
std::string installService();
std::string uninstallService();
// Hands the process to the Service Control Manager. Returns when the service has stopped.
std::string runService();

bool isServiceInstalled();

}  // namespace why

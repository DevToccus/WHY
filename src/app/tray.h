// Small per-user process that turns Live monitor alerts into Windows notifications.
#pragma once

#include <windows.h>

namespace why::app {

int runTray(HINSTANCE instance);

}  // namespace why::app

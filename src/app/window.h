// The desktop window: hosts the interface in WebView2 and connects it to a monitor or a local engine.
#pragma once

#include <windows.h>

#include <optional>
#include <string>

namespace why::app {

int runWindow(HINSTANCE instance, const std::optional<std::wstring>& reportPath);

}  // namespace why::app

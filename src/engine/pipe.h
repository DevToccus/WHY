// Local-only channel between the Live service and the unprivileged app.
#pragma once

#include <functional>
#include <optional>
#include <string>

namespace why {

class Engine;

// Answers state, wait and case requests. Runs until the process exits.
void startPipeServer(Engine& engine, const wchar_t* pipeName);

constexpr const wchar_t* kServicePipe = L"\\\\.\\pipe\\why-monitor";

// Sends one request and returns the reply. Empty when no monitor is listening.
// requireService refuses a server outside session 0, which an ordinary program could impersonate.
std::optional<std::string> pipeRequest(const std::string& request, const wchar_t* pipeName = kServicePipe, bool requireService = true);

}  // namespace why

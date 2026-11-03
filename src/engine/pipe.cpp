#include "engine/pipe.h"

#include <windows.h>
#include <sddl.h>
#include <wil/resource.h>

#include <nlohmann/json.hpp>
#include <thread>

#include "engine/engine.h"

using nlohmann::json;

namespace why {
namespace {

constexpr DWORD kMaxRequest = 64 * 1024;
constexpr DWORD kMaxReply = 256 * 1024 * 1024;
constexpr int kWaitMs = 55'000;

bool writeAll(HANDLE pipe, const void* data, DWORD size) {
    const auto* cursor = static_cast<const BYTE*>(data);
    while (size > 0) {
        DWORD written = 0;
        if (!WriteFile(pipe, cursor, size, &written, nullptr)) return false;
        cursor += written;
        size -= written;
    }
    return true;
}

bool readAll(HANDLE pipe, void* data, DWORD size) {
    auto* cursor = static_cast<BYTE*>(data);
    while (size > 0) {
        DWORD read = 0;
        if (!ReadFile(pipe, cursor, size, &read, nullptr) || read == 0) return false;
        cursor += read;
        size -= read;
    }
    return true;
}

bool writeFrame(HANDLE pipe, const std::string& body) {
    const auto size = static_cast<DWORD>(body.size());
    return writeAll(pipe, &size, sizeof(size)) && writeAll(pipe, body.data(), size);
}

std::optional<std::string> readFrame(HANDLE pipe, DWORD limit) {
    DWORD size = 0;
    if (!readAll(pipe, &size, sizeof(size)) || size > limit) return std::nullopt;
    std::string body(size, '\0');
    if (size && !readAll(pipe, body.data(), size)) return std::nullopt;
    return body;
}

std::string handleRequest(Engine& engine, const std::string& text) {
    const json request = json::parse(text, nullptr, false);
    if (!request.is_object()) return R"({"error":"bad request"})";
    const std::string command = request.value("cmd", "");
    if (command == "state") return *engine.state();
    if (command == "wait") {
        const std::uint64_t rev = engine.waitForChange(request.value("rev", std::uint64_t{0}), kWaitMs);
        return json{{"rev", rev}}.dump();
    }
    if (command == "case") {
        const bool ok = engine.applyCaseAction(request.value("key", ""), request.value("action", ""));
        return json{{"ok", ok}}.dump();
    }
    return R"({"error":"unknown command"})";
}

void serveClient(Engine& engine, wil::unique_hfile pipe) {
    while (const auto request = readFrame(pipe.get(), kMaxRequest)) {
        if (!writeFrame(pipe.get(), handleRequest(engine, *request))) break;
    }
    DisconnectNamedPipe(pipe.get());
}

}  // namespace

void startPipeServer(Engine& engine, const wchar_t* pipeName) {
    std::thread([&engine, name = std::wstring(pipeName)] {
        // SYSTEM and administrators have full access. Signed-in users can only talk to the monitor.
        wil::unique_hlocal_security_descriptor descriptor;
        ConvertStringSecurityDescriptorToSecurityDescriptorW(L"D:(A;;GA;;;SY)(A;;GA;;;BA)(A;;GRGW;;;IU)", SDDL_REVISION_1,
                                                             wil::out_param_ptr<PSECURITY_DESCRIPTOR*>(descriptor), nullptr);
        SECURITY_ATTRIBUTES attributes{sizeof(attributes), descriptor.get(), FALSE};
        for (;;) {
            wil::unique_hfile pipe(CreateNamedPipeW(name.c_str(), PIPE_ACCESS_DUPLEX,
                                                    PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT | PIPE_REJECT_REMOTE_CLIENTS,
                                                    8, 64 * 1024, 64 * 1024, 0, &attributes));
            if (!pipe) {
                Sleep(1000);
                continue;
            }
            if (!ConnectNamedPipe(pipe.get(), nullptr) && GetLastError() != ERROR_PIPE_CONNECTED) continue;
            std::thread(serveClient, std::ref(engine), std::move(pipe)).detach();
        }
    }).detach();
}

std::optional<std::string> pipeRequest(const std::string& request, const wchar_t* pipeName, bool requireService) {
    wil::unique_hfile pipe(CreateFileW(pipeName, GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING,
                                       SECURITY_SQOS_PRESENT | SECURITY_IDENTIFICATION, nullptr));
    if (!pipe) return std::nullopt;
    if (requireService) {
        ULONG session = 1;
        if (!GetNamedPipeServerSessionId(pipe.get(), &session) || session != 0) return std::nullopt;
    }
    if (!writeFrame(pipe.get(), request)) return std::nullopt;
    return readFrame(pipe.get(), kMaxReply);
}

}  // namespace why

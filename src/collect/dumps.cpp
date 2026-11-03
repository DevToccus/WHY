#include "collect/dumps.h"

#include <windows.h>
#include <dbgeng.h>
#include <wil/com.h>
#include <wil/resource.h>

#include <array>
#include <cstddef>
#include <vector>

#include "core/bugcheck.h"
#include "core/dump_header.h"
#include "core/text.h"
#include "engine/winutil.h"

namespace why {
namespace {

// The kernel itself appears in nearly every crash, so naming it would say nothing.
bool isCoreModule(const std::string& stem) {
    static constexpr std::array<std::string_view, 9> kCore{"nt",       "ntoskrnl", "ntkrnlmp", "ntkrnlpa", "ntkrpamp",
                                                           "hal",      "halmacpi", "kdcom",    "kd"};
    return std::find(kCore.begin(), kCore.end(), stem) != kCore.end();
}

using DebugCreateFn = HRESULT(STDAPICALLTYPE*)(REFIID, PVOID*);

// Always the copy in System32: a dbgeng.dll planted next to the executable must never load.
DebugCreateFn loadDebugCreate() {
    static const DebugCreateFn fn = []() -> DebugCreateFn {
        const HMODULE module = LoadLibraryExW(L"dbgeng.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
        if (!module) return nullptr;
        return reinterpret_cast<DebugCreateFn>(GetProcAddress(module, "DebugCreate"));
    }();
    return fn;
}

std::string moduleImageAt(IDebugSymbols* symbols, std::uint64_t address) {
    ULONG index = 0;
    ULONG64 base = 0;
    if (FAILED(symbols->GetModuleByOffset(address, 0, &index, &base))) return {};
    char image[MAX_PATH]{};
    ULONG imageSize = 0;
    if (FAILED(symbols->GetModuleNames(index, base, image, sizeof(image), &imageSize, nullptr, 0, nullptr, nullptr, 0, nullptr))) {
        return {};
    }
    const std::string_view text(image);
    const auto slash = text.find_last_of("\\/");
    return toLower(slash == std::string_view::npos ? text : text.substr(slash + 1));
}

// A pointer on the stack that lands inside a module shows that module was involved in the call path.
std::string scanStack(IUnknown* client, IDebugSymbols* symbols) {
    wil::com_ptr_nothrow<IDebugRegisters> registers;
    wil::com_ptr_nothrow<IDebugDataSpaces> memory;
    if (FAILED(client->QueryInterface(__uuidof(IDebugRegisters), registers.put_void())) ||
        FAILED(client->QueryInterface(__uuidof(IDebugDataSpaces), memory.put_void()))) {
        return {};
    }
    ULONG64 stack = 0;
    if (FAILED(registers->GetStackOffset(&stack))) return {};

    std::array<std::uint64_t, 2048> slots{};
    ULONG read = 0;
    if (FAILED(memory->ReadVirtual(stack, slots.data(), static_cast<ULONG>(sizeof(slots)), &read)) && read == 0) return {};
    const std::size_t count = read / sizeof(std::uint64_t);
    for (std::size_t i = 0; i < count; ++i) {
        if (slots[i] < 0x10000) continue;
        const std::string image = moduleImageAt(symbols, slots[i]);
        if (!image.empty() && !isCoreModule(moduleStem(image))) return image;
    }
    return {};
}

}  // namespace

DumpListing listDumps() {
    DumpListing listing;
    const std::wstring folder = windowsDirectory() + L"\\Minidump\\";
    WIN32_FIND_DATAW data{};
    wil::unique_hfind find(FindFirstFileW((folder + L"*.dmp").c_str(), &data));
    if (!find) {
        listing.accessDenied = GetLastError() == ERROR_ACCESS_DENIED;
        return listing;
    }
    do {
        listing.paths.push_back(folder + data.cFileName);
    } while (FindNextFileW(find.get(), &data));
    return listing;
}

std::optional<ModuleHit> findModuleInDump(const std::wstring& path, std::optional<std::uint64_t> address) {
    const auto debugCreate = loadDebugCreate();
    if (!debugCreate) return std::nullopt;

    wil::com_ptr_nothrow<IDebugClient4> client;
    if (FAILED(debugCreate(__uuidof(IDebugClient4), client.put_void()))) return std::nullopt;
    wil::com_ptr_nothrow<IDebugControl> control;
    wil::com_ptr_nothrow<IDebugSymbols> symbols;
    if (FAILED(client->QueryInterface(__uuidof(IDebugControl), control.put_void())) ||
        FAILED(client->QueryInterface(__uuidof(IDebugSymbols), symbols.put_void()))) {
        return std::nullopt;
    }

    // Empty paths keep the debug engine off the network, even if _NT_SYMBOL_PATH is set.
    symbols->SetSymbolPath("");
    symbols->SetImagePath("");

    if (FAILED(client->OpenDumpFileWide(path.c_str(), 0))) return std::nullopt;
    const auto closeDump = wil::scope_exit([&] { client->EndSession(DEBUG_END_ACTIVE_DETACH); });
    if (FAILED(control->WaitForEvent(DEBUG_WAIT_DEFAULT, INFINITE))) return std::nullopt;

    if (address) {
        std::string image = moduleImageAt(symbols.get(), *address);
        if (!image.empty() && !isCoreModule(moduleStem(image))) return ModuleHit{std::move(image), "parameter"};
    }
    std::string image = scanStack(client.get(), symbols.get());
    if (!image.empty()) return ModuleHit{std::move(image), "stack"};
    return std::nullopt;
}

std::optional<DumpInfo> analyzeDump(const std::wstring& path) {
    wil::unique_hfile file(CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                       nullptr, OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, nullptr));
    if (!file) return std::nullopt;

    std::vector<std::byte> bytes(kDumpHeaderSize);
    DWORD read = 0;
    if (!ReadFile(file.get(), bytes.data(), static_cast<DWORD>(bytes.size()), &read, nullptr)) return std::nullopt;
    const auto header = parseDumpHeader(std::span<const std::byte>(bytes.data(), read));
    if (!header) return std::nullopt;

    DumpInfo info;
    const auto slash = path.find_last_of(L'\\');
    info.fileName = toUtf8(slash == std::wstring::npos ? path : path.substr(slash + 1));
    info.timeMs = header->timeMs;
    info.bugCheck = header->bugCheck;
    std::copy(std::begin(header->params), std::end(header->params), std::begin(info.params));

    std::optional<std::uint64_t> address;
    if (const int param = bugCheckInfo(info.bugCheck).addressParam; param >= 0) address = info.params[param];
    if (const auto hit = findModuleInDump(path, address)) {
        info.module = hit->module;
        info.moduleEvidence = hit->evidence;
    }
    return info;
}

}  // namespace why

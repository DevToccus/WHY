#include "core/bugcheck.h"

#include <array>

#include "core/text.h"

namespace why {
namespace {

struct Entry {
    std::uint32_t code;
    const char* name;
    const char* plain;
    BugCheckHint hint;
    int addressParam;
};

constexpr std::array kEntries{
    Entry{0x0A, "IRQL_NOT_LESS_OR_EQUAL", "Kernel code touched memory it was not allowed to touch at that moment.", BugCheckHint::Driver, 3},
    Entry{0x19, "BAD_POOL_HEADER", "Kernel memory bookkeeping was found damaged.", BugCheckHint::Memory, -1},
    Entry{0x1A, "MEMORY_MANAGEMENT", "Windows found an inconsistency in how memory is managed.", BugCheckHint::Memory, -1},
    Entry{0x1E, "KMODE_EXCEPTION_NOT_HANDLED", "Kernel code hit an error that nothing handled.", BugCheckHint::Driver, 1},
    Entry{0x3B, "SYSTEM_SERVICE_EXCEPTION", "An error occurred while the kernel was serving a request.", BugCheckHint::Driver, 1},
    Entry{0x4E, "PFN_LIST_CORRUPT", "The list Windows uses to track physical memory was damaged.", BugCheckHint::Memory, -1},
    Entry{0x50, "PAGE_FAULT_IN_NONPAGED_AREA", "Kernel code read memory that does not exist.", BugCheckHint::Driver, 2},
    Entry{0x77, "KERNEL_STACK_INPAGE_ERROR", "Windows could not read kernel data back from disk.", BugCheckHint::Storage, -1},
    Entry{0x7A, "KERNEL_DATA_INPAGE_ERROR", "Windows could not read kernel data back from disk.", BugCheckHint::Storage, -1},
    Entry{0x7E, "SYSTEM_THREAD_EXCEPTION_NOT_HANDLED", "A kernel thread hit an error that nothing handled.", BugCheckHint::Driver, 1},
    Entry{0x9F, "DRIVER_POWER_STATE_FAILURE", "A driver did not complete a power transition, such as sleep, in time.", BugCheckHint::Driver, -1},
    Entry{0xBE, "ATTEMPTED_WRITE_TO_READONLY_MEMORY", "A driver tried to write to protected memory.", BugCheckHint::Driver, -1},
    Entry{0xC2, "BAD_POOL_CALLER", "Kernel code misused the memory allocator.", BugCheckHint::Driver, -1},
    Entry{0xC4, "DRIVER_VERIFIER_DETECTED_VIOLATION", "Driver Verifier caught a driver breaking the rules.", BugCheckHint::Driver, -1},
    Entry{0xC5, "DRIVER_CORRUPTED_EXPOOL", "A driver damaged kernel memory.", BugCheckHint::Driver, -1},
    Entry{0xD1, "DRIVER_IRQL_NOT_LESS_OR_EQUAL", "A driver touched memory it was not allowed to touch at that moment.", BugCheckHint::Driver, 3},
    Entry{0xEF, "CRITICAL_PROCESS_DIED", "A process Windows cannot run without stopped unexpectedly.", BugCheckHint::Unknown, -1},
    Entry{0xFC, "ATTEMPTED_EXECUTE_OF_NOEXECUTE_MEMORY", "Kernel code tried to run data as if it were code.", BugCheckHint::Driver, -1},
    Entry{0x101, "CLOCK_WATCHDOG_TIMEOUT", "A processor core stopped responding.", BugCheckHint::Hardware, -1},
    Entry{0x10D, "WDF_VIOLATION", "A driver built on the Windows Driver Framework broke its rules.", BugCheckHint::Driver, -1},
    Entry{0x116, "VIDEO_TDR_FAILURE", "The graphics driver stopped responding and could not be recovered.", BugCheckHint::Graphics, 1},
    Entry{0x117, "VIDEO_TDR_TIMEOUT_DETECTED", "The graphics driver stopped responding.", BugCheckHint::Graphics, 1},
    Entry{0x119, "VIDEO_SCHEDULER_INTERNAL_ERROR", "The graphics scheduler found a serious inconsistency.", BugCheckHint::Graphics, -1},
    Entry{0x124, "WHEA_UNCORRECTABLE_ERROR", "The hardware reported an error it could not correct.", BugCheckHint::Hardware, -1},
    Entry{0x133, "DPC_WATCHDOG_VIOLATION", "A driver kept the processor busy for too long.", BugCheckHint::Driver, -1},
    Entry{0x139, "KERNEL_SECURITY_CHECK_FAILURE", "The kernel detected damaged internal data.", BugCheckHint::Driver, -1},
    Entry{0x154, "UNEXPECTED_STORE_EXCEPTION", "The kernel memory store hit an error, often related to the disk.", BugCheckHint::Storage, -1},
};

}  // namespace

BugCheckInfo bugCheckInfo(std::uint32_t code) {
    for (const auto& entry : kEntries) {
        if (entry.code == code) return {entry.name, entry.plain, entry.hint, entry.addressParam};
    }
    return {hexCode(code), "Windows stopped because of a serious error.", BugCheckHint::Unknown, -1};
}

}  // namespace why

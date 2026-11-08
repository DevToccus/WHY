#include "core/investigator.h"

#include <algorithm>
#include <charconv>
#include <cstdio>
#include <cstdlib>
#include <set>

#include "core/bugcheck.h"
#include "core/signals.h"
#include "core/text.h"

namespace why {
namespace {

constexpr std::int64_t kMinute = 60'000;
constexpr std::int64_t kDay = 24 * 60 * kMinute;
constexpr std::int64_t kCrashWindow = 15 * kMinute;  // A crash leaves several records around one reboot.
constexpr std::int64_t kBurstWindow = 10 * kMinute;  // Repeats this close are one incident.
constexpr std::int64_t kChangeLookback = 14 * kDay;
constexpr std::int64_t kRecentWindow = 7 * kDay;
constexpr std::int64_t kLongRunningAfter = 30 * kDay;
constexpr std::int64_t kWatchUntilResolved = 14 * kDay;
constexpr std::int64_t kQuietAfter = 30 * kDay;
constexpr std::size_t kMaxChain = 50;
constexpr std::size_t kMaxOtherChanges = 5;
constexpr int kHabitualInstalls = 3;
constexpr int kBulkRefreshDrivers = 10;
constexpr int kRestartsWorthAlert = 2;  // A PC going off by itself is unusual enough that twice is a pattern.

struct Incident {
    std::string key;
    std::string signal;
    std::string subject;
    std::vector<Event> events;
    const DumpInfo* dump = nullptr;
    std::uint32_t bugCheck = 0;
    std::int64_t startMs() const { return events.front().timeMs; }
};

const std::string& dataOf(const Event& event, const char* key) {
    static const std::string kEmpty;
    const auto it = event.data.find(key);
    return it == event.data.end() ? kEmpty : it->second;
}

std::uint32_t bugCheckOfEvent(const Event& event) {
    if (event.provider == "Microsoft-Windows-Kernel-Power") {
        const auto& text = dataOf(event, "BugcheckCode");
        std::uint32_t code = 0;
        std::from_chars(text.data(), text.data() + text.size(), code);
        return code;
    }
    if (event.signal == "bsod") return bugCheckFromReport(dataOf(event, "param1"));
    return 0;
}

Event eventFromDump(const DumpInfo& dump) {
    Event event;
    event.channel = "Crash dump";
    event.timeMs = dump.timeMs;
    event.provider = "Crash dump";
    event.signal = "bsod";
    event.subject = dump.module;
    event.message = "Crash dump " + dump.fileName + " records stop code " + hexCode(dump.bugCheck) + ".";
    return event;
}

// One blue screen leaves a dump, a WER report and a Kernel-Power record minutes apart. They are one incident.
std::vector<Incident> buildCrashIncidents(const std::vector<Event>& problems, const std::vector<DumpInfo>& dumps) {
    struct Item {
        Event event;
        const DumpInfo* dump;
    };
    std::vector<Item> items;
    for (const auto& event : problems) {
        if (event.signal == "bsod" || event.signal == "unexpected_restart") items.push_back({event, nullptr});
    }
    for (const auto& dump : dumps) items.push_back({eventFromDump(dump), &dump});
    std::stable_sort(items.begin(), items.end(),
                     [](const Item& a, const Item& b) { return a.event.timeMs < b.event.timeMs; });

    std::vector<Incident> incidents;
    for (const auto& item : items) {
        if (incidents.empty() || item.event.timeMs - incidents.back().events.back().timeMs > kCrashWindow) {
            incidents.emplace_back();
        }
        auto& incident = incidents.back();
        incident.events.push_back(item.event);
        if (item.dump) incident.dump = item.dump;
        if (!incident.bugCheck) incident.bugCheck = bugCheckOfEvent(item.event);
    }

    for (auto& incident : incidents) {
        if (incident.dump) incident.bugCheck = incident.dump->bugCheck;
        if (incident.bugCheck) {
            incident.signal = "bsod";
            incident.subject = incident.dump ? moduleStem(incident.dump->module) : "";
            incident.key = "bsod:" + hexCode(incident.bugCheck) + ":" + incident.subject;
        } else {
            incident.signal = "unexpected_restart";
            incident.key = "unexpected_restart";
        }
    }
    return incidents;
}

std::vector<Incident> buildIncidents(const std::vector<Event>& events, const std::vector<DumpInfo>& dumps) {
    std::vector<Event> problems;
    for (const auto& event : events) {
        if (event.role == Role::Problem) problems.push_back(event);
    }
    std::stable_sort(problems.begin(), problems.end(),
                     [](const Event& a, const Event& b) { return a.timeMs < b.timeMs; });

    auto incidents = buildCrashIncidents(problems, dumps);

    std::map<std::string, std::size_t> lastOfKey;
    for (const auto& event : problems) {
        if (event.signal == "bsod" || event.signal == "unexpected_restart") continue;
        const std::string key = event.signal + ":" + toLower(event.subject);
        const auto it = lastOfKey.find(key);
        if (it != lastOfKey.end() && event.timeMs - incidents[it->second].events.back().timeMs <= kBurstWindow) {
            incidents[it->second].events.push_back(event);
            continue;
        }
        Incident incident;
        incident.key = key;
        incident.signal = event.signal;
        incident.subject = event.subject;
        incident.events.push_back(event);
        incidents.push_back(std::move(incident));
        lastOfKey[key] = incidents.size() - 1;
    }
    return incidents;
}

const DriverInfo* findDriver(const std::vector<DriverInfo>& drivers, const std::string& stem) {
    if (stem.empty()) return nullptr;
    for (const auto& driver : drivers) {
        if (moduleStem(driver.module) == stem || toLower(driver.service) == stem) return &driver;
    }
    return nullptr;
}

std::string pluralize(int count, const char* one, const char* many) {
    return std::to_string(count) + " " + (count == 1 ? one : many);
}

std::string daysBetween(std::int64_t earlier, std::int64_t later) {
    const auto days = (later - earlier) / kDay;
    if (days <= 0) return "the same day as";
    return pluralize(static_cast<int>(days), "day", "days") + " before";
}

// ---- Case text. Everything stated here must be backed by what the logs actually hold. ----

void describeBsod(Case& c, const Incident& latest, const std::vector<DriverInfo>& drivers) {
    const auto info = bugCheckInfo(latest.bugCheck);
    c.title = "Blue screen: " + info.name;
    c.summary = info.plain;
    c.findings.push_back({Certainty::Verified, "Windows stopped with " + info.name + " (" + hexCode(latest.bugCheck) +
                                                   ") " + pluralize(c.incidents, "time", "times") + "."});
    if (latest.dump && !latest.dump->module.empty()) {
        const bool direct = latest.dump->moduleEvidence == "parameter";
        c.findings.push_back({direct ? Certainty::Probable : Certainty::Correlated,
                              direct ? "The stop code points directly at " + latest.dump->module +
                                           ". The module named is often the cause, but it can also be the victim."
                                     : latest.dump->module +
                                           " was found on the kernel stack at the moment of the crash. "
                                           "This shows it was active, not that it was responsible."});
        c.causeSupported = direct;
        if (const auto* driver = findDriver(drivers, moduleStem(latest.dump->module))) {
            c.title = "Blue screen involving " + (driver->displayName.empty() ? driver->module : driver->displayName);
        }
    } else if (!latest.dump) {
        c.findings.push_back({Certainty::Correlated,
                              "No crash dump was available to name the responsible module. "
                              "Run WHY? as administrator to read dumps, if Windows kept any."});
    }
    // 0xE2 and 0xDEADDEAD are crashes started on purpose, from the keyboard or a debugger.
    if (latest.bugCheck == 0xE2 || latest.bugCheck == 0xDEADDEAD) {
        c.origin = "forced";
        c.originText = "This blue screen was started on purpose, from the keyboard shortcut or a debugger. It is not a fault.";
        c.advice.push_back({"Nothing to fix if you started it yourself. If you did not, check for keyboard crash settings (CrashOnCtrlScroll) set by a tool.", ""});
        return;
    }
    c.origin = "error";
    c.originText = "Windows stopped itself to avoid damage. A blue screen is never caused by closing a program or the PC: it is a real fault in a driver, Windows or the hardware.";
    switch (info.hint) {
        case BugCheckHint::Hardware:
            c.advice.push_back({"Reset the BIOS to its defaults, which turns off overclocking and XMP memory profiles, then see if it happens again.", ""});
            c.advice.push_back({"Clean dust from fans and heatsinks and check temperatures under load.", ""});
            c.advice.push_back({"Update the BIOS and chipset drivers from the motherboard maker's website.", ""});
            break;
        case BugCheckHint::Memory:
            c.advice.push_back({"Test the RAM with Windows Memory Diagnostic. Any error means a module must be replaced.", "memory_diagnostic"});
            c.advice.push_back({"Turn off the XMP or EXPO memory profile in the BIOS: unstable memory speeds cause exactly this.", ""});
            break;
        case BugCheckHint::Storage:
            c.advice.push_back({"Back up your data now.", ""});
            c.advice.push_back({"Run \"chkdsk C: /scan\" in an administrator terminal, and read the disk's health with its maker's tool.", ""});
            c.advice.push_back({"Reseat or replace the disk's cable, or try another port.", ""});
            break;
        case BugCheckHint::Graphics:
            c.advice.push_back({"Install the latest graphics driver from NVIDIA, AMD or Intel with the Clean installation option.", ""});
            c.advice.push_back({"If it started after a driver update, roll it back: Device Manager, the graphics card, Properties, Driver, Roll Back Driver.", "device_manager"});
            break;
        default:
            c.advice.push_back({"Update the driver named above from its maker's website, not only Windows Update.", ""});
            c.advice.push_back({"If it started after an update, roll it back: Device Manager, the device, Properties, Driver, Roll Back Driver.", "device_manager"});
            break;
    }
    c.advice.push_back({"Repair Windows files: run \"sfc /scannow\", then \"DISM /Online /Cleanup-Image /RestoreHealth\" in an administrator terminal.", ""});
}

void describeRestart(Case& c, const std::vector<Incident>& incidents) {
    c.title = "Windows restarted without shutting down";
    c.summary = "The PC went off or restarted without a clean shutdown, and no blue screen was recorded.";
    c.findings.push_back({Certainty::Verified, "Windows recorded " + pluralize(c.incidents, "restart", "restarts") +
                                                   " without a clean shutdown and without a stop code."});
    bool powerButton = false;
    bool duringSleep = false;
    for (const auto& incident : incidents) {
        for (const auto& event : incident.events) {
            const auto& pressed = dataOf(event, "PowerButtonTimestamp");
            if ((!pressed.empty() && pressed != "0") || dataOf(event, "LongPowerButtonPressDetected") == "true") {
                powerButton = true;
            }
            const auto& sleeping = dataOf(event, "SleepInProgress");
            if (!sleeping.empty() && sleeping != "0") duringSleep = true;
        }
    }
    if (powerButton) {
        c.findings.push_back({Certainty::Probable, "The power button was held down, so someone may have forced it off."});
        c.causeSupported = true;
    }
    if (duringSleep) {
        c.findings.push_back({Certainty::Verified, "At least one restart happened while the PC was entering or leaving sleep."});
    }
    c.findings.push_back({Certainty::Correlated,
                          "Common reasons are a power cut, an unstable power supply, overheating or a complete freeze. "
                          "The logs cannot tell these apart."});
    if (powerButton) {
        c.origin = "forced";
        c.originText = "The power button was held down, so the PC was switched off by hand. If it had frozen first, the freeze is the real problem; "
                       "otherwise there is nothing to fix.";
        c.advice.push_back({"If you switched it off because it froze, look at the other cases from that moment: they show what froze it.", ""});
    } else {
        c.origin = "unclear";
        c.originText = "Nobody pressed the power button, so this was not a normal switch-off. A power cut, a failing power supply, overheating "
                       "or a complete freeze all look the same in the logs.";
        c.advice.push_back({"If it happens under load, such as games or builds, check temperatures and the power supply first: they are the usual cause.", ""});
        c.advice.push_back({"Update the BIOS and chipset drivers from the motherboard maker's website.", ""});
        c.advice.push_back({"Turn off Fast Startup (Control Panel, Power Options, Choose what the power buttons do) if it happens while starting or waking.", ""});
    }
    c.advice.push_back({"See what else failed at the same times in Reliability Monitor.", "reliability_monitor"});
}

void describeGpu(Case& c) {
    const std::string driver = c.subject.empty() ? "graphics driver" : c.subject;
    c.title = "Graphics driver stopped responding: " + driver;
    c.summary = "Windows or the graphics driver reported that the GPU stopped responding.";
    c.findings.push_back({Certainty::Verified, "The graphics driver " + driver + " reported a problem " +
                                                   pluralize(c.incidents, "time", "times") + "."});
    c.origin = "error";
    c.originText = "The graphics card stopped answering and Windows reset its driver. Closing a program does not cause this: it is a real driver or hardware fault.";
    c.advice.push_back({"Install the latest graphics driver from NVIDIA, AMD or Intel with the Clean installation option.", ""});
    c.advice.push_back({"If it started after a driver update, roll it back: Device Manager, the graphics card, Properties, Driver, Roll Back Driver.", "device_manager"});
    c.advice.push_back({"Remove any GPU overclock or undervolt, and check that the card's power cables are firmly connected.", ""});
}

void describeWhea(Case& c, bool fatal) {
    c.title = fatal ? "Hardware reported a fatal error" : "Hardware reported errors it corrected";
    c.summary = fatal ? "A component reported an error it could not recover from."
                      : "A component had errors. It corrected them, but repeats can announce a failure.";
    c.findings.push_back({Certainty::Verified, "Windows Hardware Error Architecture logged this " +
                                                   pluralize(c.incidents, "time", "times") + "."});
    c.origin = "error";
    c.originText = "The hardware itself reported this error. It cannot come from closing a program.";
    c.advice.push_back({"Reset the BIOS to its defaults, which turns off overclocking and XMP memory profiles.", ""});
    c.advice.push_back({"Update the BIOS from the motherboard maker's website: many of these errors are fixed by microcode updates.", ""});
    c.advice.push_back({"Test the RAM with Windows Memory Diagnostic.", "memory_diagnostic"});
}

void describeDisk(Case& c, const std::string& signal) {
    const std::string device = c.subject.empty() ? "A disk" : c.subject;
    if (signal == "disk_failure_predicted") {
        c.title = "Disk predicts its own failure";
        c.summary = "The disk reports that it expects to fail soon.";
    } else if (signal == "filesystem_corruption") {
        c.title = "File system corruption detected";
        c.summary = "Windows found damaged file system structures on a volume.";
    } else if (signal == "disk_error") {
        c.title = "Disk read or write errors";
        c.summary = "Windows could not complete some operations on a disk.";
    } else if (signal == "disk_retry") {
        c.title = "Disk operations had to be retried";
        c.summary = "Windows had to repeat some disk operations.";
    } else {
        c.title = "Storage controller reset a device";
        c.summary = "A disk stopped responding for long enough that Windows reset it.";
    }
    c.findings.push_back({Certainty::Verified, device + ": recorded " + pluralize(c.incidents, "time", "times") + "."});
    c.origin = "error";
    c.originText = "The disk or Windows' storage stack reported it. It is a real fault, not caused by closing programs, although switching the PC off by force can damage the file system.";
    c.advice.push_back({"Back up important data now.", ""});
    if (signal == "filesystem_corruption") {
        c.advice.push_back({"Repair the volume: run \"chkdsk C: /f\" in an administrator terminal (use the letter of the volume) and restart when asked.", ""});
    } else if (signal == "disk_failure_predicted") {
        c.advice.push_back({"Replace the disk. When a disk predicts its own failure, the prediction is usually right.", ""});
    } else {
        c.advice.push_back({"Reseat or replace the disk's cable, or try another port: this fixes many retries and resets.", ""});
        c.advice.push_back({"Read the disk's health with its maker's tool or CrystalDiskInfo. Replace it if it shows Caution or Bad.", ""});
    }
}

void describeDriverLoad(Case& c) {
    const std::string driver = c.subject.empty() ? "A driver" : c.subject;
    c.title = "Driver failed to load: " + driver;
    c.summary = "Windows tried to load a driver during startup and could not.";
    c.findings.push_back({Certainty::Verified, driver + " failed to load " + pluralize(c.incidents, "time", "times") + "."});
    c.origin = "error";
    c.originText = "Windows could not load this driver at startup. It is a real error, though often harmless when the driver belongs to a device or program that is gone.";
    c.advice.push_back({"If a device is not working, open Device Manager, uninstall that device with its driver, then install the driver from the maker's website.", "device_manager"});
    c.advice.push_back({"If every device works, the driver is probably left over from uninstalled software: reinstall or fully remove that software.", ""});
}

void describeService(Case& c) {
    const std::string service = c.subject.empty() ? "A service" : c.subject;
    c.title = "Service stopped unexpectedly: " + service;
    c.summary = "A Windows service ended without being asked to.";
    c.findings.push_back({Certainty::Verified, service + " stopped unexpectedly " + pluralize(c.incidents, "time", "times") + "."});
    c.origin = "error";
    c.originText = "Windows records this only when a service ends without being asked to. Stopping it yourself from Services is not logged this way.";
    c.advice.push_back({"Update or reinstall the program the service belongs to.", ""});
    c.advice.push_back({"In Services, open it and on the Recovery tab set it to restart on failure, so it recovers by itself.", "services"});
    c.advice.push_back({"If it is a Windows service, run \"sfc /scannow\" in an administrator terminal to repair system files.", ""});
}

// The file name of a program, from the path the crash record gives.
std::string programName(const std::string& subject) {
    const auto slash = subject.find_last_of("\\/");
    return slash == std::string::npos ? subject : subject.substr(slash + 1);
}

void describeApp(Case& c, const Incident& latest) {
    const std::string program = c.subject.empty() ? "A program" : programName(c.subject);
    const bool hang = c.signal == "app_hang";
    c.title = (hang ? "App stopped responding: " : "App crashed: ") + program;
    c.summary = hang ? "The program stopped responding and was closed." : "The program closed because of an error it could not handle.";
    c.findings.push_back({Certainty::Verified, program + (hang ? " stopped responding " : " crashed ") +
                                                   pluralize(c.incidents, "time", "times") + "."});
    std::string module, code;
    if (!hang && !latest.events.empty()) {
        const auto& last = latest.events.back();
        module = dataOf(last, "ModuleName");
        code = toLower(dataOf(last, "ExceptionCode"));
        std::string detail;
        if (code == "e0434352") detail = "an unhandled .NET exception";
        else if (code == "c0000005") detail = "an invalid memory access";
        else if (code == "c0000409") detail = "a stack buffer overrun detected by the program itself";
        else if (code == "c00000fd") detail = "a stack overflow";
        else if (!code.empty()) detail = "exception code 0x" + code;
        if (!detail.empty()) {
            c.findings.push_back({Certainty::Verified, "The latest crash was " + detail +
                                                           (module.empty() ? "." : ", reported in " + module + ".")});
        }
    }
    if (c.subject.find('\\') != std::string::npos) {
        c.findings.push_back({Certainty::Verified, "Program path: " + c.subject});
        c.image = c.subject;
    }
    if (hang) {
        c.origin = "error";
        c.originText = "The program really froze. It was then closed by force, by you or by Windows at shutdown: the closing is not the problem, the freeze is.";
        c.advice.push_back({"Update the program. Freezes often come from waiting on a slow disk, a network folder or an add-on.", ""});
        c.advice.push_back({"If it freezes while opening or saving files in a synced or network folder, try a local folder.", ""});
    } else {
        c.origin = "error";
        c.originText = "Windows records a crash only when the program fails on its own. Closing it, or ending it from Task Manager, is not logged as a crash.";
        const std::string lowerModule = toLower(module);
        const std::string lowerProgram = toLower(program);
        if (code == "e0434352") {
            c.advice.push_back({"Repair or install the .NET runtime the program needs, from Microsoft's .NET download page.", ""});
        }
        if (lowerModule.starts_with("msvcp") || lowerModule.starts_with("vcruntime") || lowerModule.starts_with("ucrtbase")) {
            c.advice.push_back({"Repair the Microsoft Visual C++ Redistributable (Settings, Apps, Installed apps, Modify, Repair), or install the latest one.", ""});
        } else if (!lowerModule.empty() && lowerModule != lowerProgram && !lowerModule.starts_with("ntdll") && !lowerModule.starts_with("kernelbase")) {
            c.advice.push_back({"The crash happened inside " + module + ", not in the program's own code. If it belongs to an overlay, antivirus or plug-in, update or disable it.", ""});
        }
        c.advice.push_back({"Update or reinstall the program. If you develop it, its own logs or a debugger will show the failing code.", ""});
    }
    c.advice.push_back({"See every crash of this program in Reliability Monitor.", "reliability_monitor"});
}

// Defender writes paths as "file:_C:\\...; process:_pid:..." and lists several at once.
std::vector<std::string> threatFiles(const std::string& paths) {
    std::vector<std::string> files;
    std::size_t start = 0;
    while (start < paths.size()) {
        const auto end = std::min(paths.find(';', start), paths.size());
        std::string part = trim(paths.substr(start, end - start));
        if (part.starts_with("file:_")) files.push_back(part.substr(6));
        start = end + 1;
    }
    return files;
}

void describeDefender(Case& c, const Incident& latest) {
    if (c.signal == "av_disabled") {
        c.title = "Microsoft Defender real-time protection was turned off";
        c.summary = "Files were not scanned as they were opened while protection was off.";
        c.findings.push_back({Certainty::Verified, "Real-time protection was turned off " + pluralize(c.incidents, "time", "times") + "."});
        c.origin = "unclear";
        c.originText = "Protection can be turned off by you, by another antivirus or by malware. If you did it yourself, there is nothing to fix.";
        c.advice.push_back({"If you did not turn it off, turn it back on in Windows Security and run a full scan.", "windows_security"});
        return;
    }

    const std::string threat = c.subject.empty() ? "a threat" : c.subject;
    const bool failed = c.signal == "av_action_failed";
    c.title = (failed ? "Microsoft Defender could not remove " : "Microsoft Defender detected ") + threat;
    c.summary = failed ? "Defender found a threat and tried to act on it, but the action did not complete."
                       : "Defender found something it considers harmful and acted on it.";
    c.causeSupported = true;
    c.findings.push_back({Certainty::Verified, "Defender reported " + threat + " " + pluralize(c.incidents, "time", "times") + "."});

    std::string severity, category, action, process;
    std::vector<std::string> files;
    for (const auto& event : latest.events) {
        if (const auto& value = dataOf(event, "Severity Name"); !value.empty()) severity = value;
        if (const auto& value = dataOf(event, "Category Name"); !value.empty()) category = value;
        if (const auto& value = dataOf(event, "Action Name"); !value.empty()) action = value;
        if (const auto& value = dataOf(event, "Process Name"); !value.empty() && value != "Unknown" && process.empty()) process = value;
        for (auto& file : threatFiles(dataOf(event, "Path"))) {
            if (std::find(files.begin(), files.end(), file) == files.end()) files.push_back(std::move(file));
        }
    }
    if (!severity.empty() || !category.empty()) {
        c.findings.push_back({Certainty::Verified, "Defender rates it: " + severity + (category.empty() ? "" : ", " + category) + "."});
    }
    for (const auto& file : files) {
        c.findings.push_back({Certainty::Verified, "File involved: " + file});
        if (c.image.empty() && endsWithNoCase(file, ".exe")) c.image = file;
    }
    if (!process.empty()) c.findings.push_back({Certainty::Verified, "Noticed while this program was running: " + process});
    if (!action.empty()) c.findings.push_back({Certainty::Verified, "What Defender did: " + action + "."});
    if (c.subject.starts_with("Behavior:") || c.subject.find("!ml") != std::string::npos) {
        c.findings.push_back({Certainty::Correlated,
                              "This name comes from behaviour analysis, not from a known sample. Programs you build yourself, "
                              "which nobody has signed yet, can trigger it when they start with Windows or ask for administrator rights."});
    }
    c.advice.push_back({"Open Windows Security to see the details, restore the file if you trust it, or remove it.", "windows_security"});
}

// A dev case's subject is "label@folder".
std::pair<std::string, std::string> devSubject(const std::string& subject) {
    const auto at = subject.find('@');
    if (at == std::string::npos) return {subject, ""};
    return {subject.substr(0, at), subject.substr(at + 1)};
}

std::string exitCodeMeaning(std::uint32_t code) {
    switch (code) {
        case 1: return "a general error, which is how Node, Python and most tools report an uncaught exception";
        case 3: return "an abort";
        case 7: return "an error inside Node's own exception handler";
        case 9: return "an invalid argument to Node";
        case 13: return "an unfinished top-level await in Node";
        case 134: return "an abort, often JavaScript running out of memory";
        case 0xC0000005: return "an invalid memory access in native code";
        case 0xC0000409: return "a fail-fast stop, which Node and V8 use for fatal errors such as running out of memory";
        case 0xC00000FD: return "a stack overflow";
        case 0xE0434352: return "an unhandled .NET exception";
        case 0xC0000142: return "a failure to start, often a missing or broken DLL";
        case 0xFFFFFFFF: return "the process was ended from outside, for example by End task";
        default: return "";
    }
}

std::string describeDuration(std::int64_t ms) {
    const auto minutes = ms / kMinute;
    if (minutes < 1) return std::to_string(ms / 1000) + " seconds";
    if (minutes < 120) return pluralize(static_cast<int>(minutes), "minute", "minutes");
    return pluralize(static_cast<int>(minutes / 60), "hour", "hours");
}

void describeDev(Case& c, const Incident& latest) {
    const auto [label, folder] = devSubject(c.subject);
    const bool server = c.signal == "dev_crash";
    const std::string folderName = folder.substr(folder.find_last_of("\\/") + 1);
    c.title = (server ? "Dev server stopped with an error: " : "Command failed: ") + label + (folderName.empty() ? "" : " (" + folderName + ")");

    const Event& last = latest.events.back();
    const auto& rawCode = dataOf(last, "ExitCode");
    const auto code = static_cast<std::uint32_t>(std::strtoull(rawCode.c_str(), nullptr, 10));
    char hex[16]{};
    std::snprintf(hex, sizeof(hex), "0x%08X", code);
    const std::string codeText = code > 255 ? std::string(hex) : std::to_string(code);
    // A command that returns 1 to 255 is reporting its own failure, as a failed build or test run does: the person already
    // saw it in the terminal. A Windows status code such as 0xC0000005 means the process itself crashed.
    const bool crashed = !server && code > 255 && code != 0xFFFFFFFF;
    if (crashed) {
        c.severity = Severity::Warning;
        c.title = "Command crashed: " + label + (folderName.empty() ? "" : " (" + folderName + ")");
    }
    c.image = dataOf(last, "Image");
    const auto& runtime = dataOf(last, "RuntimeMs");
    const std::int64_t ranFor = std::strtoll(runtime.c_str(), nullptr, 10);

    c.summary = server ? "It had been running for " + describeDuration(ranFor) + " and exited with code " + codeText + "."
                       : "It exited with code " + codeText + " after " + describeDuration(ranFor) + ".";
    c.findings.push_back({Certainty::Verified, label + (server ? " stopped with an error " : " failed ") + pluralize(c.incidents, "time", "times") + "."});
    const std::string meaning = exitCodeMeaning(code);
    c.findings.push_back({Certainty::Verified, "Exit code " + codeText + (meaning.empty() ? "." : ": " + meaning + ".")});
    if (!server && !crashed) {
        c.findings.push_back({Certainty::Verified, "The command ended on its own with an error code, the way a failed build or test does. "
                                                   "It is listed here but never asks for your attention."});
    }
    if (const auto& command = dataOf(last, "CommandLine"); !command.empty()) c.findings.push_back({Certainty::Verified, "Command: " + command});
    if (!folder.empty()) c.findings.push_back({Certainty::Verified, "Folder: " + folder});

    // why-run keeps the end of what the command printed. That text is usually the actual error and stack.
    for (auto it = latest.events.rbegin(); it != latest.events.rend(); ++it) {
        if (const auto& output = dataOf(*it, "Output"); !output.empty()) {
            c.output = output;
            break;
        }
    }
    if (!c.output.empty()) {
        c.causeSupported = true;
        c.findings.push_back({Certainty::Verified, "The last lines it printed are kept in the terminal output. The error and its stack are usually at the end."});
        c.advice.push_back({"Read the terminal output from the bottom up: the first line of the stack that points into your own code is where to start.", ""});
    } else {
        c.findings.push_back({Certainty::Correlated, "WHY? saw it exit but cannot read a terminal it did not start, so the error text is not here."});
        c.advice.push_back({"Start it through why-run, for example \"why-run npm run dev\", and WHY? keeps the error it prints next time.", ""});
    }
    if (code == 134 || code == 0xC0000409) {
        c.advice.push_back({"If the output mentions heap out of memory, raise Node's limit with NODE_OPTIONS=--max-old-space-size=4096 or look for a leak.", ""});
    }
    if (code == 0xC0000005) {
        c.advice.push_back({"A native module crashed. Delete node_modules and reinstall, or run \"npm rebuild\", so it matches your Node version.", ""});
    }
    if (code == 0xC0000142) {
        c.advice.push_back({"Reinstall the runtime (Node, Python, .NET) the command uses: a DLL it needs is missing or broken.", ""});
    }

    // Ctrl+C never reaches this point. What is left is either a real failure or the process being killed from outside.
    if (code == 0xFFFFFFFF) {
        c.origin = "forced";
        c.originText = "It was ended from outside, by End task, taskkill or another tool. It did not fail by itself.";
    } else if (code > 255) {
        c.origin = "error";
        c.originText = "Exit code " + codeText + " is a Windows crash code: the process itself crashed. Closing it never produces this code.";
    } else if (!c.output.empty()) {
        c.origin = "error";
        c.originText = "It printed an error before exiting with code " + codeText + ", so it failed by itself. The output above shows why.";
    } else if (code == 1) {
        c.origin = "unclear";
        c.originText = "Exit code 1 means a failure, but \"taskkill /F\" and some tools also end processes with code 1. Without its output WHY? cannot tell which.";
    } else {
        c.origin = "error";
        c.originText = "It chose exit code " + codeText + " itself, which is how programs report a failure. Closing it with Ctrl+C is not counted.";
    }
}

// ---- What changed before the case began ----

bool sameModule(const Change& change, const std::string& stem) {
    if (stem.empty()) return false;
    return moduleStem(change.module) == stem || toLower(change.name) == stem;
}

std::string describeChange(const Change& change) {
    std::string text = change.name;
    if (!change.oldVersion.empty() && !change.newVersion.empty()) {
        text += " (" + change.oldVersion + " to " + change.newVersion + ")";
    } else if (!change.newVersion.empty()) {
        text += " (" + change.newVersion + ")";
    }
    return text;
}

// A driver change is a plausible lead for these. A service crashing is not explained by an unrelated driver.
bool unrelatedChangesMatter(std::string_view signal) {
    return signal != "service_crash" && signal != "driver_load_failed";
}

void linkChanges(Case& c, const std::string& stem, const std::vector<Change>& changes, std::int64_t historyStartMs) {
    std::vector<Change> sorted = changes;
    std::sort(sorted.begin(), sorted.end(), [](const Change& a, const Change& b) { return a.timeMs > b.timeMs; });

    // A change near the very start of the logs says little: earlier incidents may simply have aged out.
    const bool historyReachesBack = c.firstMs - historyStartMs >= kChangeLookback;

    // Some programs re-register their own driver every time they start. That is a habit, not a change.
    std::map<std::string, int> installsByName;
    // Many driver folders dated the same day mean Windows refreshed them all at once, not that each one changed.
    std::map<std::int64_t, int> storeDatesByDay;
    for (const auto& change : changes) {
        if (change.source == "service_install") ++installsByName[change.name];
        if (change.source == "driver_store") ++storeDatesByDay[change.timeMs / kDay];
    }
    const auto isBackground = [&](const Change& change) {
        if (change.source == "service_install" && installsByName[change.name] >= kHabitualInstalls) return true;
        return change.source == "driver_store" && storeDatesByDay[change.timeMs / kDay] >= kBulkRefreshDrivers;
    };

    std::vector<Change> others;
    bool directFound = false;
    for (const auto& change : sorted) {
        const bool direct = sameModule(change, stem);
        const bool beforeFirst = change.timeMs <= c.firstMs && c.firstMs - change.timeMs <= kChangeLookback;
        const bool duringCase = change.timeMs > c.firstMs && change.timeMs <= c.lastMs;
        if (direct && (beforeFirst || duringCase)) {
            c.changesBefore.push_back(change);
            if (directFound) continue;
            directFound = true;
            const std::string when = change.estimated ? "about " : "";
            if (beforeFirst && historyReachesBack) {
                c.findings.push_back({Certainty::Probable,
                                      describeChange(change) + " changed " + when + daysBetween(change.timeMs, c.firstMs) +
                                          " the first occurrence, and no earlier occurrence is on record."});
                c.causeSupported = true;
            } else if (beforeFirst) {
                c.findings.push_back({Certainty::Correlated,
                                      describeChange(change) + " changed " + when + daysBetween(change.timeMs, c.firstMs) +
                                          " the first occurrence on record, but the logs do not reach far enough back to "
                                          "know whether it was already happening."});
            } else {
                c.findings.push_back({Certainty::Correlated,
                                      describeChange(change) + " changed after this problem had already started, "
                                                               "so that change did not start it."});
            }
        } else if (!direct && beforeFirst && unrelatedChangesMatter(c.signal) && !isBackground(change)) {
            others.push_back(change);
        }
    }

    std::stable_sort(others.begin(), others.end(), [](const Change& a, const Change& b) {
        const auto isDriver = [](const Change& x) { return x.kind != "update_installed"; };
        return isDriver(a) > isDriver(b);
    });
    if (others.size() > kMaxOtherChanges) others.resize(kMaxOtherChanges);
    if (!others.empty()) {
        c.findings.push_back({Certainty::Correlated,
                              pluralize(static_cast<int>(others.size()), "other change", "other changes") +
                                  " happened in the two weeks before. Timing alone does not make them the cause."});
        c.changesBefore.insert(c.changesBefore.end(), others.begin(), others.end());
    }
}

// The part of a key that stays the same for one problem. A blue screen read without its dump has no module, and a dev
// command moves between "server" and "command" with how long it ran; a fix must still follow it.
std::string fixFamily(const std::string& key) {
    if (key.starts_with("bsod:")) {
        const auto second = key.find(':', 5);
        return second == std::string::npos ? key : key.substr(0, second);
    }
    if (key.starts_with("dev_crash:")) return "dev:" + key.substr(10);
    if (key.starts_with("dev_failed:")) return "dev:" + key.substr(11);
    return key;
}

void applyStatusAndAlert(Case& c, const std::vector<Incident>& incidents, const InvestigationInput& input) {
    const auto now = input.nowMs;
    c.longRunning = now - c.firstMs >= kLongRunningAfter && c.incidents >= 4;

    const CaseOverride* latest = nullptr;
    for (const auto& item : input.overrides) {
        const bool same = item.key == c.key || fixFamily(item.key) == fixFamily(c.key);
        if (same && (!latest || item.atMs >= latest->atMs)) latest = &item;
    }
    if (latest && latest->action == "tried_fix") {
        c.fixedMs = latest->atMs;
        const bool recurred = std::any_of(incidents.begin(), incidents.end(), [&](const Incident& incident) {
            return incident.startMs() > latest->atMs;
        });
        c.status = recurred                                   ? CaseStatus::Recurred
                   : now - latest->atMs >= kWatchUntilResolved ? CaseStatus::Resolved
                                                               : CaseStatus::Watching;
    } else if (now - c.lastMs >= kQuietAfter) {
        c.status = CaseStatus::Quiet;
    } else {
        c.status = CaseStatus::Open;
    }

    if (c.status == CaseStatus::Recurred) {
        c.alert = "recurred";
        return;
    }
    if (c.status != CaseStatus::Open || now - c.lastMs >= kRecentWindow) return;
    // An ordinary failed command is the person's own feedback loop, not a problem with the PC.
    if (c.signal == "dev_failed" && c.severity == Severity::Notice) return;

    const int recent = c.incidentsLast7Days;
    const auto earlierSpan = std::max<std::int64_t>(now - kRecentWindow - c.firstMs, kDay);
    const double earlierPerWeek =
        static_cast<double>(c.incidents - recent) * static_cast<double>(kRecentWindow) / static_cast<double>(earlierSpan);

    if (c.severity == Severity::Critical) c.alert = "severe";
    else if (now - c.firstMs < kRecentWindow && c.severity == Severity::Warning) c.alert = "new";
    else if (c.signal == "unexpected_restart" && c.incidents >= kRestartsWorthAlert) c.alert = "repeating";
    else if (c.longRunning) {
        if (recent >= 3 && recent >= 3.0 * earlierPerWeek) c.alert = "increased";
    } else if (recent >= 3) {
        c.alert = "repeating";
    }
}

}  // namespace

std::vector<Change> changesFromEvents(const std::vector<Event>& events) {
    std::vector<Change> changes;
    for (const auto& event : events) {
        if (event.role != Role::Change) continue;
        Change change;
        change.timeMs = event.timeMs;
        change.name = event.subject;
        if (event.signal == "driver_service_installed") {
            change.kind = "driver_installed";
            change.source = "service_install";
            const auto& image = dataOf(event, "ImagePath");
            const auto slash = image.find_last_of("\\/");
            change.module = toLower(slash == std::string::npos ? image : image.substr(slash + 1));
        } else {
            change.kind = isDriverUpdateTitle(event.subject) ? "driver_updated" : "update_installed";
            change.source = "windows_update";
        }
        changes.push_back(std::move(change));
    }
    return changes;
}

std::vector<Case> investigate(const InvestigationInput& input) {
    const auto incidents = buildIncidents(input.events, input.dumps);

    std::map<std::string, std::vector<const Incident*>> byKey;
    for (const auto& incident : incidents) byKey[incident.key].push_back(&incident);

    std::set<std::string> recentBsodKinds;
    for (const auto& incident : incidents) {
        if (incident.signal == "bsod" && input.nowMs - incident.startMs() <= kLongRunningAfter) {
            recentBsodKinds.insert(incident.key);
        }
    }

    std::vector<Case> cases;
    for (auto& [key, group] : byKey) {
        std::sort(group.begin(), group.end(),
                  [](const Incident* a, const Incident* b) { return a->startMs() < b->startMs(); });
        const Incident& latest = *group.back();
        const SignalInfo* info = signalInfo(latest.signal);

        Case c;
        c.key = key;
        c.signal = latest.signal;
        c.subject = latest.subject;
        c.area = info->area;
        c.severity = info->severity;
        c.firstMs = group.front()->startMs();
        c.lastMs = latest.events.back().timeMs;
        c.incidents = static_cast<int>(group.size());
        std::vector<Incident> flat;
        for (const auto* incident : group) {
            flat.push_back(*incident);
            if (input.nowMs - incident->startMs() <= kRecentWindow) ++c.incidentsLast7Days;
            for (const auto& event : incident->events) c.chain.push_back(event);
        }
        if (c.chain.size() > kMaxChain) c.chain.erase(c.chain.begin(), c.chain.end() - kMaxChain);

        const std::string_view signal = c.signal;
        if (signal == "bsod") {
            describeBsod(c, latest, input.drivers);
            if (bugCheckInfo(latest.bugCheck).hint == BugCheckHint::Graphics) c.area = Area::Driver;
            else if (bugCheckInfo(latest.bugCheck).hint == BugCheckHint::Hardware) c.area = Area::Hardware;
            else if (latest.dump && !latest.dump->module.empty()) c.area = Area::Driver;
            if (recentBsodKinds.size() >= 3) {
                c.findings.push_back({Certainty::Correlated,
                                      pluralize(static_cast<int>(recentBsodKinds.size()), "different blue screen", "different blue screens") +
                                          " in 30 days. Varied crashes often share one cause, such as RAM, power or heat, "
                                          "rather than one driver."});
                c.advice.push_back({"Test the RAM with Windows Memory Diagnostic.", "memory_diagnostic"});
            }
        } else if (signal == "unexpected_restart") {
            describeRestart(c, flat);
        } else if (signal == "gpu_reset") {
            describeGpu(c);
        } else if (signal == "whea_fatal" || signal == "whea_corrected") {
            describeWhea(c, signal == "whea_fatal");
        } else if (signal == "driver_load_failed") {
            describeDriverLoad(c);
        } else if (signal == "service_crash") {
            describeService(c);
        } else if (signal == "app_crash" || signal == "app_hang") {
            describeApp(c, latest);
        } else if (signal.starts_with("av_")) {
            describeDefender(c, latest);
        } else if (signal.starts_with("dev_")) {
            describeDev(c, latest);
        } else {
            describeDisk(c, c.signal);
        }

        // Which module the case concerns, when the evidence names one.
        std::string stem = moduleStem(c.subject);
        if (signal == "service_crash" || signal.starts_with("disk") || signal == "storage_reset" || signal.starts_with("app_") || signal.starts_with("av_") || signal.starts_with("dev_")) stem.clear();
        if (const auto* driver = findDriver(input.drivers, stem)) {
            if (!driver->version.empty()) {
                c.findings.push_back({Certainty::Verified, "Installed driver: " +
                                                               (driver->displayName.empty() ? driver->module : driver->displayName) +
                                                               ", version " + driver->version +
                                                               (driver->provider.empty() ? "" : " by " + driver->provider) + "."});
            }
        }
        linkChanges(c, stem, input.changes, input.historyStartMs);

        if (const auto it = input.activity.find(key); it != input.activity.end()) c.activity = it->second;
        applyStatusAndAlert(c, flat, input);
        cases.push_back(std::move(c));
    }

    const auto rank = [](const Case& c) {
        const int status = c.alert.empty() ? (c.status == CaseStatus::Open ? 1 : 2) : 0;
        return std::make_tuple(status, -static_cast<int>(c.severity), -c.lastMs);
    };
    std::sort(cases.begin(), cases.end(), [&](const Case& a, const Case& b) { return rank(a) < rank(b); });
    return cases;
}

}  // namespace why

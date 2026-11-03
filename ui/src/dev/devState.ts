// Development only: lets the interface run in a browser without the C++ host. Never bundled in why.exe.
import type { State } from "../types";

const now = Date.now();
const day = 86_400_000;

// Invented data, to review the screens with a case richer than whatever this PC happens to have.
const sample: State = {
  schema: 1,
  appVersion: "dev",
  releaseName: "WHY - Triage",
  mode: "investigation",
  scanning: false,
  elevated: false,
  machine: { computerName: "SAMPLE", os: "Windows 11 Pro (build 26200)" },
  historyStartMs: now - 80 * day,
  generatedMs: now,
  rev: 1,
  coverage: [
    { id: "event_log", label: "Windows System log", status: "ok", detail: "Blue screens, restarts, driver, disk and hardware errors.", count: 0, oldestMs: now - 80 * day },
    { id: "drivers", label: "Installed drivers", status: "ok", detail: "Drivers running or attached to a present device.", count: 2, oldestMs: 0 },
    { id: "crash_dumps", label: "Crash dumps", status: "limited", detail: "Run as administrator to read crash dumps, which can name the responsible driver.", count: 0, oldestMs: 0 },
    { id: "usn", label: "File change journal", status: "limited", detail: "Needs administrator rights.", count: 0, oldestMs: 0 },
  ],
  cases: [
    {
      key: "gpu_reset:nvlddmkm",
      signal: "gpu_reset",
      area: "driver",
      severity: "warning",
      status: "open",
      title: "Graphics driver stopped responding: nvlddmkm",
      summary: "Windows or the graphics driver reported that the GPU stopped responding.",
      subject: "nvlddmkm",
      firstMs: now - 3 * day,
      lastMs: now - 3600_000,
      incidents: 4,
      incidentsLast7Days: 4,
      alert: "new",
      longRunning: false,
      causeSupported: true,
      chain: [3, 2, 1].map((d) => ({
        timeMs: now - d * day,
        provider: "Display",
        eventId: 4101,
        signal: "gpu_reset",
        subject: "nvlddmkm",
        message: "Display driver nvlddmkm stopped responding and has successfully recovered.",
        channel: "System",
      })),
      findings: [
        { certainty: "verified", text: "The graphics driver nvlddmkm reported a problem 4 times." },
        { certainty: "verified", text: "Installed driver: NVIDIA GeForce RTX 3070, version 32.0.15.6094 by NVIDIA." },
        { certainty: "probable", text: "NVIDIA GeForce RTX 3070 (32.0.15.5599 to 32.0.15.6094) changed 2 days before the first occurrence, and no earlier occurrence is on record." },
        { certainty: "correlated", text: "2 other changes happened in the two weeks before. Timing alone does not make them the cause." },
      ],
      changesBefore: [
        { timeMs: now - 5 * day, kind: "driver_updated", name: "NVIDIA GeForce RTX 3070", module: "nvlddmkm.sys", oldVersion: "32.0.15.5599", newVersion: "32.0.15.6094", source: "windows_update", estimated: false },
        { timeMs: now - 9 * day, kind: "driver_installed", name: "Realtek Audio", module: "rtkvhd64.sys", oldVersion: "", newVersion: "6.0.9600", source: "driver_store", estimated: true },
        { timeMs: now - 11 * day, kind: "update_installed", name: "Cumulative Update for Windows 11 (KB5040000)", module: "", oldVersion: "", newVersion: "", source: "windows_update", estimated: false },
      ],
      advice: [
        { text: "Update or roll back the graphics driver from Device Manager.", tool: "device_manager" },
        { text: "If it happens under load, check GPU temperature and power cables.", tool: "" },
      ],
      activity: [
        { timeMs: now - 3 * day - 60_000, kind: "process_start", image: "\\Program Files\\Game\\game.exe", pid: 4120 },
        { timeMs: now - 3 * day - 30_000, kind: "driver_load", image: "\\Windows\\System32\\drivers\\sample.sys", pid: 4 },
      ],
    },
    {
      key: "unexpected_restart",
      signal: "unexpected_restart",
      area: "system",
      severity: "warning",
      status: "open",
      title: "Windows restarted without shutting down",
      summary: "The PC went off or restarted without a clean shutdown, and no blue screen was recorded.",
      subject: "",
      firstMs: now - 6 * day,
      lastMs: now - 6 * day,
      incidents: 1,
      incidentsLast7Days: 1,
      alert: "new",
      longRunning: false,
      causeSupported: false,
      chain: [
        { timeMs: now - 6 * day, provider: "Microsoft-Windows-Kernel-Power", eventId: 41, signal: "unexpected_restart", subject: "", message: "The system has rebooted without cleanly shutting down first.", channel: "System" },
      ],
      findings: [
        { certainty: "verified", text: "Windows recorded 1 restart without a clean shutdown and without a stop code." },
        { certainty: "correlated", text: "Common reasons are a power cut, an unstable power supply, overheating or a complete freeze. The logs cannot tell these apart." },
      ],
      changesBefore: [],
      advice: [{ text: "See when this started in Reliability Monitor.", tool: "reliability_monitor" }],
      activity: [],
    },
  ],
  timeline: [],
  changes: [],
  drivers: [
    { service: "nvlddmkm", module: "nvlddmkm.sys", displayName: "NVIDIA GeForce RTX 3070", provider: "NVIDIA", version: "32.0.15.6094", driverDate: "2026-07-01", imagePath: "", deviceClass: "Display", installedMs: now - 5 * day, running: true },
    { service: "rtkvhd64", module: "rtkvhd64.sys", displayName: "Realtek Audio", provider: "Realtek", version: "6.0.9600", driverDate: "2026-03-01", imagePath: "", deviceClass: "MEDIA", installedMs: now - 9 * day, running: true },
  ],
  integrity: [],
};
sample.timeline = sample.cases.flatMap((item) => item.chain).sort((a, b) => b.timeMs - a.timeMs);
sample.changes = sample.cases.flatMap((item) => item.changesBefore).sort((a, b) => b.timeMs - a.timeMs);

export async function loadDevState(): Promise<State> {
  // ?real reads a report saved from this PC and copied to public/dev-state.json.
  if (new URLSearchParams(location.search).has("real")) return (await fetch("/dev-state.json")).json();
  return sample;
}

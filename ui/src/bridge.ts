// Talks to the C++ host through WebView2. Replies are matched to requests by id.
import type { ProcessInfo, RunningProgram, Settings, State, SystemStats } from "./types";

interface HostChannel {
  postMessage(message: unknown): void;
  addEventListener(type: "message", listener: (event: { data: any }) => void): void;
}

declare global {
  interface Window {
    chrome?: { webview?: HostChannel };
  }
}

export interface ActionResult {
  ok: boolean;
  cancelled?: boolean;
  error?: string;
  path?: string;
}

const host = window.chrome?.webview;
const pending = new Map<number, (value: any) => void>();
const listeners = new Set<() => void>();
let nextId = 1;

host?.addEventListener("message", ({ data }) => {
  if (data.event === "stateChanged") {
    listeners.forEach((listener) => listener());
  } else if (typeof data.id === "number") {
    pending.get(data.id)?.(data.result);
    pending.delete(data.id);
  }
});

function request<T>(cmd: string, args: Record<string, unknown> = {}): Promise<T> {
  if (!host) return Promise.reject(new Error("No host"));
  return new Promise<T>((resolve) => {
    const id = nextId++;
    pending.set(id, resolve);
    host.postMessage({ id, cmd, ...args });
  });
}

export const hasHost = Boolean(host);
export const onStateChanged = (listener: () => void) => {
  listeners.add(listener);
  return () => void listeners.delete(listener);
};
export const getState = () => request<State>("getState");
export const caseAction = (key: string, action: "tried_fix" | "reopen") => request<ActionResult>("caseAction", { key, action });
export const openTool = (tool: string) => request<ActionResult>("openTool", { tool });
export const saveReport = () => request<ActionResult>("saveReport");
export const openReport = () => request<ActionResult>("openReport");
export const closeReport = () => request<ActionResult>("closeReport");
export const restartElevated = () => request<ActionResult>("restartElevated");
// In the browser there is no service to install; the change only pretends to succeed.
export const installLive = (): Promise<ActionResult> => (host ? request<ActionResult>("installLive") : Promise.resolve({ ok: true }));
export const uninstallLive = (): Promise<ActionResult> => (host ? request<ActionResult>("uninstallLive") : Promise.resolve({ ok: true }));

export interface SettingsReply {
  settings: Settings;
  elevated: boolean;
}

// In the browser during development there is no host, so settings live in this browser only.
const devSettingsKey = "why-dev-settings";
const devDefaults: Settings = { setupDone: false, runAsAdmin: false, startAtLogin: false, keepInBackground: false, focusKind: "", focusTarget: "" };

export async function getSettings(): Promise<SettingsReply> {
  if (host) return request<SettingsReply>("getSettings");
  let stored: Partial<Settings> = {};
  try {
    stored = JSON.parse(localStorage.getItem(devSettingsKey) ?? "{}");
  } catch {
    // Private windows may refuse storage. The defaults are fine.
  }
  return { settings: { ...devDefaults, ...stored }, elevated: false };
}

export async function saveSettings(settings: Settings): Promise<ActionResult> {
  if (host) return request<ActionResult>("saveSettings", { settings });
  try {
    localStorage.setItem(devSettingsKey, JSON.stringify(settings));
  } catch {
    // As above: nothing to keep in a private window.
  }
  return { ok: true };
}

export const chooseFolder = (): Promise<ActionResult> =>
  host ? request<ActionResult>("chooseFolder") : Promise.resolve({ ok: true, path: "C:\\Users\\dev\\source\\Treasury" });
export const chooseProgram = (): Promise<ActionResult> =>
  host ? request<ActionResult>("chooseProgram") : Promise.resolve({ ok: true, path: "C:\\Users\\dev\\source\\Treasury\\desktop\\Treasury.exe" });
export const listPrograms = (): Promise<RunningProgram[]> =>
  host
    ? request<RunningProgram[]>("listPrograms")
    : Promise.resolve([
        { name: "Code.exe", path: "C:\\Users\\dev\\AppData\\Local\\Programs\\Microsoft VS Code\\Code.exe" },
        { name: "Treasury.exe", path: "C:\\Users\\dev\\source\\Treasury\\desktop\\Treasury.exe" },
      ]);

// During development the list is invented, with CPU that moves so the screen can be judged live.
const devProcesses: Omit<ProcessInfo, "cpu" | "disk" | "gpu" | "parent">[] = [
  { pid: 6120, group: "dev", name: "node.exe", path: "C:\\Program Files\\nodejs\\node.exe", memory: 210_000_000, threads: 14, ports: [3000] },
  { pid: 6188, group: "dev", name: "node.exe", path: "C:\\Program Files\\nodejs\\node.exe", memory: 95_000_000, threads: 11, ports: [5173, 24678] },
  { pid: 4120, group: "app", name: "Treasury.exe", path: "C:\\Users\\dev\\source\\Treasury\\desktop\\Treasury.exe", memory: 312_000_000, threads: 38, ports: [] },
  { pid: 9932, group: "app", name: "Code.exe", path: "C:\\Users\\dev\\AppData\\Local\\Programs\\Microsoft VS Code\\Code.exe", memory: 640_000_000, threads: 52, ports: [] },
  { pid: 1204, group: "app", name: "notepad++.exe", path: "C:\\Program Files\\Notepad++\\notepad++.exe", memory: 48_000_000, threads: 9, ports: [] },
  { pid: 3310, group: "background", name: "OneDrive.exe", path: "C:\\Users\\dev\\AppData\\Local\\Microsoft\\OneDrive\\OneDrive.exe", memory: 96_000_000, threads: 30, ports: [] },
  { pid: 7790, group: "app", name: "chrome.exe", path: "C:\\Program Files\\Google\\Chrome\\Application\\chrome.exe", memory: 240_000_000, threads: 20, ports: [] },
  { pid: 7788, group: "app", name: "chrome.exe", path: "C:\\Program Files\\Google\\Chrome\\Application\\chrome.exe", memory: 820_000_000, threads: 61, ports: [] },
  { pid: 5768, group: "windows", name: "MsMpEng.exe", path: "", memory: 210_000_000, threads: 47, ports: [] },
  { pid: 1460, group: "windows", name: "explorer.exe", path: "C:\\Windows\\explorer.exe", memory: 180_000_000, threads: 112, ports: [] },
  { pid: 868, group: "windows", name: "svchost.exe", path: "C:\\Windows\\System32\\svchost.exe", memory: 22_000_000, threads: 12, ports: [] },
  { pid: 4, group: "windows", name: "System", path: "", memory: 150_000, threads: 260, ports: [] },
];

export const listProcesses = (): Promise<ProcessInfo[]> =>
  host
    ? request<ProcessInfo[]>("listProcesses")
    : Promise.resolve(
        devProcesses.map((process, index) => ({
          ...process,
          parent: 1,
          cpu: Math.max(0, (index % 3) * 4 + Math.random() * 6 - 2),
          disk: index % 4 === 0 ? Math.random() * 4_000_000 : 0,
          gpu: process.name === "chrome.exe" || process.name === "Treasury.exe" ? Math.random() * 20 : 0,
        })),
      );

// A random walk, so the Monitor screen moves in the browser the way it would on a real PC.
let devWalk = { cpu: 18, gpu: 9, disk: 4, net: 2_000_000 };
const wander = (value: number, step: number, max: number) => Math.min(max, Math.max(0, value + (Math.random() - 0.5) * step));

export function systemStats(): Promise<SystemStats> {
  if (host) return request<SystemStats>("systemStats");
  devWalk = { cpu: wander(devWalk.cpu, 14, 100), gpu: wander(devWalk.gpu, 10, 100), disk: wander(devWalk.disk, 8, 100), net: wander(devWalk.net, 900_000, 12_000_000) };
  const total = 34_359_738_368;
  return Promise.resolve({
    timeMs: Date.now(),
    uptimeMs: 3 * 86_400_000 + 4 * 3_600_000,
    cpu: {
      name: "12th Gen Intel(R) Core(TM) i5-12600KF",
      usage: devWalk.cpu,
      cores: Array.from({ length: 16 }, (_, index) => wander(devWalk.cpu, 30 + index, 100)),
      logical: 16,
      physical: 10,
      baseMhz: 3700,
      currentMhz: 4300 + Math.random() * 300,
    },
    memory: { total, available: total * 0.41, used: total * 0.59, load: 59, committed: total * 0.72, commitLimit: total * 1.3, cached: total * 0.22 },
    gpu: { usage: devWalk.gpu, adapters: [{ name: "NVIDIA GeForce RTX 3070", dedicated: 8_589_934_592 }], memoryUsed: 2_900_000_000 },
    disks: [
      { name: "0 C:", active: devWalk.disk, read: devWalk.disk * 400_000, write: devWalk.disk * 120_000 },
      { name: "1 D:", active: devWalk.disk / 3, read: 50_000, write: 0 },
    ],
    volumes: [
      { root: "C:\\", label: "Windows", fileSystem: "NTFS", total: 1_000_000_000_000, free: 212_000_000_000, removable: false },
      { root: "D:\\", label: "Games", fileSystem: "NTFS", total: 2_000_000_000_000, free: 96_000_000_000, removable: false },
    ],
    network: [{ name: "Intel(R) Ethernet Controller I225-V", bandwidth: 1_000_000_000, received: devWalk.net, sent: devWalk.net / 6 }],
    power: { battery: false, percent: -1, pluggedIn: true },
    processes: 358,
    threads: 5120,
    handles: 182_400,
  });
}

// Program icons, as PNG data URIs by path. In the browser there are none, and rows fall back to a glyph.
export const programIcons = (paths: string[]): Promise<Record<string, string>> =>
  host ? request<Record<string, string>>("programIcons", { paths }) : Promise.resolve({});

// Shows a file in File Explorer, selected. Only files that exist; WHY? opens nothing else.
export const revealPath = (path: string) => (host ? request<ActionResult>("revealPath", { path }) : Promise.resolve({ ok: true }));
// Opens a local dev server in the default browser. Only http://localhost with a port.
export const openLocalhost = (port: number) =>
  host ? request<ActionResult>("openLocalhost", { port }) : (window.open(`http://localhost:${port}`, "_blank"), Promise.resolve({ ok: true }));

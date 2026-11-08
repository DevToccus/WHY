// Mirrors the JSON the engine produces (src/core/serialize.cpp).
export type Area = "hardware" | "driver" | "software" | "system" | "security" | "development";
export type Severity = "notice" | "warning" | "critical";
export type Certainty = "verified" | "probable" | "correlated";
export type CaseStatus = "open" | "quiet" | "watching" | "recurred" | "resolved";
export type Mode = "live" | "investigation" | "report";

export interface WhyEvent {
  timeMs: number;
  provider: string;
  eventId: number;
  signal: string;
  subject: string;
  message: string;
  channel: string;
}

export interface Change {
  timeMs: number;
  kind: string;
  name: string;
  module: string;
  oldVersion: string;
  newVersion: string;
  source: string;
  estimated: boolean;
}

export interface Activity {
  timeMs: number;
  kind: string;
  image: string;
  pid: number;
}

export interface Case {
  key: string;
  signal: string;
  area: Area;
  severity: Severity;
  status: CaseStatus;
  title: string;
  summary: string;
  subject: string;
  firstMs: number;
  lastMs: number;
  incidents: number;
  incidentsLast7Days: number;
  alert: string;
  longRunning: boolean;
  causeSupported: boolean;
  chain: WhyEvent[];
  findings: { certainty: Certainty; text: string }[];
  changesBefore: Change[];
  advice: { text: string; tool: string }[];
  activity: Activity[];
  output?: string; // The end of a failed command's error output, kept by why-run. Missing in older reports.
  image?: string; // The program the case is about, for its icon.
  // Whether it was a real error or a forced closure, and why WHY? says so. Missing in older reports.
  origin?: "" | "error" | "forced" | "unclear";
  originText?: string;
  fixedMs?: number; // When the person last marked it as fixed, 0 if never.
}

export interface Coverage {
  id: string;
  label: string;
  status: "ok" | "limited" | "unavailable";
  detail: string;
  count: number;
  oldestMs: number;
}

export interface Driver {
  service: string;
  module: string;
  displayName: string;
  provider: string;
  version: string;
  driverDate: string;
  imagePath: string;
  deviceClass: string;
  installedMs: number;
  running: boolean;
}

export interface State {
  schema: number;
  appVersion: string;
  releaseName?: string; // Missing in reports saved before code names existed.
  mode: Mode;
  scanning: boolean;
  elevated: boolean;
  machine: { computerName: string; os: string };
  historyStartMs: number;
  generatedMs: number;
  rev: number;
  coverage: Coverage[];
  cases: Case[];
  timeline: WhyEvent[];
  changes: Change[];
  drivers: Driver[];
  integrity: WhyEvent[];
}

// Mirrors src/app/settings.h. Kept by the host in the current user's registry.
export interface Settings {
  setupDone: boolean;
  runAsAdmin: boolean;
  startAtLogin: boolean;
  keepInBackground: boolean;
  focusKind: "" | "folder" | "program";
  focusTarget: string;
}

export interface RunningProgram {
  name: string;
  path: string;
}

// One running process, sampled by the host. CPU and GPU are percentages of the whole machine, disk is bytes per second,
// all measured since the previous sample. Memory is the private working set, as Task Manager shows it.
export interface ProcessInfo {
  pid: number;
  parent: number;
  name: string;
  path: string;
  memory: number;
  cpu: number;
  disk: number;
  gpu: number;
  threads: number;
  group: "dev" | "app" | "background" | "windows";
  ports: number[];
}

// Mirrors SystemMonitor::sample in src/app/monitor.cpp. Rates are per second.
export interface SystemStats {
  timeMs: number;
  uptimeMs: number;
  cpu: { name: string; usage: number; cores: number[]; logical: number; physical: number; baseMhz: number; currentMhz: number };
  memory: { total: number; available: number; used: number; load: number; committed: number; commitLimit: number; cached: number };
  gpu: { usage: number; adapters: { name: string; dedicated: number }[]; memoryUsed: number };
  disks: { name: string; active: number; read: number; write: number }[];
  volumes: { root: string; label: string; fileSystem: string; total: number; free: number; removable: boolean }[];
  network: { name: string; bandwidth: number; received: number; sent: number }[];
  power: { battery: boolean; percent: number; pluggedIn: boolean };
  processes: number;
  threads: number;
  handles: number;
}

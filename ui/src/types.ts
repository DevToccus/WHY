// Mirrors the JSON the engine produces (src/core/serialize.cpp).
export type Area = "hardware" | "driver" | "software" | "system";
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

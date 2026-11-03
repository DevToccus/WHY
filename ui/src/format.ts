import type { Area, Case, CaseStatus, Certainty, Change } from "./types";

const dayFormat = new Intl.DateTimeFormat(undefined, { day: "numeric", month: "short", year: "numeric" });
const timeFormat = new Intl.DateTimeFormat(undefined, { hour: "2-digit", minute: "2-digit" });

export const formatDay = (ms: number) => dayFormat.format(ms);
export const formatTime = (ms: number) => timeFormat.format(ms);
export const formatMoment = (ms: number) => `${formatDay(ms)}, ${formatTime(ms)}`;

export function relativeDay(ms: number, now: number): string {
  const days = Math.floor((now - ms) / 86_400_000);
  if (days <= 0) return "today";
  if (days === 1) return "yesterday";
  if (days < 60) return `${days} days ago`;
  return formatDay(ms);
}

export const areaLabels: Record<Area, string> = {
  hardware: "Hardware",
  driver: "Drivers",
  software: "Software",
  system: "Windows",
};

export const certaintyLabels: Record<Certainty, string> = {
  verified: "Verified",
  probable: "Probable",
  correlated: "Only correlated",
};

export const statusLabels: Record<CaseStatus, string> = {
  open: "Open",
  quiet: "No recent occurrences",
  watching: "Watching after your fix",
  recurred: "Came back after your fix",
  resolved: "Has not come back",
};

export const alertLabels: Record<string, string> = {
  severe: "Serious",
  repeating: "Keeps happening",
  new: "New",
  increased: "Happening more often",
};

export const signalLabels: Record<string, string> = {
  bsod: "Blue screen",
  unexpected_restart: "Restart without shutdown",
  gpu_reset: "Graphics driver stopped responding",
  whea_fatal: "Fatal hardware error",
  whea_corrected: "Corrected hardware error",
  disk_failure_predicted: "Disk predicts failure",
  disk_error: "Disk error",
  disk_retry: "Disk operation retried",
  storage_reset: "Storage device reset",
  filesystem_corruption: "File system corruption",
  driver_load_failed: "Driver failed to load",
  service_crash: "Service stopped unexpectedly",
  driver_service_installed: "Driver installed",
  update_installed: "Windows Update installed something",
  log_cleared: "System log cleared",
};

export const changeKindLabels: Record<string, string> = {
  driver_installed: "Driver installed",
  driver_updated: "Driver updated",
  driver_removed: "Driver removed",
  file_changed: "Driver file changed",
  update_installed: "Update installed",
};

export const sourceLabels: Record<string, string> = {
  snapshot: "compared with an earlier snapshot",
  driver_store: "date of the driver's folder",
  service_install: "Windows log",
  windows_update: "Windows Update log",
  usn: "file change journal",
};

export const plural = (count: number, one: string, many = `${one}s`) => `${count} ${count === 1 ? one : many}`;

export function changeVersion(change: Change): string {
  if (change.oldVersion && change.newVersion) return `${change.oldVersion} to ${change.newVersion}`;
  return change.newVersion || change.oldVersion;
}

export const needsLook = (item: Case) => item.alert !== "";

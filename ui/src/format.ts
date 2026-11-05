import type { Area, Case, CaseStatus, Certainty, Change, Severity } from "./types";

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
  security: "Security",
  development: "Development",
};

export const certaintyLabels: Record<Certainty, string> = {
  verified: "Verified",
  probable: "Probable",
  correlated: "Only correlated",
};

export const statusLabels: Record<CaseStatus, string> = {
  open: "Open",
  quiet: "No recent occurrences",
  watching: "Marked as fixed, watching",
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
  app_crash: "App crashed",
  app_hang: "App stopped responding",
  av_detection: "Microsoft Defender detected a threat",
  av_action_failed: "Microsoft Defender could not remove a threat",
  av_disabled: "Real-time protection turned off",
  dev_crash: "Dev server stopped with an error",
  dev_failed: "Command failed",
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

// Marked as fixed by the person, and not back since. Still listed, never tinted.
export const isFixed = (item: Case) => item.status === "watching" || item.status === "resolved";

// Whether a timeline record is something that went wrong, something that changed, or evidence about the logs themselves.
export const roleOf = (signal: string) =>
  signal === "driver_service_installed" || signal === "update_installed" ? "change" : signal === "log_cleared" ? "evidence" : "problem";

// Mirrors the signal table in src/core/signals.cpp.
export const signalSeverity: Record<string, Severity> = {
  bsod: "critical",
  unexpected_restart: "warning",
  gpu_reset: "warning",
  whea_fatal: "critical",
  whea_corrected: "warning",
  disk_failure_predicted: "critical",
  disk_error: "critical",
  disk_retry: "warning",
  storage_reset: "warning",
  filesystem_corruption: "critical",
  driver_load_failed: "notice",
  service_crash: "notice",
  app_crash: "warning",
  app_hang: "warning",
  av_detection: "critical",
  av_action_failed: "critical",
  av_disabled: "warning",
  dev_crash: "warning",
  dev_failed: "notice",
};

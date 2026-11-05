// Focus narrows WHY? to one program, or the programs in one folder. Mirrors matchesFocus in src/app/settings.cpp.
import type { Settings, State } from "./types";

const fileName = (path: string) => path.slice(Math.max(path.lastIndexOf("\\"), path.lastIndexOf("/")) + 1).toLowerCase();

export const hasFocus = (settings: Settings | null): settings is Settings => Boolean(settings?.focusKind && settings.focusTarget);

export function matchesFocus(settings: Settings, signal: string, subject: string): boolean {
  // Defender can quarantine the very program being built, so its detections are never hidden.
  if (signal === "av_detection" || signal === "av_action_failed") return true;
  // A dev server or command belongs to the folder it ran in: its subject is "label@folder".
  if (signal === "dev_crash" || signal === "dev_failed") {
    if (settings.focusKind !== "folder") return false;
    const target = settings.focusTarget.toLowerCase().replace(/[\\/]+$/, "");
    const folder = subject.slice(subject.indexOf("@") + 1).toLowerCase();
    return folder === target || folder.startsWith(`${target}\\`);
  }
  if (signal !== "app_crash" && signal !== "app_hang") return false;
  if (settings.focusKind === "program") return fileName(subject) === fileName(settings.focusTarget);
  const target = settings.focusTarget.toLowerCase();
  const folder = target.endsWith("\\") ? target : `${target}\\`;
  return subject.toLowerCase().startsWith(folder);
}

export const focusName = (settings: Settings) =>
  settings.focusKind === "program" ? settings.focusTarget.slice(settings.focusTarget.lastIndexOf("\\") + 1) : settings.focusTarget;

// The state as the screens should see it: only what concerns the focus, nothing about Windows itself.
export function focusedState(state: State, settings: Settings): State {
  return {
    ...state,
    cases: state.cases.filter((item) => matchesFocus(settings, item.signal, item.subject)),
    timeline: state.timeline.filter((event) => matchesFocus(settings, event.signal, event.subject)),
  };
}

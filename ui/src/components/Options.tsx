import { useEffect, useState } from "react";
import * as bridge from "../bridge";
import type { RunningProgram, Settings } from "../types";
import { Icon, type IconName } from "./Icon";

// The Windows 11 toggle switch, with its On/Off label beside it.
export function Toggle({ checked, onChange, label }: { checked: boolean; onChange: (next: boolean) => void; label: string }) {
  return (
    <label className="toggle">
      <span className="toggle-state">{checked ? "On" : "Off"}</span>
      <input type="checkbox" role="switch" aria-label={label} checked={checked} onChange={(event) => onChange(event.target.checked)} />
      <span className="toggle-track" aria-hidden="true" />
    </label>
  );
}

// One setting as a card: icon, what it does, and its control on the right.
export function SettingRow({ icon, title, detail, children }: { icon: IconName; title: string; detail: string; children: React.ReactNode }) {
  return (
    <div className="row static setting">
      <span className="severity-icon">
        <Icon name={icon} />
      </span>
      <span className="row-main">
        <strong>{title}</strong>
        <span className="muted">{detail}</span>
      </span>
      {children}
    </div>
  );
}

type Startup = Pick<Settings, "runAsAdmin" | "startAtLogin" | "keepInBackground">;

export function StartupOptions({ value, onChange }: { value: Startup; onChange: (next: Startup) => void }) {
  return (
    <div className="rows">
      <SettingRow
        icon="shield"
        title="Always run as administrator"
        detail="Reads crash dumps and the file change journal, and records which programs and drivers started. Windows asks you to confirm each time you open WHY?."
      >
        <Toggle label="Always run as administrator" checked={value.runAsAdmin} onChange={(runAsAdmin) => onChange({ ...value, runAsAdmin })} />
      </SettingRow>
      <SettingRow
        icon="power"
        title="Start with Windows"
        detail="Adds WHY? to your Startup folder. It starts hidden at sign-in, without administrator rights."
      >
        <Toggle label="Start with Windows" checked={value.startAtLogin} onChange={(startAtLogin) => onChange({ ...value, startAtLogin })} />
      </SettingRow>
      <SettingRow
        icon="background"
        title="Keep running when closed"
        detail="Closing the window leaves WHY? in the notification area, so it can still tell you when something goes wrong."
      >
        <Toggle label="Keep running when closed" checked={value.keepInBackground} onChange={(keepInBackground) => onChange({ ...value, keepInBackground })} />
      </SettingRow>
    </div>
  );
}

export type RunMode = "live" | "app";

const runModes: { mode: RunMode; icon: IconName; title: string; detail: string }[] = [
  {
    mode: "live",
    icon: "shield",
    title: "Always watching",
    detail:
      "Recommended. A small background service reads everything as administrator, keeps history and notifies you, even with WHY? closed and after a restart. Windows asks for permission once.",
  },
  {
    mode: "app",
    icon: "app",
    title: "Only while WHY? runs",
    detail: "Nothing is installed. WHY? reads what Windows kept each time you open it, and watches while it is running.",
  },
];

// How WHY? runs: the background service, which covers administrator rights, sign-in and notifications by itself,
// or the app alone, with its own three choices.
export function RunModeOptions({
  mode,
  onMode,
  startup,
  onStartup,
}: {
  mode: RunMode;
  onMode: (next: RunMode) => void;
  startup: Startup;
  onStartup: (next: Startup) => void;
}) {
  return (
    <div className="focus-options">
      <div className="choice-grid choice-grid-two" role="radiogroup" aria-label="How WHY? runs">
        {runModes.map((choice) => (
          <button key={choice.mode} role="radio" aria-checked={mode === choice.mode} className="choice" onClick={() => onMode(choice.mode)}>
            <Icon name={choice.icon} className="choice-icon" />
            <strong>{choice.title}</strong>
            <span className="muted">{choice.detail}</span>
          </button>
        ))}
      </div>
      {mode === "app" && <StartupOptions value={startup} onChange={onStartup} />}
    </div>
  );
}

type Focus = Pick<Settings, "focusKind" | "focusTarget">;

const focusChoices: { kind: Settings["focusKind"]; icon: IconName; title: string; detail: string }[] = [
  { kind: "", icon: "pc", title: "The whole PC", detail: "Windows, drivers, hardware and every program." },
  { kind: "folder", icon: "folder", title: "A folder I develop in", detail: "Only programs that run from this folder or below it." },
  { kind: "program", icon: "app", title: "One program", detail: "Only this program, wherever it runs from." },
];

export function FocusOptions({ value, onChange }: { value: Focus; onChange: (next: Focus) => void }) {
  const [programs, setPrograms] = useState<RunningProgram[] | null>(null);

  useEffect(() => {
    if (value.focusKind === "program" && programs === null) bridge.listPrograms().then(setPrograms);
  }, [value.focusKind, programs]);

  const pick = async (choose: () => Promise<bridge.ActionResult>, kind: Settings["focusKind"]) => {
    const result = await choose();
    if (result.ok && result.path) onChange({ focusKind: kind, focusTarget: result.path });
  };

  return (
    <div className="focus-options">
      <div className="choice-grid" role="radiogroup" aria-label="What WHY? watches">
        {focusChoices.map((choice) => (
          <button
            key={choice.kind || "pc"}
            role="radio"
            aria-checked={value.focusKind === choice.kind}
            className="choice"
            onClick={() => onChange({ focusKind: choice.kind, focusTarget: choice.kind === value.focusKind ? value.focusTarget : "" })}
          >
            <Icon name={choice.icon} className="choice-icon" />
            <strong>{choice.title}</strong>
            <span className="muted">{choice.detail}</span>
          </button>
        ))}
      </div>

      {value.focusKind === "folder" && (
        <SettingRow icon="folder" title={value.focusTarget || "No folder chosen yet"} detail="Crashes and hangs of programs inside this folder.">
          <button className="quiet" onClick={() => pick(bridge.chooseFolder, "folder")}>
            {value.focusTarget ? "Change folder" : "Choose folder"}
          </button>
        </SettingRow>
      )}

      {value.focusKind === "program" && (
        <SettingRow icon="app" title={value.focusTarget || "No program chosen yet"} detail="Pick one that is running now, or browse for its .exe file.">
          <span className="setting-controls">
            <select
              aria-label="Running programs"
              value=""
              onChange={(event) => event.target.value && onChange({ focusKind: "program", focusTarget: event.target.value })}
            >
              <option value="">{programs === null ? "Looking at running programs" : "Running now"}</option>
              {programs?.map((program) => (
                <option key={program.path} value={program.path}>
                  {program.name}
                </option>
              ))}
            </select>
            <button className="quiet" onClick={() => pick(bridge.chooseProgram, "program")}>
              Browse
            </button>
          </span>
        </SettingRow>
      )}
    </div>
  );
}

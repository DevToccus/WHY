import { useState } from "react";
import * as bridge from "../bridge";
import { Icon } from "../components/Icon";
import { RunModeOptions } from "../components/Options";
import { formatMoment } from "../format";
import type { Settings, State } from "../types";

interface Props {
  state: State;
  settings: Settings;
  onChange: (settings: Settings) => void;
  onAction: (action: () => Promise<bridge.ActionResult>, success?: string) => void;
}

// Each change applies at once, as in Windows Settings.
export function SettingsScreen({ state, settings, onChange, onAction }: Props) {
  const isReport = state.mode === "report";
  const live = state.mode === "live";
  const [error, setError] = useState<string | null>(null);

  const apply = async (next: Settings) => {
    setError(null);
    onChange(next);
    const result = await bridge.saveSettings(next);
    if (!result.ok) setError(result.error ?? "Windows did not accept this change.");
  };

  return (
    <>
      <h1>Settings</h1>
      {error && <p className="tone-critical">{error}</p>}

      {!isReport && (
        <section>
          <h2 className="section-title">How WHY? runs</h2>
          <RunModeOptions
            mode={live ? "live" : "app"}
            onMode={(next) => {
              if (next === "live" && !live) {
                onAction(async () => {
                  const result = await bridge.installLive();
                  // The service starts with Windows and reads as administrator, so the app's own startup choices go.
                  if (result.ok) await apply({ ...settings, runAsAdmin: false, startAtLogin: false, keepInBackground: false });
                  return result;
                }, "WHY? is now always watching.");
              } else if (next === "app" && live) {
                onAction(bridge.uninstallLive, "The background service and its history were removed.");
              }
            }}
            startup={settings}
            onStartup={(next) => apply({ ...settings, ...next })}
          />
        </section>
      )}

      <section>
        <h2 className="section-title">What WHY? can read</h2>
        <ul className="rows">
          {state.coverage.map((source) => (
            <li className="row static" key={source.id}>
              <span className={`severity-icon tone-${source.status === "ok" ? "ok" : "warning"}`}>
                <Icon name={source.status === "ok" ? "ok" : "warning"} />
              </span>
              <span className="row-main">
                <strong>{source.label}</strong>
                <span className="muted">
                  {source.detail}
                  {source.oldestMs > 0 && ` Reaches back to ${formatMoment(source.oldestMs)}.`}
                </span>
              </span>
              <span className="row-side">{source.status === "ok" ? "Available" : "Limited"}</span>
            </li>
          ))}
        </ul>
        {!isReport && !state.elevated && (
          <p className="action-line">
            <button onClick={() => onAction(bridge.restartElevated)}>Restart as administrator</button>
            <span className="muted">Reads crash dumps and the file change journal, and starts the activity recorder.</span>
          </p>
        )}
      </section>

      <section>
        <h2 className="section-title">Setup</h2>
        <p className="action-line">
          <button className="quiet" onClick={() => apply({ ...settings, setupDone: false })}>
            Run setup again
          </button>
        </p>
      </section>
    </>
  );
}

import { useState } from "react";
import * as bridge from "../bridge";
import { Icon } from "../components/Icon";
import { StartupOptions } from "../components/Options";
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

      <section>
        <h2 className="section-title">Startup and background</h2>
        <StartupOptions value={settings} onChange={(next) => apply({ ...settings, ...next })} />
      </section>

      {!isReport && (
        <section>
          <h2 className="section-title">Live monitoring</h2>
          <div className="row static">
            <span className="severity-icon">
              <Icon name="monitor" />
            </span>
            <span className="row-main">
              <strong>{state.mode === "live" ? "Live monitoring is on" : "Live monitoring is off"}</strong>
              <span className="muted">
                {state.mode === "live"
                  ? "A background service keeps history, reads everything as administrator and notifies you. It only observes."
                  : "A background service that keeps history and reads everything as administrator, even when WHY? is closed. Installing needs administrator rights."}
              </span>
            </span>
            {state.mode === "live" ? (
              <button className="quiet" onClick={() => onAction(bridge.uninstallLive, "Live monitoring and its history were removed.")}>
                Remove
              </button>
            ) : (
              <button onClick={() => onAction(bridge.installLive, "Live monitoring is installed and running.")}>Install</button>
            )}
          </div>
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

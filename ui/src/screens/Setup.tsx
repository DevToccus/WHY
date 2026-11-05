import { useState } from "react";
import logo from "../assets/logo.png";
import * as bridge from "../bridge";
import { FocusOptions, StartupOptions } from "../components/Options";
import { focusName, hasFocus } from "../focus";
import type { Settings } from "../types";

interface Props {
  initial: Settings;
  elevated: boolean;
  onDone: (settings: Settings) => void;
}

const steps = ["Welcome", "How WHY? runs", "Focus", "All set"];

// The first run: a few choices that make WHY? work better, asked once, as Windows asks during its own setup.
export function SetupScreen({ initial, elevated, onDone }: Props) {
  const [step, setStep] = useState(0);
  const [settings, setSettings] = useState<Settings>(initial);
  const [error, setError] = useState<string | null>(null);
  const [saving, setSaving] = useState(false);

  const focusIncomplete = settings.focusKind !== "" && !settings.focusTarget;
  const restartNeeded = settings.runAsAdmin && !elevated;

  const finish = async () => {
    setSaving(true);
    setError(null);
    const done = { ...settings, setupDone: true };
    const result = await bridge.saveSettings(done);
    setSaving(false);
    if (!result.ok && !result.cancelled) {
      setError(result.error ?? "Windows did not accept these settings.");
      return;
    }
    onDone(done);
    if (restartNeeded) await bridge.restartElevated();
  };

  return (
    <div className="setup">
      <div className="setup-card card">
        <ol className="setup-steps" aria-label="Setup steps">
          {steps.map((label, index) => (
            <li key={label} aria-current={index === step ? "step" : undefined} className={index < step ? "is-done" : undefined}>
              {label}
            </li>
          ))}
        </ol>

        <div className="setup-body" key={step}>
          {step === 0 && (
            <div className="setup-welcome">
              <img src={logo} alt="" width="88" height="88" />
              <h1>Welcome to WHY?</h1>
              <p className="lead">
                WHY? watches Windows and your programs, notices when something goes wrong, and explains what caused it. It only observes: it never changes or
                blocks anything.
              </p>
              <p className="muted">A few choices help it work better. You can change them later in Settings.</p>
            </div>
          )}

          {step === 1 && (
            <>
              <h1>How WHY? runs</h1>
              <p className="muted">To catch problems as they happen, WHY? needs to be running. These make sure it is.</p>
              <StartupOptions value={settings} onChange={(next) => setSettings({ ...settings, ...next })} />
            </>
          )}

          {step === 2 && (
            <>
              <h1>What should WHY? watch?</h1>
              <p className="muted">
                Developing something? Focus on it, and WHY? looks only at that program's crashes and hangs, not at Windows itself.
              </p>
              <FocusOptions value={settings} onChange={(next) => setSettings({ ...settings, ...next })} />
            </>
          )}

          {step === 3 && (
            <>
              <h1>All set</h1>
              <ul className="setup-summary">
                <li>{settings.runAsAdmin ? "Runs as administrator, so it reads everything Windows keeps." : "Runs without administrator rights. Some sources stay limited."}</li>
                <li>{settings.startAtLogin ? "Starts when you sign in." : "Starts only when you open it."}</li>
                <li>{settings.keepInBackground ? "Keeps watching from the notification area after you close the window." : "Stops watching when you close the window."}</li>
                <li>{hasFocus(settings) ? `Focuses on ${focusName(settings)}.` : "Watches the whole PC."}</li>
              </ul>
              {restartNeeded && <p className="muted">WHY? will restart as administrator. Windows asks you to confirm.</p>}
              {settings.runAsAdmin && settings.startAtLogin && (
                <p className="muted">At sign-in WHY? starts without administrator rights, so Windows does not prompt you then. Live monitoring covers everything in the background.</p>
              )}
              {error && <p className="tone-critical">{error}</p>}
            </>
          )}
        </div>

        <div className="setup-actions">
          {step > 0 && (
            <button className="quiet" onClick={() => setStep(step - 1)}>
              Back
            </button>
          )}
          {step < steps.length - 1 ? (
            <button onClick={() => setStep(step + 1)} disabled={step === 2 && focusIncomplete}>
              {step === 0 ? "Get started" : "Next"}
            </button>
          ) : (
            <button onClick={finish} disabled={saving}>
              {saving ? "Saving" : "Finish"}
            </button>
          )}
        </div>
      </div>
    </div>
  );
}

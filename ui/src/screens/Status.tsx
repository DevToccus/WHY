import * as bridge from "../bridge";
import { alertLabels, areaLabels, formatDay, formatMoment, needsLook, plural, relativeDay } from "../format";
import type { Area, Case, State } from "../types";

interface Props {
  state: State;
  onOpenCase: (key: string) => void;
  onShowCases: () => void;
  onAction: (action: () => Promise<bridge.ActionResult>, success?: string) => void;
}

const areas: Area[] = ["hardware", "driver", "software", "system"];

function verdict(attention: number, scanning: boolean): string {
  if (attention > 0) return `${plural(attention, "problem needs", "problems need")} a look.`;
  if (scanning) return "Still reading what Windows has recorded.";
  return "Nothing needs your attention.";
}

function areaLine(cases: Case[]): { text: string; tone: string } {
  const attention = cases.filter(needsLook);
  if (attention.some((item) => item.severity === "critical")) return { text: plural(attention.length, "serious problem"), tone: "critical" };
  if (attention.length > 0) return { text: plural(attention.length, "problem"), tone: "warning" };
  const open = cases.filter((item) => item.status === "open" || item.status === "watching");
  if (open.length > 0) return { text: `${plural(open.length, "minor complaint")}`, tone: "quiet" };
  return { text: "Nothing on record", tone: "ok" };
}

export function StatusScreen({ state, onOpenCase, onShowCases, onAction }: Props) {
  const attention = state.cases.filter(needsLook);
  const limited = state.coverage.filter((source) => source.status !== "ok");
  const isReport = state.mode === "report";

  return (
    <>
      <header className="verdict">
        <h1 className={attention.some((item) => item.severity === "critical") ? "verdict-critical" : undefined}>{verdict(attention.length, state.scanning)}</h1>
        <p>
          {state.machine.os}
          {state.historyStartMs > 0 && `. The logs reach back to ${formatDay(state.historyStartMs)}.`}
          {limited.length > 0 && ` ${plural(limited.length, "source is", "sources are")} limited, so some problems may not show.`}
        </p>
      </header>

      <section className="area-strip" aria-label="Health by area">
        {areas.map((area) => {
          const line = areaLine(state.cases.filter((item) => item.area === area));
          return (
            <div className="area" key={area}>
              <h2>{areaLabels[area]}</h2>
              <p className={`tone-${line.tone}`}>{line.text}</p>
            </div>
          );
        })}
      </section>

      {attention.length > 0 && (
        <section>
          <h2 className="section-title">Needs a look</h2>
          <ul className="rows">
            {attention.map((item) => (
              <li key={item.key}>
                <button className="row" onClick={() => onOpenCase(item.key)}>
                  <span className={`severity-bar severity-${item.severity}`} aria-hidden="true" />
                  <span className="row-main">
                    <strong>{item.title}</strong>
                    <span className="muted">{item.summary}</span>
                  </span>
                  <span className="row-side">
                    <span>{alertLabels[item.alert]}</span>
                    <span className="muted">{relativeDay(item.lastMs, state.generatedMs)}</span>
                  </span>
                </button>
              </li>
            ))}
          </ul>
        </section>
      )}

      {attention.length === 0 && state.cases.length > 0 && (
        <p>
          <button className="link" onClick={onShowCases}>
            See {plural(state.cases.length, "older or minor case")}
          </button>
        </p>
      )}

      <section>
        <h2 className="section-title">What WHY? could read</h2>
        <table className="coverage">
          <tbody>
            {state.coverage.map((source) => (
              <tr key={source.id}>
                <th scope="row">{source.label}</th>
                <td className={source.status === "ok" ? "tone-ok" : "tone-warning"}>{source.status === "ok" ? "Available" : "Limited"}</td>
                <td className="muted">
                  {source.detail}
                  {source.oldestMs > 0 && ` Reaches back to ${formatMoment(source.oldestMs)}.`}
                </td>
              </tr>
            ))}
          </tbody>
        </table>
        {!isReport && !state.elevated && (
          <p className="action-line">
            <button onClick={() => onAction(bridge.restartElevated)}>Restart as administrator</button>
            <span className="muted">Reads crash dumps and the file change journal, and starts the activity recorder.</span>
          </p>
        )}
      </section>

      {!isReport && (
        <section>
          <h2 className="section-title">{state.mode === "live" ? "Live monitoring is on" : "Monitoring ends when you close WHY?"}</h2>
          {state.mode === "live" ? (
            <p className="action-line">
              <button className="quiet" onClick={() => onAction(bridge.uninstallLive, "Live monitoring and its history were removed.")}>
                Remove Live monitoring
              </button>
              <span className="muted">Runs in the background and notifies you when something goes wrong. It only observes.</span>
            </p>
          ) : (
            <p className="action-line">
              <button className="quiet" onClick={() => onAction(bridge.installLive, "Live monitoring is installed and running.")}>
                Install Live monitoring
              </button>
              <span className="muted">
                Nothing is saved on this PC right now. Installing adds a background service that keeps history and notifies you. Needs administrator rights.
              </span>
            </p>
          )}
        </section>
      )}
    </>
  );
}

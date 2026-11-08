import { CaseRow } from "../components/CaseRow";
import { useState } from "react";
import { AreaDonut, periods, ProblemsTrend, type Period } from "../components/Charts";
import { Icon } from "../components/Icon";
import { formatDay, needsLook, plural } from "../format";
import type { State } from "../types";

interface Props {
  state: State;
  onOpenCase: (key: string) => void;
  onShowCases: () => void;
  onShowSettings: () => void;
}

// Home shows the few cases that matter most; the rest wait in Cases.
const ROWS_SHOWN = 4;

function verdict(attention: number, serious: number, scanning: boolean): string {
  if (serious > 0 && serious === attention) return `${plural(serious, "serious problem needs", "serious problems need")} a look.`;
  if (serious > 0) return `${plural(attention, "problem needs", "problems need")} a look, ${serious} of them serious.`;
  if (attention > 0) return `${plural(attention, "problem needs", "problems need")} a look.`;
  if (scanning) return "Still reading what Windows has recorded.";
  return "Nothing needs your attention.";
}

// Per-viewer conveniences: the chosen chart span and a dismissed tip. Storage may be refused; the defaults then apply.
const remembered = (key: string) => {
  try {
    return localStorage.getItem(key);
  } catch {
    return null;
  }
};
const remember = (key: string, value: string) => {
  try {
    localStorage.setItem(key, value);
  } catch {
    // Nothing to keep in a private window.
  }
};

const TIP_COMMAND = "why-run npm run dev";

export function StatusScreen({ state, onOpenCase, onShowCases, onShowSettings }: Props) {
  const attention = state.cases.filter(needsLook);
  const serious = attention.filter((item) => item.severity === "critical").length;
  const available = state.coverage.filter((source) => source.status === "ok").length;
  const tone = serious > 0 ? "critical" : attention.length > 0 ? "warning" : "ok";
  const [period, setPeriod] = useState<Period>(() => {
    const saved = remembered("why-period");
    return periods.some((item) => item.id === saved) ? (saved as Period) : "month";
  });
  const [tipHidden, setTipHidden] = useState(() => remembered("why-hide-dev-tip") === "1");
  const [copied, setCopied] = useState(false);

  const choosePeriod = (next: Period) => {
    setPeriod(next);
    remember("why-period", next);
  };

  return (
    <>
      <h1>Home</h1>

      <header className={`hero card hero-${tone}`}>
        <span className={`hero-badge tone-${tone}`} aria-hidden="true">
          <Icon name={tone === "critical" ? "critical" : tone === "warning" ? "warning" : "ok"} />
        </span>
        <div className="hero-text">
          <h2 className="hero-title">{verdict(attention.length, serious, state.scanning)}</h2>
          <p className="muted">
            {state.machine.os}
            {state.historyStartMs > 0 && `. Logs since ${formatDay(state.historyStartMs)}. `}
            <button className="link" onClick={onShowSettings}>
              {available} of {state.coverage.length} sources readable
            </button>
          </p>
        </div>
        <button className="hero-action" onClick={onShowCases} aria-label={`See all ${plural(state.cases.length, "case")}`}>
          See <Icon name="chevron" />
        </button>
      </header>

      {attention.length > 0 && (
        <section>
          <h2 className="section-title">Needs a look</h2>
          <ul className="rows">
            {attention.slice(0, ROWS_SHOWN).map((item) => (
              <li key={item.key}>
                <CaseRow item={item} now={state.generatedMs} onOpen={onOpenCase} />
              </li>
            ))}
          </ul>
          {attention.length > ROWS_SHOWN && (
            <p>
              <button className="link" onClick={onShowCases}>
                See all {attention.length} in Cases
              </button>
            </p>
          )}
        </section>
      )}

      {attention.length === 0 && state.cases.length > 0 && (
        <p>
          <button className="link" onClick={onShowCases}>
            See {plural(state.cases.length, "older or minor case")}
          </button>
        </p>
      )}

      {!tipHidden && !state.cases.some((item) => item.area === "development") && (
        <section className="banner dev-tip" aria-label="Tip">
          <Icon name="info" className="banner-icon" />
          <span className="dev-tip-text">
            <strong>Building something?</strong> WHY? notices when a dev server or command stops with an error. Start it through why-run to also keep the
            error it prints.
          </span>
          <code className="dev-tip-command">{TIP_COMMAND}</code>
          <button
            className="quiet"
            onClick={() => {
              navigator.clipboard?.writeText(TIP_COMMAND);
              setCopied(true);
            }}
          >
            {copied ? "Copied" : "Copy"}
          </button>
          <button
            className="subtle icon-button"
            aria-label="Hide this tip"
            onClick={() => {
              setTipHidden(true);
              remember("why-hide-dev-tip", "1");
            }}
          >
            <Icon name="dismiss" />
          </button>
        </section>
      )}

      <section>
        <div className="overview-head">
          <h2 className="section-title">Overview</h2>
          <div className="segmented" role="radiogroup" aria-label="Period">
            {periods.map((item) => (
              <button key={item.id} role="radio" aria-checked={period === item.id} onClick={() => choosePeriod(item.id)}>
                {item.label}
              </button>
            ))}
          </div>
        </div>
        <div className="chart-grid">
          <div className="card chart-card">
            <h2 className="card-title">Problems over time</h2>
            <ProblemsTrend state={state} period={period} />
          </div>
          <div className="card chart-card">
            <h2 className="card-title">By area</h2>
            <AreaDonut state={state} period={period} />
          </div>
        </div>
      </section>
    </>
  );
}

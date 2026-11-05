import { useMemo } from "react";
import * as bridge from "../bridge";
import { CertaintyMark } from "../components/CertaintyMark";
import { CaseIcon, CaseRow, useCaseMenu } from "../components/CaseRow";
import { copyText, useContextMenu } from "../components/ContextMenu";
import { Icon } from "../components/Icon";
import { changeKindLabels, changeVersion, formatDay, formatMoment, isFixed, plural, relativeDay, signalLabels, sourceLabels, statusLabels } from "../format";
import type { Case, Certainty, Change, State, WhyEvent } from "../types";

export function CaseList({ state, onOpenCase }: { state: State; onOpenCase: (key: string) => void }) {
  if (state.cases.length === 0) {
    return (
      <>
        <h1>Cases</h1>
        <p className="muted">{state.scanning ? "Still reading the logs." : "Windows has recorded no problems that WHY? recognises."}</p>
      </>
    );
  }
  return (
    <>
      <h1>Cases</h1>
      <p className="muted">Everything that went wrong, grouped by what it has in common. The ones that need a look come first.</p>
      <ul className="rows">
        {state.cases
          .filter((item) => !isFixed(item))
          .map((item) => (
            <li key={item.key}>
              <CaseRow item={item} now={state.generatedMs} onOpen={onOpenCase} />
            </li>
          ))}
      </ul>
      {state.cases.some(isFixed) && (
        <section>
          <h2 className="section-title">
            Marked as fixed <span className="muted section-note">WHY? keeps watching and brings them back if they happen again.</span>
          </h2>
          <ul className="rows">
            {state.cases.filter(isFixed).map((item) => (
              <li key={item.key}>
                <CaseRow item={item} now={state.generatedMs} onOpen={onOpenCase} />
              </li>
            ))}
          </ul>
        </section>
      )}
    </>
  );
}

const findingGroups: { certainty: Certainty; title: string }[] = [
  { certainty: "verified", title: "What the logs prove" },
  { certainty: "probable", title: "The likely explanation" },
  { certainty: "correlated", title: "Happened around the same time, or could be related" },
];

type SpineEntry = { side: "change"; timeMs: number; change: Change } | { side: "problem"; timeMs: number; event: WhyEvent };

const MAX_PROBLEMS_SHOWN = 12;

// What changed on the left, what went wrong on the right, on one line of time. The order is the argument.
function Spine({ item }: { item: Case }) {
  const { entries, hiddenProblems } = useMemo(() => {
    const problems = [...item.chain].sort((a, b) => a.timeMs - b.timeMs);
    const hidden = Math.max(0, problems.length - MAX_PROBLEMS_SHOWN);
    const shown = problems.length > MAX_PROBLEMS_SHOWN ? [problems[0], ...problems.slice(-(MAX_PROBLEMS_SHOWN - 1))] : problems;
    const merged: SpineEntry[] = [
      ...item.changesBefore.map((change): SpineEntry => ({ side: "change", timeMs: change.timeMs, change })),
      ...shown.map((event): SpineEntry => ({ side: "problem", timeMs: event.timeMs, event })),
    ];
    merged.sort((a, b) => a.timeMs - b.timeMs);
    return { entries: merged, hiddenProblems: hidden };
  }, [item]);

  return (
    <div className="spine">
      <div className="spine-heads" aria-hidden="true">
        <span>What changed</span>
        <span />
        <span>What went wrong</span>
      </div>
      <ol>
        {entries.map((entry, index) => (
          <li key={index} className={`spine-row spine-${entry.side}`}>
            {entry.side === "change" ? (
              <>
                <div className="spine-cell spine-left">
                  <strong>{entry.change.name}</strong>
                  <span className="muted">
                    {changeKindLabels[entry.change.kind] ?? entry.change.kind}
                    {changeVersion(entry.change) && `, ${changeVersion(entry.change)}`}
                  </span>
                  <time className="muted">
                    {entry.change.estimated && "around "}
                    {formatDay(entry.change.timeMs)}
                  </time>
                </div>
                <span className="spine-mark" aria-hidden="true" />
                <div />
              </>
            ) : (
              <>
                <div />
                <span className={`spine-mark severity-${item.severity}`} aria-hidden="true" />
                <div className="spine-cell">
                  <strong>{signalLabels[entry.event.signal] ?? entry.event.signal}</strong>
                  {entry.event.message && <span className="muted spine-message">{entry.event.message}</span>}
                  <time className="muted">{formatMoment(entry.event.timeMs)}</time>
                </div>
              </>
            )}
          </li>
        ))}
      </ol>
      {hiddenProblems > 0 && <p className="muted">{plural(hiddenProblems, "similar record")} between the first and the latest are not shown.</p>}
    </div>
  );
}

interface DetailProps {
  item: Case;
  isReport: boolean;
  now: number;
  onBack: () => void;
  onAction: (action: () => Promise<bridge.ActionResult>, success?: string) => void;
}

export function CaseDetail({ item, isReport, now, onBack, onAction }: DetailProps) {
  const openMenu = useContextMenu();
  const menu = useCaseMenu(item);
  return (
    <article className="case">
      <nav className="breadcrumb" aria-label="Breadcrumb">
        <button className="breadcrumb-link" onClick={onBack}>
          Cases
        </button>
        <Icon name="chevron" />
      </nav>
      <div className="case-title" onContextMenu={(event) => openMenu(event, menu)}>
        <CaseIcon item={item} size={36} />
        <h1>{item.title}</h1>
      </div>
      <p className="lead">{item.summary}</p>
      <p className="muted">
        {plural(item.incidents, "time")} since {formatDay(item.firstMs)}, last seen {relativeDay(item.lastMs, now)}. {statusLabels[item.status]}.
        {item.longRunning && " This has been going on for a while."}
      </p>

      {!item.causeSupported && (
        <p className="insufficient">
          <CertaintyMark certainty="correlated" />
          <span>The evidence is not enough to name a cause. What follows is what WHY? can actually support.</span>
        </p>
      )}

      {findingGroups.map((group) => {
        const findings = item.findings.filter((finding) => finding.certainty === group.certainty);
        if (findings.length === 0) return null;
        return (
          <section key={group.certainty}>
            <h2 className="section-title">
              <CertaintyMark certainty={group.certainty} /> {group.title}
            </h2>
            <ul className="findings">
              {findings.map((finding) => (
                <li key={finding.text}>{finding.text}</li>
              ))}
            </ul>
          </section>
        );
      })}

      {item.output && (
        <section>
          <h2 className="section-title">
            Terminal output
            <button className="subtle copy" onClick={() => navigator.clipboard?.writeText(item.output ?? "")}>
              Copy
            </button>
          </h2>
          <pre
            className="output"
            onContextMenu={(event) => {
              const selected = window.getSelection()?.toString() ?? "";
              openMenu(event, [
                ...(selected ? [{ label: "Copy selection", icon: "copy" as const, onSelect: () => copyText(selected) }] : []),
                { label: "Copy all", icon: "copy", onSelect: () => copyText(item.output ?? "") },
              ]);
            }}
          >
            {item.output}
          </pre>
        </section>
      )}

      <section>
        <h2 className="section-title">What to try</h2>
        <ul className="advice">
          {item.advice.map((advice) => (
            <li key={advice.text}>
              <span>{advice.text}</span>
              {advice.tool && !isReport && (
                <button className="quiet" onClick={() => onAction(() => bridge.openTool(advice.tool))}>
                  {toolLabels[advice.tool] ?? "Open"}
                </button>
              )}
            </li>
          ))}
        </ul>
        {!isReport && (
          <p className="action-line">
            {item.status === "watching" || item.status === "resolved" ? (
              <button className="quiet" onClick={() => onAction(() => bridge.caseAction(item.key, "reopen"))}>
                Reopen
              </button>
            ) : (
              <button onClick={() => onAction(() => bridge.caseAction(item.key, "tried_fix"), "Marked as fixed. It leaves Needs a look, and WHY? tells you if it comes back.")}>Mark as fixed</button>
            )}
            <span className="muted">WHY? changes nothing on your PC. It only watches whether this happens again.</span>
          </p>
        )}
      </section>

      <section>
        <h2 className="section-title">How it unfolded</h2>
        <Spine item={item} />
      </section>

      {item.activity.length > 0 && (
        <section>
          <details className="activity">
            <summary>What was running just before ({item.activity.length})</summary>
            <ul>
              {item.activity.map((record, index) => (
                <li key={index}>
                  <time className="muted">{formatMoment(record.timeMs)}</time>
                  <span>{activityLabels[record.kind] ?? record.kind}</span>
                  <code>{record.image}</code>
                </li>
              ))}
            </ul>
          </details>
        </section>
      )}

      <p className="muted evidence-note">
        Changes are dated from {Array.from(new Set(item.changesBefore.map((change) => sourceLabels[change.source] ?? change.source))).join(", ") || "no source, because none were found"}.
      </p>
    </article>
  );
}

const toolLabels: Record<string, string> = {
  device_manager: "Open Device Manager",
  reliability_monitor: "Open Reliability Monitor",
  memory_diagnostic: "Open Memory Diagnostic",
  services: "Open Services",
  windows_security: "Open Windows Security",
};

const activityLabels: Record<string, string> = {
  process_start: "Started",
  process_stop: "Stopped",
  driver_load: "Driver loaded",
};

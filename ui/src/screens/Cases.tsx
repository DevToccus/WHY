import { useMemo, useState } from "react";
import * as bridge from "../bridge";
import { CertaintyMark } from "../components/CertaintyMark";
import { CaseIcon, CaseRow, StatusIcon, useCaseMenu, type StatusTone } from "../components/CaseRow";
import { copyText, useContextMenu } from "../components/ContextMenu";
import { Icon } from "../components/Icon";
import {
  changeKindLabels,
  changeVersion,
  formatDay,
  formatMoment,
  formatTime,
  isFixed,
  needsLook,
  plural,
  relativeDay,
  severityLabels,
  signalLabels,
  sourceLabels,
  statusLabels,
} from "../format";
import type { Area, Case, Certainty, Change, State, WhyEvent } from "../types";

// ---- The list ----

type Filter = "all" | "attention" | "pc" | "apps" | "development";

const filterAreas: Record<Exclude<Filter, "all" | "attention">, Area[]> = {
  pc: ["hardware", "driver", "system", "security"],
  apps: ["software"],
  development: ["development"],
};

const filterLabels: Record<Filter, string> = {
  all: "All",
  attention: "Needs a look",
  pc: "Windows and hardware",
  apps: "Programs",
  development: "Development",
};

const matches = (item: Case, filter: Filter) =>
  filter === "all" ? true : filter === "attention" ? needsLook(item) : filterAreas[filter].includes(item.area);

function CaseGroup({ title, note, cases, now, onOpenCase }: { title: string; note?: string; cases: Case[]; now: number; onOpenCase: (key: string) => void }) {
  if (cases.length === 0) return null;
  return (
    <section className="case-group">
      <h2 className="section-title">
        {title} <span className="group-count">{cases.length}</span>
        {note && <span className="muted section-note">{note}</span>}
      </h2>
      <ul className="rows">
        {cases.map((item) => (
          <li key={item.key}>
            <CaseRow item={item} now={now} onOpen={onOpenCase} />
          </li>
        ))}
      </ul>
    </section>
  );
}

export function CaseList({ state, onOpenCase }: { state: State; onOpenCase: (key: string) => void }) {
  const [filter, setFilter] = useState<Filter>("all");

  if (state.cases.length === 0) {
    return (
      <>
        <h1>Cases</h1>
        <p className="muted">{state.scanning ? "Still reading the logs." : "Windows has recorded no problems that WHY? recognises."}</p>
      </>
    );
  }

  // Only the filters that would show something, so none of them leads to an empty page.
  const filters = (Object.keys(filterLabels) as Filter[]).filter((id) => id === "all" || state.cases.some((item) => matches(item, id)));
  const shown = state.cases.filter((item) => matches(item, filter));
  const attention = shown.filter((item) => needsLook(item) && !isFixed(item));
  const earlier = shown.filter((item) => !needsLook(item) && !isFixed(item));
  const fixed = shown.filter(isFixed);

  return (
    <>
      <div className="page-head">
        <h1>Cases</h1>
        <div className="segmented" role="radiogroup" aria-label="Show">
          {filters.map((id) => (
            <button key={id} role="radio" aria-checked={filter === id} onClick={() => setFilter(id)}>
              {filterLabels[id]}
            </button>
          ))}
        </div>
      </div>
      <p className="muted severity-key">
        <span className="key-swatch key-critical" /> Serious
        <span className="key-swatch key-warning" /> Warning
        <span className="key-swatch key-notice" /> Minor
      </p>
      <CaseGroup title="Needs a look" cases={attention} now={state.generatedMs} onOpenCase={onOpenCase} />
      <CaseGroup title="Earlier and minor" cases={earlier} now={state.generatedMs} onOpenCase={onOpenCase} />
      <CaseGroup
        title="Marked as fixed"
        note="WHY? keeps watching and brings them back if they happen again."
        cases={fixed}
        now={state.generatedMs}
        onOpenCase={onOpenCase}
      />
    </>
  );
}

// ---- One case ----

const findingGroups: { certainty: Certainty; title: string }[] = [
  { certainty: "verified", title: "Proven by the logs" },
  { certainty: "probable", title: "Likely explanation" },
  { certainty: "correlated", title: "Around the same time, not proven" },
];

// Records closer than this belong to one moment: a blue screen and its restart, a crash and its report.
const SAME_MOMENT_MS = 5 * 60_000;
// Moments shown before the middle of a long history folds away.
const FIRST_SHOWN = 2;
const LATEST_SHOWN = 4;

interface Moment {
  kind: "problem";
  timeMs: number;
  events: WhyEvent[];
  headline: string;
  detail: string;
  sameAsBefore: boolean;
}

interface ChangeEntry {
  kind: "change";
  timeMs: number;
  change: Change;
}

type Entry = Moment | ChangeEntry;

// The first sentence of a Windows log message, without the hexadecimal parameters that follow it.
function firstSentence(message: string): string {
  const clean = message.replace(/\s+/g, " ").trim();
  const end = clean.search(/[.!?](\s|$)/);
  const sentence = end > 0 ? clean.slice(0, end + 1) : clean;
  return sentence.length > 180 ? `${sentence.slice(0, 177)}...` : sentence;
}

function momentsOf(item: Case): Moment[] {
  const events = [...item.chain].sort((a, b) => a.timeMs - b.timeMs);
  const groups: WhyEvent[][] = [];
  for (const event of events) {
    const last = groups[groups.length - 1];
    if (last && event.timeMs - last[last.length - 1].timeMs <= SAME_MOMENT_MS) last.push(event);
    else groups.push([event]);
  }
  let previous = "";
  return groups.map((group) => {
    const labels = Array.from(new Set(group.map((event) => signalLabels[event.signal] ?? event.signal)));
    const detail = firstSentence(group.find((event) => event.message)?.message ?? "");
    const moment: Moment = {
      kind: "problem",
      timeMs: group[0].timeMs,
      events: group,
      headline: labels.join(", then "),
      detail,
      sameAsBefore: detail !== "" && detail === previous,
    };
    previous = detail;
    return moment;
  });
}

function gapText(ms: number): string {
  const minutes = Math.round(ms / 60_000);
  if (minutes < 60) return `${plural(minutes, "minute")} later`;
  const hours = Math.round(minutes / 60);
  if (hours < 48) return `${plural(hours, "hour")} later`;
  const days = Math.round(hours / 24);
  if (days < 60) return `${plural(days, "day")} later`;
  return `${plural(Math.round(days / 30), "month")} later`;
}

function changeRelation(change: Change, firstMs: number): string {
  const days = Math.round((firstMs - change.timeMs) / 86_400_000);
  if (change.timeMs > firstMs) return "After the problem had already started, so it did not start it.";
  if (days <= 0) return "The same day the problem first appeared.";
  return `${plural(days, "day")} before the problem first appeared.`;
}

const ordinal = (n: number) => {
  const tens = n % 100;
  const suffix = tens >= 11 && tens <= 13 ? "th" : n % 10 === 1 ? "st" : n % 10 === 2 ? "nd" : n % 10 === 3 ? "rd" : "th";
  return `${n}${suffix}`;
};

// What happened, told in order: a few plain sentences, then each moment and each change on one line of time.
function WhatHappened({ item, now }: { item: Case; now: number }) {
  const [expanded, setExpanded] = useState(false);
  const moments = useMemo(() => momentsOf(item), [item]);
  // The engine keeps only the latest records of a long history, so the first moments may be missing.
  const missing = Math.max(0, item.incidents - moments.length);
  const numbered = missing === 0 && moments.length === item.incidents;

  const entries: Entry[] = useMemo(() => {
    const all: Entry[] = [...moments, ...item.changesBefore.map((change): ChangeEntry => ({ kind: "change", timeMs: change.timeMs, change }))];
    return all.sort((a, b) => a.timeMs - b.timeMs);
  }, [moments, item.changesBefore]);

  const foldable = !expanded && moments.length > FIRST_SHOWN + LATEST_SHOWN + 1;
  const hiddenFrom = foldable ? moments[FIRST_SHOWN].timeMs : Infinity;
  const hiddenTo = foldable ? moments[moments.length - LATEST_SHOWN - 1].timeMs : -Infinity;
  const hiddenCount = foldable ? moments.length - FIRST_SHOWN - LATEST_SHOWN : 0;

  const changesBefore = item.changesBefore.filter((change) => change.timeMs <= item.firstMs).length;
  const story = [
    `It first happened on ${formatMoment(item.firstMs)}.`,
    item.incidents > 1
      ? `It came back ${plural(item.incidents - 1, "more time")}, most recently ${relativeDay(item.lastMs, now)} at ${formatTime(item.lastMs)}.`
      : "It has not happened again since.",
    item.incidentsLast7Days > 0 && item.incidents > 1 ? `${plural(item.incidentsLast7Days, "time")} in the last 7 days.` : "",
    changesBefore > 0
      ? `${plural(changesBefore, "change")} to drivers or Windows came before it began, shown below in order.`
      : "WHY? found no driver or Windows update change in the weeks before it began.",
  ].filter(Boolean);

  let occurrence = missing;
  let previousMs: number | null = null;
  let foldShown = false;

  return (
    <div className="story">
      <p className="story-summary">{story.join(" ")}</p>
      <ol className="story-line">
        {entries.map((entry, index) => {
          if (entry.kind === "problem") occurrence += 1;
          if (entry.kind === "problem" && entry.timeMs >= hiddenFrom && entry.timeMs <= hiddenTo) {
            if (foldShown) return null;
            foldShown = true;
            previousMs = null;
            return (
              <li key={`fold-${index}`} className="story-fold">
                <button className="subtle" onClick={() => setExpanded(true)}>
                  <Icon name="chevron" className="fold-icon" /> Show {plural(hiddenCount, "more occurrence")}
                </button>
              </li>
            );
          }
          const gap = previousMs !== null && entry.timeMs - previousMs >= 3_600_000 ? gapText(entry.timeMs - previousMs) : "";
          previousMs = entry.timeMs;
          return (
            <li key={index} className={`story-entry story-${entry.kind}`}>
              {gap && <span className="story-gap">{gap}</span>}
              <span className={`story-mark ${entry.kind === "problem" ? `mark-${item.severity}` : "mark-change"}`} aria-hidden="true">
                {entry.kind === "change" && <Icon name="changes" />}
              </span>
              {entry.kind === "problem" ? (
                <div className="story-body">
                  <div className="story-head">
                    <strong>{entry.headline}</strong>
                    {numbered && item.incidents > 1 && <span className="story-tag">{ordinal(occurrence)} time</span>}
                    <time className="muted">{formatMoment(entry.timeMs)}</time>
                  </div>
                  {entry.detail && <p className="story-detail">{entry.sameAsBefore ? "Same as the previous time." : entry.detail}</p>}
                  <details className="story-raw">
                    <summary>{entry.events.length === 1 ? "Windows log entry" : `${entry.events.length} Windows log entries`}</summary>
                    {entry.events.map((event, at) => (
                      <div key={at} className="raw-record">
                        <span className="muted">
                          {formatMoment(event.timeMs)} · {event.provider} · event {event.eventId}
                        </span>
                        <span>{event.message || "No message text."}</span>
                      </div>
                    ))}
                  </details>
                </div>
              ) : (
                <div className="story-body">
                  <div className="story-head">
                    <strong>
                      {changeKindLabels[entry.change.kind] ?? entry.change.kind}: {entry.change.name}
                    </strong>
                    <time className="muted">
                      {entry.change.estimated && "around "}
                      {formatDay(entry.change.timeMs)}
                    </time>
                  </div>
                  <p className="story-detail">
                    {changeVersion(entry.change) && `Version ${changeVersion(entry.change)}. `}
                    {changeRelation(entry.change, item.firstMs)}
                    <span className="muted"> Dated from the {sourceLabels[entry.change.source] ?? entry.change.source}.</span>
                  </p>
                </div>
              )}
            </li>
          );
        })}
      </ol>
      {missing > 0 && (
        <p className="muted story-note">
          {plural(missing, "other occurrence")} {missing === 1 ? "is counted, but its record is" : "are counted, but their records are"} no longer kept.
        </p>
      )}
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
  const fixed = isFixed(item);

  return (
    <article className="case">
      <nav className="breadcrumb" aria-label="Breadcrumb">
        <button className="breadcrumb-link" onClick={onBack}>
          Cases
        </button>
        <Icon name="chevron" />
      </nav>

      <header className="case-head">
        <div className="case-title" onContextMenu={(event) => openMenu(event, menu)}>
          <CaseIcon item={item} size={36} />
          <h1>{item.title}</h1>
        </div>
        {!isReport &&
          (fixed ? (
            <button className="quiet" onClick={() => onAction(() => bridge.caseAction(item.key, "reopen"))}>
              Reopen
            </button>
          ) : (
            <button
              onClick={() => onAction(() => bridge.caseAction(item.key, "tried_fix"), "Marked as fixed. WHY? tells you if it comes back.")}
            >
              Mark as fixed
            </button>
          ))}
      </header>
      <p className="lead">{item.summary}</p>
      {item.status === "recurred" && (item.fixedMs ?? 0) > 0 && (
        <p className="recurred-note">
          You marked this as fixed on {formatMoment(item.fixedMs!)}. It happened again afterwards, most recently {relativeDay(item.lastMs, now)} at{" "}
          {formatTime(item.lastMs)}, so the fix did not hold.
        </p>
      )}

      <dl className="case-facts">
        <div className={`fact fact-${item.severity}`}>
          <dt>Severity</dt>
          <dd>
            <StatusIcon tone={item.severity} className="fact-icon" />
            {severityLabels[item.severity]}
          </dd>
        </div>
        <div className="fact">
          <dt>Happened</dt>
          <dd>{plural(item.incidents, "time")}</dd>
        </div>
        <div className="fact">
          <dt>First</dt>
          <dd>{formatDay(item.firstMs)}</dd>
        </div>
        <div className="fact">
          <dt>Latest</dt>
          <dd>
            {relativeDay(item.lastMs, now)}, {formatTime(item.lastMs)}
          </dd>
        </div>
        <div className="fact">
          <dt>Status</dt>
          <dd>{statusLabels[item.status]}</dd>
        </div>
      </dl>

      <section>
        <h2 className="section-title">What happened</h2>
        <WhatHappened item={item} now={now} />
      </section>

      <section>
        <h2 className="section-title">What WHY? found</h2>
        {!item.causeSupported && (
          <p className="insufficient">
            <CertaintyMark certainty="correlated" />
            <span>Not enough evidence to name a cause. Only what the logs support is listed.</span>
          </p>
        )}
        {findingGroups.map((group) => {
          const findings = item.findings.filter((finding) => finding.certainty === group.certainty);
          if (findings.length === 0) return null;
          return (
            <div key={group.certainty} className="finding-group">
              <h3 className="finding-title">
                <CertaintyMark certainty={group.certainty} /> {group.title}
              </h3>
              <ul className="findings">
                {findings.map((finding) => (
                  <li key={finding.text}>{finding.text}</li>
                ))}
              </ul>
            </div>
          );
        })}
      </section>

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

      {(item.advice.length > 0 || item.origin) && (
        <section>
          <h2 className="section-title">What to try</h2>
          {item.origin && item.originText && (
            <div className={`origin origin-${item.origin}`}>
              <StatusIcon tone={originTones[item.origin]} size={18} className="origin-icon" />
              <span>
                <strong>{originLabels[item.origin]}</strong>
                <span className="muted">{item.originText}</span>
              </span>
            </div>
          )}
          <ol className="advice">
            {item.advice.map((advice, index) => (
              <li key={advice.text}>
                <span className="advice-step">{index + 1}</span>
                <span className="advice-text">{advice.text}</span>
                {advice.tool && !isReport && (
                  <button className="quiet" onClick={() => onAction(() => bridge.openTool(advice.tool))}>
                    {toolLabels[advice.tool] ?? "Open"}
                  </button>
                )}
              </li>
            ))}
          </ol>
        </section>
      )}

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
    </article>
  );
}

const originLabels: Record<string, string> = {
  error: "A real error",
  forced: "A forced closure, not an error",
  unclear: "Not certain: a real error or a forced closure",
};

const originTones: Record<string, StatusTone> = {
  error: "critical",
  forced: "notice",
  unclear: "unclear",
};

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

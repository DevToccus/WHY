import { useMemo, useState } from "react";
import { copyText, useContextMenu } from "../components/ContextMenu";
import { formatDay, formatTime, roleOf, signalLabels } from "../format";
import type { State, WhyEvent } from "../types";

export function TimelineScreen({ state }: { state: State }) {
  const openMenu = useContextMenu();
  const [query, setQuery] = useState("");
  const [kind, setKind] = useState<"all" | "problem" | "change">("all");

  const days = useMemo(() => {
    const needle = query.trim().toLowerCase();
    const groups = new Map<string, WhyEvent[]>();
    for (const event of state.timeline) {
      if (kind !== "all" && roleOf(event.signal) !== kind) continue;
      const label = signalLabels[event.signal] ?? event.signal;
      if (needle && !`${label} ${event.subject} ${event.message} ${event.provider}`.toLowerCase().includes(needle)) continue;
      const day = formatDay(event.timeMs);
      const group = groups.get(day);
      if (group) group.push(event);
      else groups.set(day, [event]);
    }
    return [...groups.entries()];
  }, [state.timeline, query, kind]);

  return (
    <>
      <h1>Timeline</h1>
      <p className="muted">Only the records WHY? treats as meaningful. Routine Windows messages are filtered out.</p>
      <div className="filters">
        <input type="search" placeholder="Search" aria-label="Search the timeline" value={query} onChange={(event) => setQuery(event.target.value)} />
        <select aria-label="Show" value={kind} onChange={(event) => setKind(event.target.value as typeof kind)}>
          <option value="all">Everything</option>
          <option value="problem">Problems</option>
          <option value="change">Changes</option>
        </select>
      </div>
      {days.length === 0 && <p className="muted">{state.timeline.length === 0 ? "Nothing has been recorded." : "Nothing matches this search."}</p>}
      {days.map(([day, events]) => (
        <section className="day" key={day}>
          <h2 className="section-title">{day}</h2>
          <ul className="plain-rows">
            {events.map((event, index) => (
              <li
                key={`${event.timeMs}-${index}`}
                onContextMenu={(menuEvent) =>
                  openMenu(menuEvent, [
                    {
                      label: "Copy line",
                      icon: "copy",
                      onSelect: () => copyText(`${day} ${formatTime(event.timeMs)} ${signalLabels[event.signal] ?? event.signal}${event.subject ? `: ${event.subject}` : ""}`),
                    },
                    ...(event.message ? [{ label: "Copy message", icon: "copy" as const, onSelect: () => copyText(event.message) }] : []),
                    { label: "Copy source and event ID", icon: "copy", onSelect: () => copyText(`${event.provider} ${event.eventId}`) },
                  ])
                }
              >
                <time className="muted">{formatTime(event.timeMs)}</time>
                <span className="plain-main">
                  <span>
                    {signalLabels[event.signal] ?? event.signal}
                    {event.subject && <span className="muted">: {event.subject}</span>}
                  </span>
                  {event.message && <span className="muted plain-message">{event.message}</span>}
                </span>
                <code className="muted" title={event.provider}>
                  {event.eventId}
                </code>
              </li>
            ))}
          </ul>
        </section>
      ))}
    </>
  );
}

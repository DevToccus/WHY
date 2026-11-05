import { useMemo, useState } from "react";
import { areaLabels, plural, roleOf, signalSeverity } from "../format";
import type { Area, State } from "../types";

const HOUR = 3_600_000;
const DAY = 24 * HOUR;
const areas: Area[] = ["development", "hardware", "driver", "software", "system", "security"];

// The spans the Home charts can show. Each one cuts time into a number of slots the dial can still draw:
// hours for a day, days for a month, weeks for three and six months, calendar months for a year.
export type Period = "day" | "month" | "quarter" | "half" | "year";

export const periods: { id: Period; label: string; caption: string }[] = [
  { id: "day", label: "Day", caption: "last 24 hours" },
  { id: "month", label: "Month", caption: "last 30 days" },
  { id: "quarter", label: "3 months", caption: "last 3 months" },
  { id: "half", label: "6 months", caption: "last 6 months" },
  { id: "year", label: "Year", caption: "last 12 months" },
];

interface Slot {
  startMs: number;
  endMs: number;
  label: string;
}

const hourFormat = new Intl.DateTimeFormat(undefined, { hour: "2-digit", minute: "2-digit" });
const dayFormat = new Intl.DateTimeFormat(undefined, { day: "numeric", month: "short" });
const monthFormat = new Intl.DateTimeFormat(undefined, { month: "long", year: "numeric" });

const startOfDay = (ms: number) => {
  const date = new Date(ms);
  date.setHours(0, 0, 0, 0);
  return date.getTime();
};

export function slotsFor(period: Period, now: number): Slot[] {
  if (period === "day") {
    const hour = new Date(now);
    hour.setMinutes(0, 0, 0);
    const first = hour.getTime() - 23 * HOUR;
    return Array.from({ length: 24 }, (_, index) => {
      const startMs = first + index * HOUR;
      return { startMs, endMs: startMs + HOUR, label: hourFormat.format(startMs) };
    });
  }
  if (period === "year") {
    const today = new Date(now);
    return Array.from({ length: 12 }, (_, index) => {
      const start = new Date(today.getFullYear(), today.getMonth() - 11 + index, 1);
      const end = new Date(today.getFullYear(), today.getMonth() - 10 + index, 1);
      return { startMs: start.getTime(), endMs: end.getTime(), label: monthFormat.format(start) };
    });
  }
  const days = period === "month" ? 1 : 7;
  const count = period === "month" ? 30 : period === "quarter" ? 13 : 26;
  const first = startOfDay(now) + DAY - count * days * DAY;
  return Array.from({ length: count }, (_, index) => {
    const startMs = first + index * days * DAY;
    const endMs = startMs + days * DAY;
    return { startMs, endMs, label: days === 1 ? dayFormat.format(startMs) : `${dayFormat.format(startMs)} to ${dayFormat.format(endMs - DAY)}` };
  });
}

// A smooth path through the points that never overshoots between them (monotone cubic), so counts never dip below zero.
function smoothPath(points: { x: number; y: number }[]) {
  if (points.length < 2) return points.length ? `M${points[0].x},${points[0].y}` : "";
  const slopes = points.slice(1).map((point, index) => (point.y - points[index].y) / (point.x - points[index].x));
  const tangents = points.map((_, index) => {
    if (index === 0) return slopes[0];
    if (index === points.length - 1) return slopes[slopes.length - 1];
    const before = slopes[index - 1];
    const after = slopes[index];
    return before * after <= 0 ? 0 : (before + after) / 2;
  });
  let path = `M${points[0].x},${points[0].y}`;
  for (let index = 1; index < points.length; index++) {
    const from = points[index - 1];
    const to = points[index];
    const third = (to.x - from.x) / 3;
    path += ` C${from.x + third},${from.y + tangents[index - 1] * third} ${to.x - third},${to.y - tangents[index] * third} ${to.x},${to.y}`;
  }
  return path;
}

// The chosen span as two stacked areas: serious records at the base, the rest above them.
// One point per slot; hovering shows the slot's counts on a crosshair.
export function ProblemsTrend({ state, period }: { state: State; period: Period }) {
  const [hovered, setHovered] = useState<number | null>(null);

  const slots = useMemo(() => {
    const list = slotsFor(period, state.generatedMs).map((slot) => ({ ...slot, serious: 0, other: 0 }));
    const first = list[0].startMs;
    for (const event of state.timeline) {
      if (roleOf(event.signal) !== "problem" || event.timeMs < first) continue;
      const slot = list.find((item) => event.timeMs >= item.startMs && event.timeMs < item.endMs);
      if (!slot) continue;
      if (signalSeverity[event.signal] === "critical") slot.serious += 1;
      else slot.other += 1;
    }
    return list;
  }, [state.timeline, state.generatedMs, period]);

  const serious = slots.reduce((sum, slot) => sum + slot.serious, 0);
  const total = slots.reduce((sum, slot) => sum + slot.serious + slot.other, 0);
  const peak = Math.max(...slots.map((slot) => slot.serious + slot.other), 1);
  // A round top for the scale, so the gridlines read as real numbers.
  const top = peak <= 4 ? 4 : peak <= 10 ? Math.ceil(peak / 2) * 2 : Math.ceil(peak / 5) * 5;
  const caption = periods.find((item) => item.id === period)!.caption;

  const width = 600;
  const height = 200;
  const x = (index: number) => (slots.length === 1 ? width / 2 : (index / (slots.length - 1)) * width);
  const y = (value: number) => height - (value / top) * (height - 8);

  const seriousLine = slots.map((slot, index) => ({ x: x(index), y: y(slot.serious) }));
  const totalLine = slots.map((slot, index) => ({ x: x(index), y: y(slot.serious + slot.other) }));
  const seriousPath = smoothPath(seriousLine);
  const totalPath = smoothPath(totalLine);
  // The upper area is the band between the total and the serious line.
  const reversedSerious = smoothPath([...seriousLine].reverse()).replace(/^M/, "L");
  const current = hovered === null ? null : slots[hovered];

  const onMove = (event: React.MouseEvent<HTMLDivElement>) => {
    const box = event.currentTarget.getBoundingClientRect();
    const ratio = (event.clientX - box.left) / box.width;
    setHovered(Math.max(0, Math.min(slots.length - 1, Math.round(ratio * (slots.length - 1)))));
  };

  return (
    <div className="trend">
      <div className="trend-figure">
        <span className="trend-number">{total}</span>
        <span className="muted">
          {total === 1 ? "problem record" : "problem records"} in the {caption}
          {serious > 0 && (
            <>
              , <span className="tone-critical">{serious} serious</span>
            </>
          )}
        </span>
      </div>
      <div className="trend-plot" onMouseMove={onMove} onMouseLeave={() => setHovered(null)}>
        <svg viewBox={`0 0 ${width} ${height}`} preserveAspectRatio="none" role="img" aria-label={`${plural(total, "problem record")} in the ${caption}, ${serious} of them serious.`}>
          <defs>
            <linearGradient id="trend-other" x1="0" y1="0" x2="0" y2="1">
              <stop offset="0" className="trend-stop-other" stopOpacity="0.55" />
              <stop offset="1" className="trend-stop-other" stopOpacity="0.08" />
            </linearGradient>
            <linearGradient id="trend-serious" x1="0" y1="0" x2="0" y2="1">
              <stop offset="0" className="trend-stop-serious" stopOpacity="0.7" />
              <stop offset="1" className="trend-stop-serious" stopOpacity="0.15" />
            </linearGradient>
          </defs>
          {[0.25, 0.5, 0.75, 1].map((step) => (
            <line key={step} className="trend-grid" x1="0" x2={width} y1={y(top * step)} y2={y(top * step)} />
          ))}
          <path className="trend-area-other" d={`${totalPath} ${reversedSerious} Z`} />
          <path className="trend-area-serious" d={`${seriousPath} L${width},${height} L0,${height} Z`} />
          <path className="trend-line trend-line-other" d={totalPath} />
          <path className="trend-line trend-line-serious" d={seriousPath} />
          {current && <line className="trend-cursor" x1={x(hovered!)} x2={x(hovered!)} y1="0" y2={height} />}
        </svg>
        <span className="trend-scale muted">{top}</span>
        {current && (
          <>
            <span className="trend-dot dot-other" style={{ left: `${(x(hovered!) / width) * 100}%`, top: `${(y(current.serious + current.other) / height) * 100}%` }} />
            <span className="trend-dot dot-serious" style={{ left: `${(x(hovered!) / width) * 100}%`, top: `${(y(current.serious) / height) * 100}%` }} />
            <div className={`trend-tip${hovered! > slots.length / 2 ? " is-left" : ""}`} style={{ left: `${(x(hovered!) / width) * 100}%` }} role="status">
              <strong>{current.label}</strong>
              <span>
                <span className="swatch swatch-serious" aria-hidden="true" /> {current.serious} serious
              </span>
              <span>
                <span className="swatch swatch-other" aria-hidden="true" /> {current.other} other
              </span>
            </div>
          </>
        )}
      </div>
      <div className="trend-axis muted" aria-hidden="true">
        <span>{slots[0].label}</span>
        <span>{slots[Math.floor(slots.length / 2)].label}</span>
        <span>Now</span>
      </div>
      <ul className="chart-legend">
        <li>
          <span className="swatch swatch-serious" aria-hidden="true" />
          Serious
        </li>
        <li>
          <span className="swatch swatch-other" aria-hidden="true" />
          Other
        </li>
      </ul>
    </div>
  );
}

// Problem records of the chosen span by area, as a ring. Each area keeps its colour; the legend names and counts them.
export function AreaDonut({ state, period }: { state: State; period: Period }) {
  const [hovered, setHovered] = useState<Area | null>(null);

  const rows = useMemo(() => {
    const since = slotsFor(period, state.generatedMs)[0].startMs;
    const areaOf = new Map(state.cases.map((item) => [item.signal, item.area]));
    const counts = new Map<Area, number>(areas.map((area) => [area, 0]));
    for (const event of state.timeline) {
      if (roleOf(event.signal) !== "problem" || event.timeMs < since) continue;
      const area = areaOf.get(event.signal);
      if (area) counts.set(area, (counts.get(area) ?? 0) + 1);
    }
    return areas.map((area) => ({ area, count: counts.get(area) ?? 0 }));
  }, [state.timeline, state.cases, state.generatedMs, period]);

  const total = rows.reduce((sum, row) => sum + row.count, 0);
  const size = 200;
  const centre = size / 2;
  const radius = 72;
  const circumference = 2 * Math.PI * radius;
  // A small gap between segments keeps neighbouring colours apart.
  const gap = rows.filter((row) => row.count > 0).length > 1 ? 4 : 0;
  const current = rows.find((row) => row.area === hovered);

  let offset = 0;
  const segments = rows.map((row) => {
    const length = total > 0 ? (row.count / total) * circumference : 0;
    const start = offset;
    offset += length;
    return { ...row, length, start };
  });

  return (
    <div className="donut">
      <svg viewBox={`0 0 ${size} ${size}`} role="img" aria-label={rows.map((row) => `${areaLabels[row.area]} ${row.count}`).join(", ")}>
        <circle className="donut-track" cx={centre} cy={centre} r={radius} />
        <circle className="dial-ring" cx={centre} cy={centre} r={radius - 22} />
        {segments.map(
          (segment) =>
            segment.count > 0 && (
              <circle
                key={segment.area}
                className={`donut-segment area-${segment.area}${hovered && hovered !== segment.area ? " is-dimmed" : ""}`}
                cx={centre}
                cy={centre}
                r={radius}
                strokeDasharray={`${Math.max(1, segment.length - gap)} ${circumference}`}
                strokeDashoffset={-segment.start}
                transform={`rotate(-90 ${centre} ${centre})`}
                onMouseEnter={() => setHovered(segment.area)}
                onMouseLeave={() => setHovered(null)}
              />
            ),
        )}
        <text className="dial-number" x={centre} y={centre + 6} textAnchor="middle">
          {current ? current.count : total}
        </text>
        <text className="dial-caption" x={centre} y={centre + 26} textAnchor="middle">
          {current ? areaLabels[current.area] : "records"}
        </text>
      </svg>
      <ul className="donut-legend">
        {rows.map((row) => (
          <li
            key={row.area}
            className={row.count === 0 ? "is-empty" : undefined}
            onMouseEnter={() => setHovered(row.area)}
            onMouseLeave={() => setHovered(null)}
          >
            <span className={`swatch area-${row.area}`} aria-hidden="true" />
            <span>{areaLabels[row.area]}</span>
            <strong>{row.count}</strong>
          </li>
        ))}
      </ul>
    </div>
  );
}

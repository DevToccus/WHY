import { Fragment, useEffect, useMemo, useState } from "react";
import * as bridge from "../bridge";
import { copyText, useContextMenu, type MenuEntry } from "../components/ContextMenu";
import { formatBytes, formatRate } from "../components/Gauge";
import { Icon } from "../components/Icon";
import { hasFocus, matchesFocus } from "../focus";
import { cachedIcon, requestIcons } from "../icons";
import type { ProcessInfo, Settings, State } from "../types";

interface Props {
  state: State;
  settings: Settings;
  onFocus: (focus: Pick<Settings, "focusKind" | "focusTarget">) => void;
}

const REFRESH_MS = 2000;

type SortKey = "name" | "cpu" | "memory" | "crashes";
type Group = ProcessInfo["group"];

// Development tools first: they are what WHY? watches for crashes while you build something.
const groups: { id: Group; label: string; hint: string }[] = [
  { id: "dev", label: "Development", hint: "Node, Python, .NET and other runtimes. WHY? notes when one stops with an error." },
  { id: "app", label: "Apps", hint: "Programs with a window." },
  { id: "background", label: "Background", hint: "Programs without a window." },
  { id: "windows", label: "Windows", hint: "Part of Windows itself." },
];

// Copies of one program are one row that opens into its processes.
interface ProgramRow {
  key: string;
  name: string;
  path: string;
  group: Group;
  processes: ProcessInfo[];
  cpu: number;
  memory: number;
  disk: number;
  ports: number[];
  crashes: number;
}

function ProgramIcon({ path }: { path: string }) {
  const icon = cachedIcon(path);
  return icon ? <img className="process-icon" src={icon} alt="" width="20" height="20" /> : <Icon name="app" className="process-icon process-glyph" />;
}

// Only values that stand out get colour, so the eye goes to what is heavy.
const cpuClass = (value: number) => (value >= 50 ? "value-strained" : value >= 15 ? "value-busy" : undefined);
const memoryClass = (bytes: number) => (bytes >= 2e9 ? "value-strained" : bytes >= 1e9 ? "value-busy" : undefined);

function Ports({ ports }: { ports: number[] }) {
  if (ports.length === 0) return null;
  return (
    <span className="ports">
      {ports.slice(0, 3).map((port) => (
        <span key={port} className="port">
          localhost:{port}
        </span>
      ))}
    </span>
  );
}

export function ProcessesScreen({ state, settings, onFocus }: Props) {
  const [processes, setProcesses] = useState<ProcessInfo[] | null>(null);
  const [query, setQuery] = useState("");
  const [sort, setSort] = useState<SortKey>("cpu");
  const [open, setOpen] = useState<Set<string>>(new Set());
  const [collapsed, setCollapsed] = useState<Set<Group>>(new Set(["background", "windows"]));
  const [, setIconsLoaded] = useState(0);

  useEffect(() => {
    let alive = true;
    const load = () =>
      bridge.listProcesses().then((list) => {
        if (!alive) return;
        setProcesses(list);
        requestIcons([...new Set(list.map((process) => process.path))]).then(() => {
          if (alive) setIconsLoaded((count) => count + 1);
        });
      });
    load();
    const timer = window.setInterval(load, REFRESH_MS);
    return () => {
      alive = false;
      window.clearInterval(timer);
    };
  }, []);

  // How often each program crashed or hung, from the cases WHY? already built.
  const crashesByName = useMemo(() => {
    const counts = new Map<string, number>();
    for (const item of state.cases) {
      if (item.signal !== "app_crash" && item.signal !== "app_hang") continue;
      const name = item.subject.slice(item.subject.lastIndexOf("\\") + 1).toLowerCase();
      counts.set(name, (counts.get(name) ?? 0) + item.incidents);
    }
    return counts;
  }, [state.cases]);

  const rows = useMemo(() => {
    const programs = new Map<string, ProgramRow>();
    for (const process of processes ?? []) {
      // Each dev process is its own row: two node.exe can be two different servers.
      const key = process.group === "dev" ? `dev|${process.pid}` : `${process.group}|${(process.path || process.name).toLowerCase()}`;
      const row = programs.get(key) ?? {
        key,
        name: process.name,
        path: process.path,
        group: process.group,
        processes: [],
        cpu: 0,
        memory: 0,
        disk: 0,
        ports: [],
        crashes: crashesByName.get(process.name.toLowerCase()) ?? 0,
      };
      row.processes.push(process);
      row.cpu += process.cpu;
      row.memory += process.memory;
      row.disk += process.disk;
      row.ports = [...new Set([...row.ports, ...process.ports])].sort((a, b) => a - b);
      programs.set(key, row);
    }
    const needle = query.trim().toLowerCase();
    const list = [...programs.values()].filter(
      (row) => !needle || `${row.name} ${row.path} ${row.ports.join(" ")} ${row.processes.map((process) => process.pid).join(" ")}`.toLowerCase().includes(needle),
    );
    const by: Record<SortKey, (a: ProgramRow, b: ProgramRow) => number> = {
      name: (a, b) => a.name.localeCompare(b.name),
      cpu: (a, b) => b.cpu - a.cpu || b.memory - a.memory,
      memory: (a, b) => b.memory - a.memory,
      crashes: (a, b) => b.crashes - a.crashes || b.cpu - a.cpu,
    };
    return list.sort(by[sort]);
  }, [processes, crashesByName, query, sort]);

  const openMenu = useContextMenu();

  // What can be done with a running program. WHY? only observes, so ending a process is not offered.
  const rowMenu = (row: ProgramRow): MenuEntry[] => {
    const pids = row.processes.map((process) => process.pid).join(", ");
    return [
      ...row.ports.slice(0, 3).map((port): MenuEntry => ({ label: `Open localhost:${port} in the browser`, icon: "browser", onSelect: () => bridge.openLocalhost(port) })),
      "separator",
      ...(row.path && !inFocus(row) ? [{ label: "Focus on this program", icon: "focus" as const, onSelect: () => onFocus({ focusKind: "program", focusTarget: row.path }) }] : []),
      ...(row.path ? [{ label: "Show in File Explorer", icon: "reveal" as const, onSelect: () => bridge.revealPath(row.path) }] : []),
      "separator",
      { label: "Copy name", icon: "copy", onSelect: () => copyText(row.name) },
      ...(row.path ? [{ label: "Copy path", icon: "copy" as const, onSelect: () => copyText(row.path) }] : []),
      { label: row.processes.length > 1 ? "Copy process IDs" : "Copy process ID", icon: "copy", onSelect: () => copyText(pids) },
    ];
  };

  const inFocus = (row: ProgramRow) => hasFocus(settings) && row.path !== "" && matchesFocus(settings, "app_crash", row.path);

  const toggle = <T,>(set: Set<T>, value: T) => {
    const next = new Set(set);
    if (next.has(value)) next.delete(value);
    else next.add(value);
    return next;
  };

  const header = (key: SortKey, label: string, numeric = true) => (
    <th scope="col" className={numeric ? "numeric" : undefined} aria-sort={sort === key ? (key === "name" ? "ascending" : "descending") : undefined}>
      <button className={sort === key ? "sort is-sorted" : "sort"} onClick={() => setSort(key)}>
        {label}
      </button>
    </th>
  );

  return (
    <>
      <h1>Processes</h1>
      <p className="muted">What is running now, with the tools you develop with first.</p>

      <div className="filters">
        <input type="search" placeholder="Search by name, path, port or PID" aria-label="Search processes" value={query} onChange={(event) => setQuery(event.target.value)} />
        <span className="filters-stats muted">{processes === null ? "Reading processes" : `${processes.length} processes`}</span>
      </div>

      {groups.map((group) => {
        const members = rows.filter((row) => row.group === group.id);
        if (members.length === 0 && group.id !== "dev") return null;
        const isCollapsed = collapsed.has(group.id) && !query;
        return (
          <section key={group.id} className="process-group">
            <button className="group-toggle" aria-expanded={!isCollapsed} onClick={() => setCollapsed(toggle(collapsed, group.id))}>
              <Icon name="chevron" className={isCollapsed ? "twisty" : "twisty is-open"} />
              <span>{group.label}</span>
              <span className="group-count">{members.length}</span>
              <span className="muted group-hint">{group.hint}</span>
            </button>
            {!isCollapsed && members.length === 0 && (
              <p className="muted empty-group">No development tools running. Start one with why-run, for example: why-run npm run dev</p>
            )}
            {!isCollapsed && members.length > 0 && (
              <table className="data processes card">
                <thead>
                  <tr>
                    {header("name", "Name", false)}
                    {header("cpu", "CPU")}
                    {header("memory", "Memory")}
                    <th scope="col" className="numeric">
                      Disk
                    </th>
                    {header("crashes", "Crashes")}
                    <th scope="col">
                      <span className="visually-hidden">Focus</span>
                    </th>
                  </tr>
                </thead>
                <tbody>
                  {members.map((row) => {
                    const expandable = row.processes.length > 1;
                    const isOpen = open.has(row.key);
                    return (
                      <Fragment key={row.key}>
                        <tr className={inFocus(row) ? "program-row is-focus" : "program-row"} onContextMenu={(event) => openMenu(event, rowMenu(row))}>
                          <td>
                            <div className="process-cell">
                              {expandable ? (
                                <button className="twisty-button" aria-expanded={isOpen} aria-label={`Show the ${row.processes.length} processes`} onClick={() => setOpen(toggle(open, row.key))}>
                                  <Icon name="chevron" className={isOpen ? "twisty is-open" : "twisty"} />
                                </button>
                              ) : (
                                <span className="twisty-space" />
                              )}
                              <ProgramIcon path={row.path} />
                              <span className="process-text">
                                <span className="process-name">
                                  {row.name}
                                  {expandable && <span className="muted"> ({row.processes.length})</span>}
                                  {row.group === "dev" && <span className="muted"> PID {row.processes[0].pid}</span>}
                                </span>
                                <span className="process-path muted" title={row.path}>
                                  {row.path || `PID ${row.processes[0].pid}`}
                                </span>
                                <Ports ports={row.ports} />
                              </span>
                            </div>
                          </td>
                          <td className={`numeric ${cpuClass(row.cpu) ?? ""}`}>{row.cpu.toFixed(1)}%</td>
                          <td className={`numeric ${memoryClass(row.memory) ?? ""}`}>{formatBytes(row.memory)}</td>
                          <td className="numeric muted">{row.disk >= 50_000 ? formatRate(row.disk) : "-"}</td>
                          <td className="numeric">{row.crashes > 0 ? <span className="crash-count">{row.crashes}</span> : <span className="muted">-</span>}</td>
                          <td className="process-action">
                            {inFocus(row) ? (
                              <span className="in-focus">
                                <Icon name="focus" /> In focus
                              </span>
                            ) : (
                              row.path &&
                              row.group !== "dev" && (
                                <button className="subtle" onClick={() => onFocus({ focusKind: "program", focusTarget: row.path })}>
                                  Focus
                                </button>
                              )
                            )}
                          </td>
                        </tr>
                        {isOpen &&
                          row.processes.map((process) => (
                            <tr key={process.pid} className="child-row">
                              <td>
                                <span className="child-name">PID {process.pid}</span>
                              </td>
                              <td className="numeric">{process.cpu.toFixed(1)}%</td>
                              <td className="numeric">{formatBytes(process.memory)}</td>
                              <td className="numeric muted">{process.disk >= 50_000 ? formatRate(process.disk) : "-"}</td>
                              <td />
                              <td />
                            </tr>
                          ))}
                      </Fragment>
                    );
                  })}
                </tbody>
              </table>
            )}
          </section>
        );
      })}
      {processes !== null && rows.length === 0 && query && <p className="muted">No process matches.</p>}
    </>
  );
}

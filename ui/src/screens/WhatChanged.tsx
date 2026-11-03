import { useMemo, useState } from "react";
import { changeKindLabels, changeVersion, formatDay, plural, sourceLabels } from "../format";
import type { Change, State } from "../types";

export function WhatChangedScreen({ state }: { state: State }) {
  const [onlyDrivers, setOnlyDrivers] = useState(true);
  const [showHabitual, setShowHabitual] = useState(false);
  const [driverQuery, setDriverQuery] = useState("");

  // Some programs re-register their own driver every time they start. That is a habit, not a change.
  const habitual = useMemo(() => {
    const installs = new Map<string, number>();
    for (const change of state.changes) {
      if (change.source === "service_install") installs.set(change.name, (installs.get(change.name) ?? 0) + 1);
    }
    return new Set([...installs].filter(([, count]) => count >= 3).map(([name]) => name));
  }, [state.changes]);

  const days = useMemo(() => {
    const groups = new Map<string, Change[]>();
    for (const change of state.changes) {
      if (onlyDrivers && change.kind === "update_installed") continue;
      if (!showHabitual && change.source === "service_install" && habitual.has(change.name)) continue;
      const day = formatDay(change.timeMs);
      const group = groups.get(day);
      if (group) group.push(change);
      else groups.set(day, [change]);
    }
    return [...groups.entries()];
  }, [state.changes, onlyDrivers, showHabitual, habitual]);

  const drivers = useMemo(() => {
    const needle = driverQuery.trim().toLowerCase();
    return state.drivers
      .filter((driver) => !needle || `${driver.displayName} ${driver.module} ${driver.provider} ${driver.deviceClass}`.toLowerCase().includes(needle))
      .sort((a, b) => b.installedMs - a.installedMs);
  }, [state.drivers, driverQuery]);

  return (
    <>
      <h1>What changed</h1>
      <p className="muted">A change near a problem is a lead, not a verdict. Dates marked "around" are estimates from what Windows has kept.</p>
      <div className="filters">
        <label className="check">
          <input type="checkbox" checked={onlyDrivers} onChange={(event) => setOnlyDrivers(event.target.checked)} />
          Drivers only
        </label>
        {habitual.size > 0 && (
          <label className="check">
            <input type="checkbox" checked={showHabitual} onChange={(event) => setShowHabitual(event.target.checked)} />
            Show {plural(habitual.size, "driver")} that keep reinstalling themselves
          </label>
        )}
      </div>
      {days.length === 0 && <p className="muted">No changes on record.</p>}
      {days.map(([day, changes]) => (
        <section className="day" key={day}>
          <h2 className="section-title">
            {day}
            {changes.length >= 10 && <span className="muted"> ({plural(changes.length, "change")}, probably a Windows update or reinstall)</span>}
          </h2>
          <ul className="plain-rows change-rows">
            {changes.map((change, index) => (
              <li key={`${change.name}-${index}`}>
                <span>{changeKindLabels[change.kind] ?? change.kind}</span>
                <span className="plain-main">
                  <span>{change.name}</span>
                  {changeVersion(change) && <span className="muted">{changeVersion(change)}</span>}
                </span>
                <span className="muted">
                  {change.estimated && "around, "}
                  {sourceLabels[change.source] ?? change.source}
                </span>
              </li>
            ))}
          </ul>
        </section>
      ))}

      <section className="drivers">
        <h2 className="section-title">Installed drivers ({state.drivers.length})</h2>
        <div className="filters">
          <input type="search" placeholder="Search drivers" aria-label="Search drivers" value={driverQuery} onChange={(event) => setDriverQuery(event.target.value)} />
        </div>
        <table className="data">
          <thead>
            <tr>
              <th scope="col">Driver</th>
              <th scope="col">Maker</th>
              <th scope="col">Version</th>
              <th scope="col">Installed around</th>
            </tr>
          </thead>
          <tbody>
            {drivers.map((driver) => (
              <tr key={driver.service}>
                <td>
                  {driver.displayName}
                  <code className="muted"> {driver.module}</code>
                </td>
                <td>{driver.provider}</td>
                <td>{driver.version}</td>
                <td>{driver.installedMs > 0 ? formatDay(driver.installedMs) : ""}</td>
              </tr>
            ))}
          </tbody>
        </table>
      </section>
    </>
  );
}

// Polls the host for PC statistics while a screen needs them, and keeps the last minute so graphs survive tab changes.
import { useEffect, useState } from "react";
import * as bridge from "./bridge";
import type { SystemStats } from "./types";

export const HISTORY_LENGTH = 60;
const INTERVAL_MS = 1000;

export interface StatsHistory {
  cpu: number[];
  memory: number[];
  gpu: number[];
  disk: number[];
  received: number[];
  sent: number[];
}

const history: StatsHistory = { cpu: [], memory: [], gpu: [], disk: [], received: [], sent: [] };
let latest: SystemStats | null = null;

const push = (series: number[], value: number) => {
  series.push(value);
  if (series.length > HISTORY_LENGTH) series.shift();
};

// The busiest physical disk, as Task Manager's Disk column header shows it.
export const busiestDisk = (stats: SystemStats) => Math.max(0, ...stats.disks.map((disk) => disk.active));

function record(stats: SystemStats) {
  latest = stats;
  push(history.cpu, stats.cpu.usage);
  push(history.memory, stats.memory.load);
  push(history.gpu, stats.gpu.usage);
  push(history.disk, busiestDisk(stats));
  push(history.received, stats.network.reduce((sum, adapter) => sum + adapter.received, 0));
  push(history.sent, stats.network.reduce((sum, adapter) => sum + adapter.sent, 0));
}

export function useSystemStats(): { stats: SystemStats | null; history: StatsHistory } {
  const [, setTick] = useState(0);

  useEffect(() => {
    let alive = true;
    const load = () =>
      bridge.systemStats().then((stats) => {
        if (!alive) return;
        record(stats);
        setTick((tick) => tick + 1);
      });
    load();
    const timer = window.setInterval(load, INTERVAL_MS);
    return () => {
      alive = false;
      window.clearInterval(timer);
    };
  }, []);

  return { stats: latest, history };
}

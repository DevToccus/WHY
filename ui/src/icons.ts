// Program icons from the host, fetched once per file and shared by every screen.
import { useEffect, useState } from "react";
import * as bridge from "./bridge";

const cache = new Map<string, string | null>();
const waiting = new Map<string, Promise<void>>();
let queued = new Set<string>();
let flush: Promise<void> | null = null;

// Requests made in the same moment go to the host together.
function request(path: string): Promise<void> {
  const pending = waiting.get(path);
  if (pending) return pending;
  queued.add(path);
  flush ??= Promise.resolve().then(async () => {
    const paths = [...queued];
    queued = new Set();
    flush = null;
    const icons = await bridge.programIcons(paths);
    for (const item of paths) cache.set(item, icons[item] ?? null);
  });
  const done = flush;
  waiting.set(path, done);
  return done;
}

export function requestIcons(paths: string[]) {
  return Promise.all(paths.filter((path) => path && !cache.has(path)).map(request));
}

export const cachedIcon = (path: string) => (path ? cache.get(path) ?? null : null);

// The icon of one program file, or null while it loads or when there is none.
export function useProgramIcon(path: string | undefined): string | null {
  const [icon, setIcon] = useState(() => cachedIcon(path ?? ""));
  useEffect(() => {
    if (!path) return;
    if (cache.has(path)) {
      setIcon(cachedIcon(path));
      return;
    }
    let alive = true;
    request(path).then(() => {
      if (alive) setIcon(cachedIcon(path));
    });
    return () => {
      alive = false;
    };
  }, [path]);
  return icon;
}

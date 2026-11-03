// Talks to the C++ host through WebView2. Replies are matched to requests by id.
import type { State } from "./types";

interface HostChannel {
  postMessage(message: unknown): void;
  addEventListener(type: "message", listener: (event: { data: any }) => void): void;
}

declare global {
  interface Window {
    chrome?: { webview?: HostChannel };
  }
}

export interface ActionResult {
  ok: boolean;
  cancelled?: boolean;
  error?: string;
  path?: string;
}

const host = window.chrome?.webview;
const pending = new Map<number, (value: any) => void>();
const listeners = new Set<() => void>();
let nextId = 1;

host?.addEventListener("message", ({ data }) => {
  if (data.event === "stateChanged") {
    listeners.forEach((listener) => listener());
  } else if (typeof data.id === "number") {
    pending.get(data.id)?.(data.result);
    pending.delete(data.id);
  }
});

function request<T>(cmd: string, args: Record<string, unknown> = {}): Promise<T> {
  if (!host) return Promise.reject(new Error("No host"));
  return new Promise<T>((resolve) => {
    const id = nextId++;
    pending.set(id, resolve);
    host.postMessage({ id, cmd, ...args });
  });
}

export const hasHost = Boolean(host);
export const onStateChanged = (listener: () => void) => {
  listeners.add(listener);
  return () => void listeners.delete(listener);
};
export const getState = () => request<State>("getState");
export const caseAction = (key: string, action: "tried_fix" | "reopen") => request<ActionResult>("caseAction", { key, action });
export const openTool = (tool: string) => request<ActionResult>("openTool", { tool });
export const saveReport = () => request<ActionResult>("saveReport");
export const openReport = () => request<ActionResult>("openReport");
export const closeReport = () => request<ActionResult>("closeReport");
export const restartElevated = () => request<ActionResult>("restartElevated");
export const installLive = () => request<ActionResult>("installLive");
export const uninstallLive = () => request<ActionResult>("uninstallLive");

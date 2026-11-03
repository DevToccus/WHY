import { useCallback, useEffect, useState } from "react";
import * as bridge from "./bridge";
import { formatMoment, needsLook } from "./format";
import { CaseDetail, CaseList } from "./screens/Cases";
import { StatusScreen } from "./screens/Status";
import { TimelineScreen } from "./screens/Timeline";
import { WhatChangedScreen } from "./screens/WhatChanged";
import type { State } from "./types";

type Screen = "status" | "cases" | "timeline" | "changes";

const navigation: { id: Screen; label: string }[] = [
  { id: "status", label: "Status" },
  { id: "cases", label: "Cases" },
  { id: "timeline", label: "Timeline" },
  { id: "changes", label: "What changed" },
];

async function loadState(): Promise<State> {
  if (bridge.hasHost) return bridge.getState();
  if (import.meta.env.DEV) {
    const { loadDevState } = await import("./dev/devState");
    return loadDevState();
  }
  throw new Error("WHY? must run inside its own window.");
}

export default function App() {
  const [state, setState] = useState<State | null>(null);
  const [screen, setScreen] = useState<Screen>("status");
  const [openCase, setOpenCase] = useState<string | null>(null);
  const [message, setMessage] = useState<string | null>(null);

  const refresh = useCallback(() => {
    loadState()
      .then((next) => {
        if (next.schema) setState(next);
      })
      .catch((error: Error) => setMessage(error.message));
  }, []);

  useEffect(() => {
    refresh();
    return bridge.onStateChanged(refresh);
  }, [refresh]);

  const showCase = (key: string) => {
    setOpenCase(key);
    setScreen("cases");
  };

  const navigate = (next: Screen) => {
    setOpenCase(null);
    setScreen(next);
  };

  const runAction = async (action: () => Promise<bridge.ActionResult>, success?: string) => {
    setMessage(null);
    const result = await action();
    if (result.cancelled) return;
    if (!result.ok) setMessage(result.error ?? "That did not work. Windows gave no further detail.");
    else if (success) setMessage(success);
  };

  if (!state) {
    return (
      <main className="loading" aria-busy="true">
        <p className="brand">WHY?</p>
        <p>{message ?? "Reading what Windows has recorded."}</p>
      </main>
    );
  }

  const current = state.cases.find((item) => item.key === openCase);
  const openCount = state.cases.filter(needsLook).length;
  const isReport = state.mode === "report";

  return (
    <div className="app">
      <nav className="sidebar" aria-label="Main">
        <p className="brand">WHY?</p>
        <ul>
          {navigation.map((item) => (
            <li key={item.id}>
              <button className="nav-item" aria-current={screen === item.id ? "page" : undefined} onClick={() => navigate(item.id)}>
                {item.label}
                {item.id === "cases" && openCount > 0 && <span className="count">{openCount}</span>}
              </button>
            </li>
          ))}
        </ul>
        <div className="sidebar-actions">
          {/* A report carries the name of the version that saved it, which may not be the one running. */}
          {!isReport && state.releaseName && <p className="release muted">{state.releaseName}</p>}
          {!isReport && (
            <button className="quiet" onClick={() => runAction(bridge.saveReport, "Report saved. Account and computer names were removed.")}>
              Save report
            </button>
          )}
          <button className="quiet" onClick={() => runAction(bridge.openReport)}>
            Open report
          </button>
        </div>
      </nav>

      <div className="content">
        {isReport && (
          <div className="banner">
            <span>
              Reading a report saved {formatMoment(state.generatedMs)}
              {state.releaseName && ` by ${state.releaseName}`}, from {state.machine.os}.
            </span>
            <button className="quiet" onClick={() => runAction(bridge.closeReport)}>
              Back to this PC
            </button>
          </div>
        )}
        {message && (
          <div className="banner" role="status">
            <span>{message}</span>
            <button className="quiet" onClick={() => setMessage(null)}>
              Dismiss
            </button>
          </div>
        )}
        <main className="screen" key={`${screen}-${openCase ?? ""}`}>
          {screen === "status" && <StatusScreen state={state} onOpenCase={showCase} onShowCases={() => navigate("cases")} onAction={runAction} />}
          {screen === "cases" &&
            (current ? (
              <CaseDetail item={current} isReport={isReport} now={state.generatedMs} onBack={() => setOpenCase(null)} onAction={runAction} />
            ) : (
              <CaseList state={state} onOpenCase={showCase} />
            ))}
          {screen === "timeline" && <TimelineScreen state={state} />}
          {screen === "changes" && <WhatChangedScreen state={state} />}
        </main>
      </div>
    </div>
  );
}

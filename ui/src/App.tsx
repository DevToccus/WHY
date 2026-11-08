import { useCallback, useEffect, useState } from "react";
import logo from "./assets/logo.png";
import { ActionsContext } from "./actions";
import * as bridge from "./bridge";
import { ContextMenuProvider } from "./components/ContextMenu";
import { Icon, type IconName } from "./components/Icon";
import { focusName, focusedState, hasFocus } from "./focus";
import { formatMoment, needsLook } from "./format";
import { CaseDetail, CaseList } from "./screens/Cases";
import { FocusScreen } from "./screens/Focus";
import { MonitorScreen } from "./screens/Monitor";
import { ProcessesScreen } from "./screens/Processes";
import { SettingsScreen } from "./screens/SettingsScreen";
import { SetupScreen } from "./screens/Setup";
import { StatusScreen } from "./screens/Status";
import { TimelineScreen } from "./screens/Timeline";
import { WhatChangedScreen } from "./screens/WhatChanged";
import type { Settings, State } from "./types";

type Screen = "status" | "cases" | "focus" | "monitor" | "processes" | "timeline" | "changes" | "settings";

// Secondary tools, kept at the bottom: WHY? is about what went wrong, not about load.
const tools: { id: Screen; label: string; icon: IconName }[] = [
  { id: "processes", label: "Processes", icon: "processes" },
  { id: "monitor", label: "Monitor", icon: "performance" },
];

const navigation: { id: Screen; label: string; icon: IconName }[] = [
  { id: "status", label: "Home", icon: "home" },
  { id: "cases", label: "Cases", icon: "cases" },
  { id: "focus", label: "Focus", icon: "focus" },
  { id: "timeline", label: "Timeline", icon: "timeline" },
  { id: "changes", label: "What changed", icon: "changes" },
];

// "v0.1.0 · Triage": the code name without the "WHY -" the brand already shows.
function versionLine(state: State) {
  const codename = (state.releaseName ?? "").replace(/^WHY\??\s*[-–]\s*/i, "");
  const version = /^\d/.test(state.appVersion ?? "") ? `v${state.appVersion}` : state.appVersion;
  return [version, codename].filter(Boolean).join(" · ");
}

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
  const [settings, setSettings] = useState<Settings | null>(null);
  const [elevated, setElevated] = useState(false);
  // With a Focus set, Windows' own problems stay hidden unless asked for, for this session only.
  const [showEverything, setShowEverything] = useState(false);

  const refresh = useCallback(() => {
    loadState()
      .then((next) => {
        if (next.schema) setState(next);
        // The engine may still be starting. Ask again rather than wait for a change that may take a while.
        else window.setTimeout(refresh, 1000);
      })
      .catch((error: Error) => setMessage(error.message));
  }, []);

  useEffect(() => {
    refresh();
    return bridge.onStateChanged(refresh);
  }, [refresh]);

  useEffect(() => {
    bridge.getSettings().then((reply) => {
      setSettings(reply.settings);
      setElevated(reply.elevated);
    });
  }, []);

  const showCase = (key: string) => {
    setOpenCase(key);
    setScreen("cases");
  };

  const navigate = (next: Screen) => {
    setOpenCase(null);
    setScreen(next);
  };

  // Focus chosen from Processes is saved straight away, as Settings does.
  const changeFocus = async (focus: Pick<Settings, "focusKind" | "focusTarget">) => {
    if (!settings) return;
    const next = { ...settings, ...focus };
    setSettings(next);
    setShowEverything(false);
    const result = await bridge.saveSettings(next);
    if (!result.ok) setMessage(result.error ?? "Windows did not accept this change.");
  };

  const runAction = async (action: () => Promise<bridge.ActionResult>, success?: string) => {
    setMessage(null);
    const result = await action();
    if (result.cancelled) return;
    if (!result.ok) setMessage(result.error ?? "That did not work. Windows gave no further detail.");
    else if (success) setMessage(success);
  };

  if (settings && !settings.setupDone) {
    return (
      <SetupScreen
        initial={settings}
        elevated={elevated}
        liveInstalled={state?.mode === "live"}
        onDone={(done) => {
          setSettings(done);
          setScreen("status");
        }}
      />
    );
  }

  if (!state) {
    return (
      <main className="loading" aria-busy="true">
        <div className="spinner" />
        <p>{message ?? "Reading what Windows has recorded."}</p>
      </main>
    );
  }

  const isReport = state.mode === "report";
  const focused = hasFocus(settings) && !isReport && !showEverything;
  const view = focused ? focusedState(state, settings) : state;
  const current = view.cases.find((item) => item.key === openCase);
  const openCount = view.cases.filter(needsLook).length;

  const actions = { isReport, openCase: showCase, runAction, setFocus: changeFocus };

  return (
    <ActionsContext.Provider value={actions}>
    <ContextMenuProvider>
    <div className="app">
      <nav className="sidebar" aria-label="Main">
        <div className="brand">
          <img className="brand-logo" src={logo} alt="" width="32" height="32" />
          <span>
            <strong>WHY?</strong>
            {/* A report carries the version that saved it, which may not be the one running. */}
            {!isReport && <span className="release muted">{versionLine(state)}</span>}
          </span>
        </div>
        <ul>
          {navigation.map((item) => (
            <li key={item.id}>
              <button className="nav-item" aria-current={screen === item.id ? "page" : undefined} onClick={() => navigate(item.id)}>
                <Icon name={item.icon} />
                <span className="nav-label">{item.label}</span>
                {item.id === "cases" && openCount > 0 && <span className="count">{openCount}</span>}
              </button>
            </li>
          ))}
        </ul>
        <div className="sidebar-actions">
          {tools.map((item) => (
            <button key={item.id} className="nav-item nav-tool" aria-current={screen === item.id ? "page" : undefined} onClick={() => navigate(item.id)}>
              <Icon name={item.icon} />
              <span className="nav-label">{item.label}</span>
            </button>
          ))}
          <hr className="nav-divider" />
          <button className="nav-item" aria-current={screen === "settings" ? "page" : undefined} onClick={() => navigate("settings")}>
            <Icon name="settings" />
            <span className="nav-label">Settings</span>
          </button>
          {!isReport && (
            <button className="nav-item" onClick={() => runAction(bridge.saveReport, "Report saved. Account and computer names were removed.")}>
              <Icon name="save" />
              <span className="nav-label">Save report</span>
            </button>
          )}
          <button className="nav-item" onClick={() => runAction(bridge.openReport)}>
            <Icon name="open" />
            <span className="nav-label">Open report</span>
          </button>
        </div>
      </nav>

      <div className="content">
        {isReport && (
          <div className="banner">
            <Icon name="info" className="banner-icon" />
            <span>
              Reading a report saved {formatMoment(state.generatedMs)}
              {state.releaseName && ` by ${state.releaseName}`}, from {state.machine.os}.
            </span>
            <button className="quiet" onClick={() => runAction(bridge.closeReport)}>
              Back to this PC
            </button>
          </div>
        )}
        {hasFocus(settings) && !isReport && screen !== "settings" && screen !== "processes" && screen !== "monitor" && screen !== "focus" && (
          <div className="banner banner-focus">
            <Icon name="focus" className="banner-icon" />
            <span>
              {focused ? (
                <>
                  <strong>Focus: {focusName(settings)}.</strong> Only its crashes and hangs are shown, plus Microsoft Defender detections, not Windows itself.
                </>
              ) : (
                <>Showing everything. Focus on {focusName(settings)} is paused for now.</>
              )}
            </span>
            <button className="quiet" onClick={() => setShowEverything(!showEverything)}>
              {focused ? "Show everything" : "Back to Focus"}
            </button>
            <button className="subtle" onClick={() => navigate("focus")}>
              Change
            </button>
          </div>
        )}
        {message && (
          <div className="banner" role="status">
            <Icon name="info" className="banner-icon" />
            <span>{message}</span>
            <button className="subtle icon-button" aria-label="Dismiss" onClick={() => setMessage(null)}>
              <Icon name="dismiss" />
            </button>
          </div>
        )}
        <main className="screen" key={`${screen}-${openCase ?? ""}`}>
          {screen === "status" && (
            <StatusScreen state={view} onOpenCase={showCase} onShowCases={() => navigate("cases")} onShowSettings={() => navigate("settings")} />
          )}
          {screen === "cases" &&
            (current ? (
              <CaseDetail item={current} isReport={isReport} now={state.generatedMs} onBack={() => setOpenCase(null)} onAction={runAction} />
            ) : (
              <CaseList state={view} onOpenCase={showCase} />
            ))}
          {screen === "focus" && settings && <FocusScreen state={state} settings={settings} onFocus={changeFocus} onOpenCase={showCase} />}
          {screen === "monitor" && <MonitorScreen />}
          {screen === "processes" && settings && <ProcessesScreen state={state} settings={settings} onFocus={changeFocus} />}
          {screen === "timeline" && <TimelineScreen state={view} />}
          {screen === "changes" && <WhatChangedScreen state={state} />}
          {screen === "settings" && settings && (
            <SettingsScreen state={state} settings={settings} onChange={setSettings} onAction={runAction} />
          )}
        </main>
      </div>
    </div>
    </ContextMenuProvider>
    </ActionsContext.Provider>
  );
}

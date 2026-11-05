// What a row's right-click menu can do, provided once by the app so rows deep in any screen can offer it.
import { createContext, useContext } from "react";
import type * as bridge from "./bridge";
import type { Settings } from "./types";

export interface AppActions {
  isReport: boolean;
  openCase: (key: string) => void;
  runAction: (action: () => Promise<bridge.ActionResult>, success?: string) => void;
  setFocus: (focus: Pick<Settings, "focusKind" | "focusTarget">) => void;
}

export const ActionsContext = createContext<AppActions>({
  isReport: false,
  openCase: () => {},
  runAction: () => {},
  setFocus: () => {},
});

export const useActions = () => useContext(ActionsContext);

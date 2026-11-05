import { useState } from "react";
import { CaseRow } from "../components/CaseRow";
import { Icon } from "../components/Icon";
import { FocusOptions } from "../components/Options";
import { focusName, focusedState, hasFocus } from "../focus";
import { needsLook, plural } from "../format";
import type { Settings, State } from "../types";

interface Props {
  state: State;
  settings: Settings;
  onFocus: (focus: Pick<Settings, "focusKind" | "focusTarget">) => void;
  onOpenCase: (key: string) => void;
}

// Focus: one project folder or one program, and only what went wrong with it.
export function FocusScreen({ state, settings, onFocus, onOpenCase }: Props) {
  // A folder or program choice waits here until one is picked; the saved Focus changes only then.
  const [draft, setDraft] = useState<Pick<Settings, "focusKind" | "focusTarget">>({ focusKind: settings.focusKind, focusTarget: settings.focusTarget });
  const active = hasFocus(settings);
  const cases = active ? focusedState(state, settings).cases : [];
  const attention = cases.filter(needsLook);

  return (
    <>
      <h1>Focus</h1>
      <p className="muted">
        Point WHY? at what you are building. It then shows only its crashes, hangs, failed dev servers and commands, plus Microsoft Defender
        detections, and notifies you only about those.
      </p>

      <section>
        <h2 className="section-title">What to focus on</h2>
        <FocusOptions
          value={draft}
          onChange={(next) => {
            setDraft(next);
            if (!next.focusKind || next.focusTarget) onFocus(next);
          }}
        />
      </section>

      {active && (
        <section>
          <h2 className="section-title">
            <Icon name="focus" /> {focusName(settings)}
            <span className="muted section-note">
              {cases.length === 0 ? "Nothing on record yet" : `${plural(cases.length, "case")}, ${attention.length} need a look`}
            </span>
          </h2>
          {cases.length === 0 ? (
            <p className="muted empty-group">
              No crashes, hangs or failed commands for this yet. Start dev servers with <code>why-run npm run dev</code> to keep the error they print
              if they fail.
            </p>
          ) : (
            <ul className="rows">
              {cases.map((item) => (
                <li key={item.key}>
                  <CaseRow item={item} now={state.generatedMs} onOpen={onOpenCase} />
                </li>
              ))}
            </ul>
          )}
        </section>
      )}
    </>
  );
}

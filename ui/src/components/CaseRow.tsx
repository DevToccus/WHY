import { useActions } from "../actions";
import * as bridge from "../bridge";
import { alertLabels, isFixed, relativeDay, statusLabels } from "../format";
import { useProgramIcon } from "../icons";
import type { Case } from "../types";
import { copyText, useContextMenu, type MenuEntry } from "./ContextMenu";
import { Icon } from "./Icon";

type Tone = Case["severity"] | "quiet" | "fixed";

const toneOf = (item: Case): Tone => (isFixed(item) ? "fixed" : item.alert ? item.severity : "quiet");

function SeverityBadge({ tone, small = false }: { tone: Tone; small?: boolean }) {
  return (
    <span className={`infobar-badge badge-${tone}${small ? " badge-small" : ""}`} aria-hidden="true">
      {tone === "critical" ? <Icon name="dismiss" /> : tone === "fixed" ? <Icon name="ok" /> : tone === "quiet" ? "i" : "!"}
    </span>
  );
}

// The program a case is about, with its severity badge on the corner. Without a program, the badge alone.
export function CaseIcon({ item, size = 24 }: { item: Case; size?: number }) {
  const icon = useProgramIcon(item.image);
  const tone = toneOf(item);
  if (!icon) return <SeverityBadge tone={tone} />;
  return (
    <span className="case-icon" style={{ width: size, height: size }}>
      <img src={icon} alt="" width={size} height={size} />
      <SeverityBadge tone={tone} small />
    </span>
  );
}

// The plain text of a case, for pasting into an issue or a chat.
export function caseAsText(item: Case) {
  const lines = [item.title, item.summary, "", ...item.findings.map((finding) => `- ${finding.text}`)];
  if (item.output) lines.push("", "Output:", item.output);
  return lines.join("\n");
}

// The right-click menu of a case, the same wherever the case appears.
export function useCaseMenu(item: Case): MenuEntry[] {
  const { isReport, openCase, runAction, setFocus } = useActions();
  const fixed = isFixed(item);
  const folder = item.signal.startsWith("dev_") ? item.subject.slice(item.subject.indexOf("@") + 1) : "";
  const entries: MenuEntry[] = [{ label: "Open details", icon: "chevron", onSelect: () => openCase(item.key) }];
  if (!isReport) {
    entries.push(
      fixed
        ? { label: "Reopen", icon: "undo", onSelect: () => runAction(() => bridge.caseAction(item.key, "reopen")) }
        : {
            label: "Mark as fixed",
            icon: "ok",
            onSelect: () => runAction(() => bridge.caseAction(item.key, "tried_fix"), "Marked as fixed. It leaves Needs a look, and WHY? tells you if it comes back."),
          },
    );
  }
  entries.push("separator", { label: "Copy title", icon: "copy", onSelect: () => copyText(item.title) });
  entries.push({ label: "Copy details", icon: "copy", onSelect: () => copyText(caseAsText(item)) });
  if (item.output) entries.push({ label: "Copy terminal output", icon: "copy", onSelect: () => copyText(item.output ?? "") });
  entries.push("separator");
  if (item.image && !isReport) entries.push({ label: "Show in File Explorer", icon: "reveal", onSelect: () => runAction(() => bridge.revealPath(item.image!)) });
  if (item.image && item.signal.startsWith("app_")) {
    entries.push({ label: "Focus on this program", icon: "focus", onSelect: () => setFocus({ focusKind: "program", focusTarget: item.image! }) });
  }
  if (folder) {
    entries.push({ label: "Focus on this folder", icon: "focus", onSelect: () => setFocus({ focusKind: "folder", focusTarget: folder }) });
    if (!isReport) entries.push({ label: "Show folder in File Explorer", icon: "folder", onSelect: () => runAction(() => bridge.revealPath(folder)) });
  }
  return entries;
}

// A case as a Windows 11 InfoBar: a tinted bar with the program's icon or the severity badge, a bold title and the message.
// Only cases that need a look are tinted; the rest are plain cards.
export function CaseRow({ item, now, onOpen }: { item: Case; now: number; onOpen: (key: string) => void }) {
  const tone = toneOf(item);
  const openMenu = useContextMenu();
  const menu = useCaseMenu(item);
  return (
    <button className={`row infobar infobar-${tone}`} onClick={() => onOpen(item.key)} onContextMenu={(event) => openMenu(event, menu)}>
      <CaseIcon item={item} />
      <span className="row-main">
        <strong>{item.title}</strong>
        <span className="muted">{item.summary}</span>
      </span>
      <span className="row-side">
        <span>{item.alert ? alertLabels[item.alert] : statusLabels[item.status]}</span>
        <span className="muted">{relativeDay(item.lastMs, now)}</span>
      </span>
      <Icon name="chevron" className="row-chevron" />
    </button>
  );
}

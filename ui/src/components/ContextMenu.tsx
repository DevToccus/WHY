import { createContext, useCallback, useContext, useEffect, useLayoutEffect, useRef, useState } from "react";
import { Icon, type IconName } from "./Icon";

export type MenuEntry = { label: string; icon?: IconName; onSelect: () => void; disabled?: boolean } | "separator";

type Open = (event: React.MouseEvent, entries: MenuEntry[]) => void;

const MenuContext = createContext<Open>(() => {});

// Opens the right-click menu with these entries at the pointer. Entries that are false or empty are left out.
export const useContextMenu = () => useContext(MenuContext);

// One menu for the whole window, drawn like a Windows 11 context menu: a flyout with icons, separators and keys.
export function ContextMenuProvider({ children }: { children: React.ReactNode }) {
  const [menu, setMenu] = useState<{ x: number; y: number; entries: MenuEntry[] } | null>(null);
  const [position, setPosition] = useState<{ left: number; top: number } | null>(null);
  const ref = useRef<HTMLDivElement>(null);

  const open = useCallback<Open>((event, entries) => {
    event.preventDefault();
    event.stopPropagation();
    // Trailing or doubled separators would look like a mistake.
    const tidy = entries.filter((entry, index, list) => entry !== "separator" || (index > 0 && index < list.length - 1 && list[index - 1] !== "separator"));
    if (tidy.length === 0) return;
    setPosition(null);
    setMenu({ x: event.clientX, y: event.clientY, entries: tidy });
  }, []);

  const close = useCallback(() => setMenu(null), []);

  // Kept inside the window: a menu opened near an edge opens towards the inside.
  useLayoutEffect(() => {
    if (!menu || !ref.current) return;
    const box = ref.current.getBoundingClientRect();
    setPosition({
      left: Math.max(4, Math.min(menu.x, window.innerWidth - box.width - 4)),
      top: Math.max(4, menu.y + box.height > window.innerHeight - 4 ? menu.y - box.height : menu.y),
    });
    ref.current.querySelector<HTMLButtonElement>("button:not(:disabled)")?.focus({ preventScroll: true });
  }, [menu]);

  useEffect(() => {
    if (!menu) return;
    const onKey = (event: KeyboardEvent) => {
      if (event.key === "Escape") close();
      if (event.key !== "ArrowDown" && event.key !== "ArrowUp") return;
      event.preventDefault();
      const buttons = [...(ref.current?.querySelectorAll<HTMLButtonElement>("button:not(:disabled)") ?? [])];
      const at = buttons.indexOf(document.activeElement as HTMLButtonElement);
      const next = event.key === "ArrowDown" ? (at + 1) % buttons.length : (at - 1 + buttons.length) % buttons.length;
      buttons[next]?.focus();
    };
    const onPointer = (event: PointerEvent) => {
      if (!ref.current?.contains(event.target as Node)) close();
    };
    window.addEventListener("keydown", onKey);
    window.addEventListener("pointerdown", onPointer, true);
    window.addEventListener("blur", close);
    window.addEventListener("resize", close);
    document.addEventListener("scroll", close, true);
    return () => {
      window.removeEventListener("keydown", onKey);
      window.removeEventListener("pointerdown", onPointer, true);
      window.removeEventListener("blur", close);
      window.removeEventListener("resize", close);
      document.removeEventListener("scroll", close, true);
    };
  }, [menu, close]);

  return (
    <MenuContext.Provider value={open}>
      {children}
      {menu && (
        <div
          ref={ref}
          className="context-menu"
          role="menu"
          style={position ? { left: position.left, top: position.top } : { left: menu.x, top: menu.y, visibility: "hidden" }}
          onContextMenu={(event) => event.preventDefault()}
        >
          {menu.entries.map((entry, index) =>
            entry === "separator" ? (
              <hr key={index} className="context-separator" />
            ) : (
              <button
                key={index}
                role="menuitem"
                disabled={entry.disabled}
                onClick={() => {
                  close();
                  entry.onSelect();
                }}
              >
                {entry.icon ? <Icon name={entry.icon} /> : <span className="icon" />}
                <span>{entry.label}</span>
              </button>
            ),
          )}
        </div>
      )}
    </MenuContext.Provider>
  );
}

export const copyText = (text: string) => {
  navigator.clipboard?.writeText(text);
};

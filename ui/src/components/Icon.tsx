// Glyphs from Segoe Fluent Icons, the icon font Windows 11 uses in its own tools. MDL2 Assets covers Windows 10.
const glyphs = {
  home: "\uE80F",
  cases: "\uEBE8",
  timeline: "\uE81C",
  changes: "\uE895",
  save: "\uE74E",
  open: "\uE8E5",
  back: "\uE72B",
  chevron: "\uE76C",
  shield: "\uEA18",
  monitor: "\uE7F4",
  critical: "\uEA39",
  warning: "\uE7BA",
  ok: "\uE73E",
  info: "\uE946",
  dismiss: "\uE711",
  tool: "\uE8A7",
  hardware: "\uE950",
  driver: "\uE964",
  software: "\uE71D",
  system: "\uE770",
  security: "\uEA18",
  settings: "\uE713",
  power: "\uE7E8",
  background: "\uE7B3",
  pc: "\uE977",
  folder: "\uE8B7",
  app: "\uECAA",
  focus: "\uE71E",
  next: "\uE72A",
  processes: "\uF0E2",
  performance: "\uE9D9",
  cpu: "\uE950",
  memory: "\uE964",
  gpu: "\uF211",
  disk: "\uEDA2",
  network: "\uE968",
  usb: "\uE88E",
  copy: "\uE8C8",
  browser: "\uE774",
  reveal: "\uEC50",
  undo: "\uE7A7",
} as const;

export type IconName = keyof typeof glyphs;

export function Icon({ name, className }: { name: IconName; className?: string }) {
  return (
    <span className={className ? `icon ${className}` : "icon"} aria-hidden="true">
      {glyphs[name]}
    </span>
  );
}

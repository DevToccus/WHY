// Small instruments shared by Monitor and Processes: a ring gauge, a sparkline and the load tone they use.

// Load reads as calm, busy or strained. The tones are status colours, so they also carry an icon-free word nearby.
export const loadTone = (percent: number) => (percent >= 90 ? "strained" : percent >= 70 ? "busy" : "calm");

export function Ring({ value, size = 132, stroke = 10, label, caption }: { value: number; size?: number; stroke?: number; label: string; caption?: string }) {
  const radius = (size - stroke) / 2 - 4;
  const circumference = 2 * Math.PI * radius;
  const clamped = Math.min(100, Math.max(0, value));
  const centre = size / 2;
  // Ticks every 10%, like a dial face.
  const ticks = Array.from({ length: 40 }, (_, index) => {
    const angle = (index / 40) * 2 * Math.PI - Math.PI / 2;
    const outer = radius + stroke / 2 + 4;
    const inner = outer - (index % 4 === 0 ? 4 : 2);
    return { x1: centre + inner * Math.cos(angle), y1: centre + inner * Math.sin(angle), x2: centre + outer * Math.cos(angle), y2: centre + outer * Math.sin(angle) };
  });
  return (
    <svg className={`ring tone-${loadTone(clamped)}`} viewBox={`0 0 ${size} ${size}`} width={size} height={size} role="img" aria-label={`${label} ${clamped.toFixed(0)}%`}>
      {ticks.map((tick, index) => (
        <line key={index} className="ring-tick" {...tick} />
      ))}
      <circle className="ring-track" cx={centre} cy={centre} r={radius} strokeWidth={stroke} />
      <circle
        className="ring-value"
        cx={centre}
        cy={centre}
        r={radius}
        strokeWidth={stroke}
        strokeDasharray={`${(clamped / 100) * circumference} ${circumference}`}
        transform={`rotate(-90 ${centre} ${centre})`}
      />
      <text className="ring-number" x={centre} y={centre + (caption ? 2 : 8)} textAnchor="middle">
        {clamped.toFixed(0)}
        <tspan className="ring-unit">%</tspan>
      </text>
      {caption && (
        <text className="ring-caption" x={centre} y={centre + 22} textAnchor="middle">
          {caption}
        </text>
      )}
    </svg>
  );
}

// The last minute of a value as a filled line. `max` fixes the scale; without it the line uses its own peak.
export function Sparkline({ values, max, className = "", height = 44 }: { values: number[]; max?: number; className?: string; height?: number }) {
  const width = 240;
  const top = Math.max(max ?? 0, ...values, 1);
  const points = values.map((value, index) => {
    const x = values.length > 1 ? (index / (values.length - 1)) * width : width;
    return `${x.toFixed(1)},${(height - (value / top) * (height - 2) - 1).toFixed(1)}`;
  });
  return (
    <svg className={`sparkline ${className}`} viewBox={`0 0 ${width} ${height}`} preserveAspectRatio="none" aria-hidden="true">
      {points.length > 1 && (
        <>
          <polygon className="sparkline-fill" points={`0,${height} ${points.join(" ")} ${width},${height}`} />
          <polyline className="sparkline-line" points={points.join(" ")} />
        </>
      )}
    </svg>
  );
}

export const formatBytes = (bytes: number, digits = 1) => {
  if (bytes >= 1e12) return `${(bytes / 1e12).toFixed(digits)} TB`;
  if (bytes >= 1_073_741_824) return `${(bytes / 1_073_741_824).toFixed(digits)} GB`;
  if (bytes >= 1_048_576) return `${(bytes / 1_048_576).toFixed(digits)} MB`;
  if (bytes >= 1024) return `${(bytes / 1024).toFixed(0)} KB`;
  return `${Math.round(bytes)} B`;
};

export const formatRate = (bytesPerSecond: number) => `${formatBytes(bytesPerSecond)}/s`;

// Network speeds read in bits, as Windows shows them.
export const formatBits = (bytesPerSecond: number) => {
  const bits = bytesPerSecond * 8;
  if (bits >= 1e9) return `${(bits / 1e9).toFixed(1)} Gbps`;
  if (bits >= 1e6) return `${(bits / 1e6).toFixed(1)} Mbps`;
  if (bits >= 1e3) return `${(bits / 1e3).toFixed(0)} Kbps`;
  return `${Math.round(bits)} bps`;
};

export function formatUptime(ms: number) {
  const minutes = Math.floor(ms / 60_000);
  const days = Math.floor(minutes / 1440);
  const hours = Math.floor((minutes % 1440) / 60);
  return days > 0 ? `${days}d ${hours}h ${minutes % 60}m` : `${hours}h ${minutes % 60}m`;
}

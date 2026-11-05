import { Icon, type IconName } from "../components/Icon";
import { formatBits, formatBytes, formatRate, formatUptime, loadTone, Ring, Sparkline } from "../components/Gauge";
import { busiestDisk, useSystemStats } from "../stats";

const toneWords = { calm: "Normal", busy: "Busy", strained: "Under strain" } as const;

// One of the four figures that matter most: a ring, the number in words, two supporting facts and the last minute.
function PrimaryCard({
  icon,
  title,
  detail,
  value,
  caption,
  facts,
  history,
}: {
  icon: IconName;
  title: string;
  detail: string;
  value: number;
  caption?: string;
  facts: [string, string][];
  history: number[];
}) {
  const tone = loadTone(value);
  return (
    <section className={`card monitor-primary tone-${tone}`} aria-label={title}>
      <header className="monitor-card-head">
        <Icon name={icon} className="monitor-icon" />
        <div>
          <h2>{title}</h2>
          <span className="muted" title={detail}>
            {detail}
          </span>
        </div>
        <span className={`load-chip load-${tone}`}>{toneWords[tone]}</span>
      </header>
      <div className="monitor-primary-body">
        <Ring value={value} label={title} caption={caption} />
        <dl className="facts">
          {facts.map(([label, text]) => (
            <div key={label}>
              <dt className="muted">{label}</dt>
              <dd>{text}</dd>
            </div>
          ))}
        </dl>
      </div>
      <Sparkline values={history} max={100} className={`spark-${tone}`} />
    </section>
  );
}

export function MonitorScreen() {
  const { stats, history } = useSystemStats();

  if (!stats) {
    return (
      <>
        <h1>Monitor</h1>
        <p className="muted">Reading the PC's counters.</p>
      </>
    );
  }

  const { cpu, memory, gpu } = stats;
  const disk = busiestDisk(stats);
  const received = stats.network.reduce((sum, adapter) => sum + adapter.received, 0);
  const sent = stats.network.reduce((sum, adapter) => sum + adapter.sent, 0);
  const gpuName = gpu.adapters[0]?.name ?? "No graphics adapter found";
  const gpuMemory = gpu.adapters[0]?.dedicated ?? 0;
  const diskRead = stats.disks.reduce((sum, item) => sum + item.read, 0);
  const diskWrite = stats.disks.reduce((sum, item) => sum + item.write, 0);

  return (
    <>
      <div className="monitor-title">
        <h1>Monitor</h1>
        <span className="live-dot" aria-hidden="true" />
        <span className="muted">Live, every second. Up for {formatUptime(stats.uptimeMs)}.</span>
      </div>

      {/* The four that decide whether the PC feels slow come first and largest. */}
      <div className="monitor-primary-grid">
        <PrimaryCard
          icon="cpu"
          title="Processor"
          detail={cpu.name}
          value={cpu.usage}
          caption={`${(cpu.currentMhz / 1000).toFixed(2)} GHz`}
          facts={[
            ["Speed", `${(cpu.currentMhz / 1000).toFixed(2)} GHz (base ${(cpu.baseMhz / 1000).toFixed(2)})`],
            ["Cores", `${cpu.physical} cores, ${cpu.logical} threads`],
          ]}
          history={history.cpu}
        />
        <PrimaryCard
          icon="memory"
          title="Memory"
          detail={`${formatBytes(memory.total, 0)} installed`}
          value={memory.load}
          caption={formatBytes(memory.used)}
          facts={[
            ["In use", `${formatBytes(memory.used)} of ${formatBytes(memory.total, 0)}`],
            ["Committed", `${formatBytes(memory.committed)} of ${formatBytes(memory.commitLimit, 0)}`],
          ]}
          history={history.memory}
        />
        <PrimaryCard
          icon="gpu"
          title="Graphics"
          detail={gpuName}
          value={gpu.usage}
          facts={[
            ["Dedicated memory", gpuMemory ? `${formatBytes(gpu.memoryUsed)} of ${formatBytes(gpuMemory, 0)}` : formatBytes(gpu.memoryUsed)],
            ["Adapters", String(gpu.adapters.length)],
          ]}
          history={history.gpu}
        />
        <PrimaryCard
          icon="disk"
          title="Disk"
          detail={`${stats.disks.length} physical ${stats.disks.length === 1 ? "disk" : "disks"}, busiest shown`}
          value={disk}
          facts={[
            ["Read", formatRate(diskRead)],
            ["Write", formatRate(diskWrite)],
          ]}
          history={history.disk}
        />
      </div>

      {/* Second tier: what moves but rarely decides. */}
      <div className="monitor-secondary-grid">
        <section className="card monitor-network" aria-label="Network">
          <header className="monitor-card-head">
            <Icon name="network" className="monitor-icon" />
            <div>
              <h2>Network</h2>
              <span className="muted">{stats.network[0]?.name ?? "No connected adapter"}</span>
            </div>
          </header>
          <div className="network-figures">
            <div>
              <span className="muted">Receiving</span>
              <strong className="figure-down">{formatBits(received)}</strong>
            </div>
            <div>
              <span className="muted">Sending</span>
              <strong className="figure-up">{formatBits(sent)}</strong>
            </div>
          </div>
          <div className="network-graphs">
            <Sparkline values={history.received} className="spark-down" height={56} />
            <Sparkline values={history.sent} max={Math.max(...history.received, 1)} className="spark-up" height={56} />
          </div>
        </section>

        <section className="card monitor-system" aria-label="System">
          <header className="monitor-card-head">
            <Icon name="system" className="monitor-icon" />
            <div>
              <h2>System</h2>
              <span className="muted">Counts across Windows</span>
            </div>
          </header>
          <dl className="system-figures">
            <div>
              <dt className="muted">Up time</dt>
              <dd>{formatUptime(stats.uptimeMs)}</dd>
            </div>
            <div>
              <dt className="muted">Processes</dt>
              <dd>{stats.processes.toLocaleString()}</dd>
            </div>
            <div>
              <dt className="muted">Threads</dt>
              <dd>{stats.threads.toLocaleString()}</dd>
            </div>
            <div>
              <dt className="muted">Handles</dt>
              <dd>{stats.handles.toLocaleString()}</dd>
            </div>
            <div>
              <dt className="muted">Power</dt>
              <dd>{stats.power.battery ? `${stats.power.percent}%${stats.power.pluggedIn ? ", charging" : ""}` : "Mains"}</dd>
            </div>
            <div>
              <dt className="muted">Cached</dt>
              <dd>{formatBytes(memory.cached)}</dd>
            </div>
          </dl>
        </section>
      </div>

      {/* Details: for when something above looks wrong. */}
      <section>
        <h2 className="section-title">Storage</h2>
        <ul className="rows">
          {stats.volumes.map((volume) => {
            const used = volume.total - volume.free;
            const percent = volume.total ? (used / volume.total) * 100 : 0;
            const activity = stats.disks.find((item) => item.name.toUpperCase().includes(volume.root.slice(0, 2).toUpperCase()));
            return (
              <li key={volume.root} className="row static volume">
                <span className="severity-icon">
                  <Icon name={volume.removable ? "usb" : "disk"} />
                </span>
                <span className="row-main">
                  <strong>
                    {volume.label || "Local disk"} ({volume.root.replace("\\", "")})
                  </strong>
                  <span className={`space-bar space-${percent >= 90 ? "strained" : percent >= 75 ? "busy" : "calm"}`}>
                    <span style={{ width: `${percent}%` }} />
                  </span>
                  <span className="muted">
                    {formatBytes(volume.free)} free of {formatBytes(volume.total, 0)}, {volume.fileSystem}
                    {activity && ` · ${activity.active.toFixed(0)}% active, ${formatRate(activity.read + activity.write)}`}
                  </span>
                </span>
                <span className="row-side volume-percent">{percent.toFixed(0)}%</span>
              </li>
            );
          })}
        </ul>
      </section>

    </>
  );
}

import { useEffect, useMemo, useRef, useState } from "react";
import { SerialService, type IncomingMessage } from "./serialService";
import type { DeviceStatus, MapPayload } from "./types";

const AXIS_NAMES = ["X", "Y", "Z", "RZ", "RX", "RY"] as const;

function clamp(v: number, min: number, max: number): number {
  return Math.max(min, Math.min(max, v));
}

function metricTone(value: number, warnAt: number, dangerAt: number): "ok" | "warn" | "danger" {
  if (value >= warnAt) return "ok";
  if (value >= dangerAt) return "warn";
  return "danger";
}

export function App() {
  const serialRef = useRef<SerialService>(new SerialService());
  const [connected, setConnected] = useState(false);
  const [status, setStatus] = useState<DeviceStatus | null>(null);
  const [channels, setChannels] = useState<number[]>(new Array(16).fill(992));
  const [axisMap, setAxisMap] = useState<number[]>([0, 1, 4, 5, 3, 2]);
  const [buttonMap, setButtonMap] = useState<Array<{ idx: number; ch: number; th: number }>>(
    Array.from({ length: 16 }, (_, i) => ({ idx: i, ch: 6 + (i % 10), th: 1500 }))
  );
  const [logs, setLogs] = useState<string[]>([]);
  const [demoMode, setDemoMode] = useState(false);
  const [autoscrollLogs, setAutoscrollLogs] = useState(true);
  const demoStartRef = useRef<number>(Date.now());
  const logsRef = useRef<HTMLPreElement | null>(null);

  const appendLog = (line: string) => {
    setLogs((prev) => [...prev.slice(-80), line]);
  };

  const onMessage = (msg: IncomingMessage) => {
    if (typeof msg.type !== "string") {
      appendLog(JSON.stringify(msg));
      return;
    }
    if (msg.type === "status") {
      const parsed = msg as DeviceStatus;
      setStatus(parsed);
      if (parsed.channels) setChannels(parsed.channels);
      return;
    }
    if (msg.type === "map") {
      const parsed = msg as MapPayload;
      setAxisMap(parsed.axes.slice(0, 6));
      setButtonMap(parsed.buttons.slice(0, 16));
      return;
    }
    appendLog(JSON.stringify(msg));
  };

  const connect = async () => {
    try {
      await serialRef.current.connect(115200);
      setConnected(true);
      appendLog("Connected");
      void serialRef.current.startReadLoop(onMessage);
      await serialRef.current.send("app get proto");
      await serialRef.current.send("app get map");
      await serialRef.current.send("app sub telemetry 100");
    } catch (err) {
      appendLog(`Connect error: ${(err as Error).message}`);
    }
  };

  const disconnect = async () => {
    try {
      if (serialRef.current.isConnected()) {
        await serialRef.current.send("app unsub");
        await serialRef.current.disconnect();
      }
      setConnected(false);
      appendLog("Disconnected");
    } catch (err) {
      appendLog(`Disconnect error: ${(err as Error).message}`);
    }
  };

  const toggleDemoMode = () => {
    setDemoMode((prev) => {
      const next = !prev;
      if (next) {
        demoStartRef.current = Date.now();
        setConnected(false);
        appendLog("Demo mode enabled.");
      } else {
        appendLog("Demo mode disabled.");
      }
      return next;
    });
  };

  const requestChannels = async () => {
    await serialRef.current.send("app get channels");
  };

  const applyMap = async () => {
    try {
      for (let i = 0; i < axisMap.length; i++) {
        await serialRef.current.send(`app set axis ${i} ${clamp(axisMap[i], 0, 15)}`);
      }
      for (const b of buttonMap) {
        await serialRef.current.send(
          `app set button ${b.idx} ${clamp(b.ch, 0, 15)} ${clamp(b.th, 900, 1900)}`
        );
      }
      await serialRef.current.send("app get map");
      appendLog("Mapping applied.");
    } catch (err) {
      appendLog(`Apply map error: ${(err as Error).message}`);
    }
  };

  const axisRows = useMemo(
    () =>
      AXIS_NAMES.map((name, i) => (
        <div className="row" key={name}>
          <span>{name}</span>
          <input
            type="number"
            min={0}
            max={15}
            value={axisMap[i] ?? 0}
            onChange={(e) => {
              const next = [...axisMap];
              next[i] = Number(e.target.value);
              setAxisMap(next);
            }}
          />
        </div>
      )),
    [axisMap]
  );

  useEffect(() => {
    if (!demoMode) return;
    const id = window.setInterval(() => {
      const t = Date.now() - demoStartRef.current;
      const wave = (phase: number, amp: number) =>
        Math.round(992 + Math.sin((t + phase) / 420) * amp);

      const demoChannels = Array.from({ length: 16 }, (_, i) => {
        if (i < 6) return clamp(wave(i * 160, 700 - i * 60), 172, 1811);
        if (i < 10) return Math.sin((t + i * 200) / 900) > 0 ? 1800 : 200;
        return Math.sin((t + i * 120) / 1300) > 0 ? 1600 : 900;
      });

      const pkt = 250 + Math.round(Math.sin(t / 1800) * 6);
      const age = 2 + Math.round((Math.sin(t / 600) + 1) * 2);
      const rfLq = clamp(95 + Math.round(Math.sin(t / 2500) * 4), 75, 100);
      const quality = clamp(rfLq - Math.round(age * 1.5), 0, 100);
      const uptime = t;

      setChannels(demoChannels);
      setStatus({
        type: "status",
        proto: "1.0-demo",
        uptime_ms: uptime,
        mode: "gamepad",
        link_active: true,
        packet_rate_hz: pkt,
        last_packet_age_ms: age,
        link_quality_pct: quality,
        rf_stats_valid: true,
        rf_lq_pct: rfLq,
        rf_uplink_rssi_dbm: -58 - Math.round(Math.sin(t / 1700) * 5),
        rf_uplink_snr_db: 9 + Math.round(Math.sin(t / 2100) * 3),
        rf_stats_age_ms: age,
        smoothing: true,
        cutoff_hz: 45,
        deadband: 4,
        loop0_hz: 980 + Math.round(Math.sin(t / 2000) * 20),
        loop1_hz: 920 + Math.round(Math.sin(t / 2300) * 18),
        process_avg_us: 36 + Math.round(Math.sin(t / 1500) * 8),
        process_p95_us: 62 + Math.round(Math.sin(t / 1300) * 10),
        process_max_us: 90 + Math.round(Math.sin(t / 1600) * 12),
        transport_overflow: 0,
        transport_max_buffered: 14,
        mem_free: 191000 + Math.round(Math.sin(t / 2600) * 1200),
        channels: demoChannels
      });
    }, 100);
    return () => window.clearInterval(id);
  }, [demoMode]);

  useEffect(() => {
    if (!autoscrollLogs || !logsRef.current) return;
    logsRef.current.scrollTop = logsRef.current.scrollHeight;
  }, [logs, autoscrollLogs]);

  return (
    <main>
      <h1>
        RP2040 CRSF Companion {demoMode && <span className="badgeDemo">DEMO</span>}
      </h1>
      <section className="statusBar">
        <span className={`pill ${connected ? "pillOk" : "pillMuted"}`}>
          {connected ? "Connected" : "Disconnected"}
        </span>
        <span className={`pill ${demoMode ? "pillWarn" : "pillMuted"}`}>
          Demo: {demoMode ? "ON" : "OFF"}
        </span>
        <span className={`pill ${status?.link_active ? "pillOk" : "pillDanger"}`}>
          Link: {status?.link_active ? "UP" : "DOWN"}
        </span>
        <span className="pill pillMuted">Mode: {status?.mode ?? "-"}</span>
      </section>
      {!("serial" in navigator) && (
        <p className="warn">
          Web Serial API не підтримується у цьому браузері. Використай Chrome/Edge.
        </p>
      )}

      <section className="card">
        <h2>Device</h2>
        <div className="actions">
          <button onClick={connect} disabled={connected || demoMode}>
            Connect
          </button>
          <button onClick={disconnect} disabled={!connected}>
            Disconnect
          </button>
          <button onClick={requestChannels} disabled={!connected}>
            Refresh channels
          </button>
          <button onClick={toggleDemoMode}>{demoMode ? "Stop demo" : "Start demo"}</button>
        </div>
        <p>Connected: {connected ? "Yes" : "No"}</p>
        <p>Demo mode: {demoMode ? "On" : "Off"}</p>
        <p>Uptime: {status ? `${Math.floor(status.uptime_ms / 1000)} s` : "-"}</p>
      </section>

      <section className="card">
        <h2>Telemetry</h2>
        <div className="metricsGrid">
          <div className={`metricCard ${metricTone(status?.link_quality_pct ?? 0, 80, 60)}`}>
            <div className="metricLabel">Link quality</div>
            <div className="metricValue">{status?.link_quality_pct ?? 0}%</div>
          </div>
          <div className={`metricCard ${metricTone(status?.rf_lq_pct ?? 0, 80, 60)}`}>
            <div className="metricLabel">RF LQ</div>
            <div className="metricValue">{status?.rf_lq_pct ?? 0}%</div>
          </div>
          <div className="metricCard neutral">
            <div className="metricLabel">RF RSSI</div>
            <div className="metricValue">{status?.rf_uplink_rssi_dbm ?? 0} dBm</div>
          </div>
          <div className="metricCard neutral">
            <div className="metricLabel">RF SNR</div>
            <div className="metricValue">{status?.rf_uplink_snr_db ?? 0} dB</div>
          </div>
          <div className="metricCard neutral">
            <div className="metricLabel">Packet rate</div>
            <div className="metricValue">{status?.packet_rate_hz ?? 0} Hz</div>
          </div>
          <div
            className={`metricCard ${
              (status?.last_packet_age_ms ?? 1000) < 20
                ? "ok"
                : (status?.last_packet_age_ms ?? 1000) < 60
                  ? "warn"
                  : "danger"
            }`}
          >
            <div className="metricLabel">Last packet age</div>
            <div className="metricValue">{status?.last_packet_age_ms ?? 0} ms</div>
          </div>
          <div className="metricCard neutral">
            <div className="metricLabel">Process avg / p95</div>
            <div className="metricValue">
              {status?.process_avg_us ?? 0}/{status?.process_p95_us ?? 0} us
            </div>
          </div>
          <div className="metricCard neutral">
            <div className="metricLabel">Core loops</div>
            <div className="metricValue">
              {status?.loop0_hz ?? 0}/{status?.loop1_hz ?? 0} Hz
            </div>
          </div>
          <div className="metricCard neutral">
            <div className="metricLabel">Transport overflow</div>
            <div className="metricValue">{status?.transport_overflow ?? 0}</div>
          </div>
          <div className="metricCard neutral">
            <div className="metricLabel">Free mem</div>
            <div className="metricValue">{status?.mem_free ?? 0} bytes</div>
          </div>
          <div className="metricCard neutral">
            <div className="metricLabel">RF stats valid</div>
            <div className="metricValue">{status?.rf_stats_valid ? "Yes" : "No"}</div>
          </div>
          <div className="metricCard neutral">
            <div className="metricLabel">RF stats age</div>
            <div className="metricValue">{status?.rf_stats_age_ms ?? 0} ms</div>
          </div>
        </div>
      </section>

      <section className="card">
        <h2>Channels</h2>
        <div className="channels">
          {channels.map((ch, i) => {
            const pct = ((clamp(ch, 172, 1811) - 172) / (1811 - 172)) * 100;
            return (
              <div key={i} className="channelLine">
                <label>CH{i + 1}</label>
                <progress value={pct} max={100} />
                <span>{ch}</span>
              </div>
            );
          })}
        </div>
      </section>

      <section className="card">
        <h2>Mapping</h2>
        <div className="mappingCols">
          <div>
            <h3>Axes</h3>
            {axisRows}
          </div>
          <div>
            <h3>Buttons</h3>
            <div className="buttonsTable">
              {buttonMap.map((b, idx) => (
                <div className="row" key={b.idx}>
                  <span>B{b.idx + 1}</span>
                  <input
                    type="number"
                    min={0}
                    max={15}
                    value={b.ch}
                    onChange={(e) => {
                      const next = [...buttonMap];
                      next[idx] = { ...next[idx], ch: Number(e.target.value) };
                      setButtonMap(next);
                    }}
                  />
                  <input
                    type="number"
                    min={900}
                    max={1900}
                    value={b.th}
                    onChange={(e) => {
                      const next = [...buttonMap];
                      next[idx] = { ...next[idx], th: Number(e.target.value) };
                      setButtonMap(next);
                    }}
                  />
                </div>
              ))}
            </div>
          </div>
        </div>
        <div className="actions">
          <button onClick={applyMap} disabled={!connected || demoMode}>
            Apply mapping
          </button>
        </div>
      </section>

      <section className="card">
        <div className="logsHeader">
          <h2>Logs</h2>
          <label className="checkbox">
            <input
              type="checkbox"
              checked={autoscrollLogs}
              onChange={(e) => setAutoscrollLogs(e.target.checked)}
            />
            Autoscroll
          </label>
        </div>
        <pre ref={logsRef}>{logs.join("\n")}</pre>
      </section>
    </main>
  );
}

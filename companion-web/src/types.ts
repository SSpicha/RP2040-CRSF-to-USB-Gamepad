export type DeviceMode = "gamepad" | "passthrough";

export interface ButtonMapping {
  idx: number;
  ch: number;
  th: number;
}

export interface DeviceStatus {
  type: "status";
  proto: string;
  uptime_ms: number;
  mode: DeviceMode;
  link_active: boolean;
  packet_rate_hz: number;
  last_packet_age_ms: number;
  link_quality_pct: number;
  rf_stats_valid: boolean;
  rf_lq_pct: number;
  rf_uplink_rssi_dbm: number;
  rf_uplink_snr_db: number;
  rf_stats_age_ms: number;
  smoothing: boolean;
  cutoff_hz: number;
  deadband: number;
  loop0_hz: number;
  loop1_hz: number;
  process_avg_us: number;
  process_p95_us: number;
  process_max_us: number;
  transport_overflow: number;
  transport_max_buffered: number;
  mem_free: number;
  channels?: number[];
}

export interface MapPayload {
  type: "map";
  axes: number[];
  buttons: ButtonMapping[];
}

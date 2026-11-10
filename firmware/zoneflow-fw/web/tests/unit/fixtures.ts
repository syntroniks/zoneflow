// A complete, healthy status snapshot (every required field), for tests
// that change a few fields.
import type { Status } from '../../src/api/client.ts';

export function baseStatus(): Status {
  return {
    sequence: 1,
    utc: 1781500320,
    time_status: 'synced',
    uptime_s: 100,
    zones: [],
    master: { enabled: false, on: false, locked_out: false, fault_count: 0 },
    queue: { state: 'idle', length: 0, current_remaining_s: 0, items: [], pause_timeout_min: 120, paused_s: 0, stop_in_s: null },
    sensors: {
      rain: { enabled: false, active: false },
      spare: { enabled: false, active: false },
      flow: { enabled: false, rate_lpm: 0, total_l: 0 },
      ac_present: true,
      valve_current_ma: 0,
      current_sense_available: true,
      over_current_tripped: false,
    },
    rain_delay: { active: false, until_utc: null, remaining_s: 0 },
    inhibit: { sensor: false, no_ac: false, maintenance: false, master_lockout: false },
    faults: { active: [], zones: [] },
    maintenance: false,
    config_revision: 1,
    network: {
      state: 'connected', provisioned: true, provisioning: 'none', hostname: 'zoneflow-ab12', mac: '', ssid: '', bssid: '', channel: 1,
      rssi: -60, ip: '', netmask: '', gateway: '', reconnect_attempt: 0, retry_in_ms: 0, last_disconnect_reason: 0, disconnects: 0, mdns: true,
    },
    ota: { phase: 'idle', error: 'none', source: 'none', forced: false, received_bytes: 0, total_bytes: 0, new_version: '' },
  };
}

export function zoneStatus(id: number, patch: Partial<Status['zones'][number]> = {}): Status['zones'][number] {
  return {
    id, name: '', enabled: true, max_run_s: 3600, default_run_s: 600, current_min_ma: 80, current_max_ma: 700, state: 'off',
    remaining_s: 0, last_stop: 'none', fault: 'none', locked_out: false, resting: false, rest_remaining_s: 0, learned_current_ma: null,
    ...patch,
  };
}

// Simulated ZoneFlow controller for UI development, demos and end-to-end
// tests. It follows docs/firmware/openapi.json and the behaviour described
// in docs/firmware/api.md closely enough to drive every screen. It is not
// the firmware: no scheduler runs programs on their own, and timing is
// simplified (no inter-zone delay, no master sequencing).
import { TimeZone } from '../src/lib/tz.ts';

export type Json = Record<string, unknown>;

export const SETUP_CODE = 'K7Q2M9XW4R';
const HARD_MAX_S = 4 * 3600;

interface ZoneRt {
  state: 'off' | 'waiting' | 'open';
  openedMs: number;
  plannedS: number;
  source: string;
  programId: number;
  learnedMa: number;
  remainingMs: number;
  lastStop: string;
  fault: string;
  lockedOut: boolean;
  onMs: number;
  restMs: number;
}
interface Entry {
  id: number;
  zone: number; // 0-based
  durationSec: number;
  remainingMs: number;
  source: 'schedule' | 'manual_program' | 'manual_zone';
  programId: number;
}

export function defaultConfig(): Json {
  return {
    schema: 2,
    device_name: 'zoneflow',
    timezone: 'UTC0',
    zone_count: 8,
    zones: Array.from({ length: 8 }, () => ({ name: '', enabled: true, max_run_s: 3600, current_min_ma: 80, current_max_ma: 700, default_run_s: 600 })),
    master: { enabled: false, pre_open_ms: 1000, post_close_ms: 2000, fault_lockout_count: 3, current_min_ma: 80, current_max_ma: 700 },
    driver: { max_concurrent: 2, inter_zone_delay_ms: 2000, min_rest_s: 1800, zero_cross_switching: false },
    sensors: {
      rain: { enabled: false, contact: 'nc', debounce_ms: 2000 },
      spare: { enabled: false, contact: 'no', debounce_ms: 200, function: 'none' },
      flow: { enabled: false, pulses_per_litre: 450, leak_threshold_lpm: 0.5, leak_detect_s: 60 },
    },
    diagnostics: { current_enabled: true, settle_ms: 500, idle_max_ma: 60, short_circuit_ma: 3000, confirm_samples: 3 },
    schedule: { seasonal_percent: 100, missed_start_grace_min: 60, pause_timeout_min: 120, programs: [] },
    mqtt: {
      enabled: false, host: '', port: 1883, username: '', tls: false, ca: 'bundle', client_id: '', base_topic: '',
      discovery: true, discovery_prefix: 'homeassistant', ha_zone_entity: 'valve',
    },
  };
}

export function demoConfig(): Json {
  const c = defaultConfig();
  const names = ['Front lawn', 'Back lawn', 'Vegetable beds', 'Roses along north fence', 'Drip: pots', 'Orchard'];
  c['device_name'] = 'Garden';
  c['timezone'] = 'CET-1CEST,M3.5.0,M10.5.0/3';
  c['zone_count'] = 6;
  c['zones'] = Array.from({ length: 8 }, (_, i) => ({
    name: names[i] ?? '', enabled: i !== 5, max_run_s: i === 4 ? 1800 : 3600, current_min_ma: 80, current_max_ma: 700,
    default_run_s: [900, 1200, 600, 600, 300, 600, 600, 600][i],
  }));
  c['mqtt'] = { ...(c['mqtt'] as Json), enabled: true, host: '192.168.1.10', username: 'zoneflow' };
  (c['master'] as Json)['enabled'] = true;
  c['sensors'] = {
    rain: { enabled: true, contact: 'nc', debounce_ms: 2000 },
    spare: { enabled: false, contact: 'no', debounce_ms: 200, function: 'none' },
    flow: { enabled: true, pulses_per_litre: 450, leak_threshold_lpm: 0.5, leak_detect_s: 60 },
  };
  c['schedule'] = {
    seasonal_percent: 80,
    missed_start_grace_min: 60,
    pause_timeout_min: 120,
    programs: [
      { id: 1, name: 'Lawns, early', enabled: true, starts: ['05:30'], days: { type: 'weekdays', weekdays: ['mon', 'wed', 'fri', 'sun'] }, durations_s: [1200, 1500, 0, 0, 0, 0, 0, 0], seasonal: true, obey_sensor: true },
      { id: 2, name: 'Beds and roses', enabled: true, starts: ['06:15', '19:30'], days: { type: 'interval', every: 2, anchor: '2026-06-01' }, durations_s: [0, 0, 900, 600, 300, 0, 0, 0], seasonal: true, obey_sensor: true },
      { id: 3, name: 'Pots, hot days', enabled: false, starts: ['13:00'], days: { type: 'weekdays' }, durations_s: [0, 0, 0, 0, 240, 0, 0, 0], seasonal: false, obey_sensor: false },
    ],
  };
  return c;
}

export interface DeviceOptions {
  scenario: 'fresh' | 'demo';
  /** Fixed controller time (Unix s); with frozen, the clock does not advance. */
  utc?: number;
  frozen?: boolean;
  /** Simulated seconds per real second. */
  speed?: number;
}

export type Listener = (event: string, data: Json) => void;

export class Device {
  config: Json = defaultConfig();
  passwordHash: string | null = null;
  sessions = new Map<string, { csrf: string; created: number }>();
  security = { protect_reads: false, https: false, check_host: true, restart_required: false };
  zones: ZoneRt[] = [];
  queue: Entry[] = [];
  queueState: 'idle' | 'running' | 'paused' = 'idle';
  nextId = 1;
  rainDelayUntil = 0;
  faults = new Set<string>();
  master = { on: false, lockedOut: false, faultCount: 0 };
  sensors = { rainActive: false, spareActive: false, flowLpm: 0, flowTotal: 1234.5, ac: true, currentMa: 0, senseOk: true };
  maintenance = false;
  timeStatus: 'unset' | 'rtc' | 'synced' = 'synced';
  ota: Json = { phase: 'idle', error: 'none', source: 'none', forced: false, received_bytes: 0, total_bytes: 0, new_version: '' };
  network: Json = {};
  sequence = 1;
  bootMono = 0;
  /** Verdict override for the next run command (tests): 'reject:<code>' answers unconfirmed then rejects on the stream. */
  nextRunVerdict: string | null = null;
  /** While true the stream sends nothing (tests the "connection lost" state). */
  streamMuted = false;
  private utcBase = 0;
  private monoBase = 0;
  private frozen = false;
  private speed = 1;
  private listeners = new Set<Listener>();
  private lastTick = 0;

  constructor(opts: DeviceOptions) {
    this.reset(opts);
  }

  reset(opts: DeviceOptions): void {
    this.speed = opts.speed ?? 1;
    this.frozen = !!opts.frozen;
    this.utcBase = opts.utc ?? Math.floor(Date.now() / 1000);
    this.monoBase = Date.now();
    this.lastTick = this.monoMs();
    this.bootMono = this.monoMs();
    this.config = opts.scenario === 'demo' ? demoConfig() : defaultConfig();
    this.passwordHash = opts.scenario === 'demo' ? 'zoneflow' : null;
    this.sessions.clear();
    this.security = { protect_reads: false, https: false, check_host: true, restart_required: false };
    const demo = opts.scenario === 'demo';
    this.zones = Array.from({ length: 8 }, (_, i) => ({
      state: 'off', remainingMs: 0, lastStop: 'none', fault: 'none', lockedOut: false, onMs: 0, restMs: 0, openedMs: 0, plannedS: 0,
      source: 'manual_zone', programId: 0, learnedMa: demo && i < 5 ? 240 + i * 15 : 0,
    }));
    this.setupComplete = demo;
    this.mqttSecrets = { password: demo ? 'secret' : '', ca: '' };
    this.mqttChangedMono = this.monoMs() - 3 * 3600 * 1000;
    this.mqttCounters = { published: demo ? 1843 : 0, commands: demo ? 12 : 0 };
    this.history = [];
    this.historyId = 0;
    if (demo) this.seedHistory();
    this.queue = [];
    this.queueState = 'idle';
    this.rainDelayUntil = 0;
    this.faults.clear();
    this.master = { on: false, lockedOut: false, faultCount: 0 };
    this.sensors = { rainActive: false, spareActive: false, flowLpm: 0, flowTotal: 1234.5, ac: true, currentMa: 0, senseOk: true };
    this.maintenance = false;
    this.timeStatus = 'synced';
    this.ota = { phase: 'idle', error: 'none', source: 'none', forced: false, received_bytes: 0, total_bytes: 0, new_version: '' };
    this.network = {
      state: 'connected', provisioned: true, provisioning: 'none', hostname: 'zoneflow-ab12', mac: '24:0a:c4:12:ab:12', ssid: 'Garden WiFi',
      bssid: '9c:53:22:01:02:03', channel: 6, rssi: -61, ip: '192.168.1.47', netmask: '255.255.255.0', gateway: '192.168.1.1',
      reconnect_attempt: 0, retry_in_ms: 0, last_disconnect_reason: 0, disconnects: 0, mdns: true,
    };
    this.nextRunVerdict = null;
    this.streamMuted = false;
    this.changed();
  }

  // ---- MQTT (simulated client) ------------------------------------------------

  setupComplete = false;
  mqttSecrets = { password: '', ca: '' };
  mqttChangedMono = 0;
  mqttCounters = { published: 0, commands: 0 };

  mqttJson(): Json {
    const m = this.config['mqtt'] as Json;
    const sinceMs = this.frozen ? 3 * 3600 * 1000 : this.monoMs() - this.mqttChangedMono;
    const host = String(m['host'] ?? '');
    const fail = host === '' || /fail|invalid/.test(host) || (m['tls'] === true && m['ca'] === 'custom' && !this.mqttSecrets.ca);
    let state = 'disabled';
    if (m['enabled'] === true) state = fail ? 'waiting_retry' : sinceMs < 2000 ? 'connecting' : 'connected';
    const id = '240ac412ab12';
    const root = String(m['base_topic'] || `zoneflow/${id}`);
    return {
      ...m,
      password_set: this.mqttSecrets.password !== '',
      ca_cert_set: this.mqttSecrets.ca !== '',
      status: {
        state,
        broker: m['enabled'] ? `${m['tls'] ? 'mqtts' : 'mqtt'}://${host}:${String(m['port'])}` : '',
        root_topic: root,
        connected_for_s: state === 'connected' ? Math.floor((sinceMs - 2000) / 1000) : null,
        connects: state === 'connected' ? 1 : 0,
        reconnect_attempt: state === 'waiting_retry' ? 3 : 0,
        retry_in_ms: state === 'waiting_retry' ? 8000 : 0,
        last_error: state === 'waiting_retry' ? (host ? 'connection refused' : 'no broker address') : '',
        published: state === 'connected' ? this.mqttCounters.published : 0,
        publish_failed: 0,
        commands_received: this.mqttCounters.commands,
        commands_rejected: 0,
        events_dropped: 0,
        discovery_published: state === 'connected' && m['discovery'] === true,
      },
    };
  }

  // ---- time ------------------------------------------------------------

  monoMs(): number {
    return this.frozen ? 0 : (Date.now() - this.monoBase) * this.speed;
  }
  utc(): number {
    return this.utcBase + Math.floor(this.monoMs() / 1000);
  }
  setClock(utc: number, frozen?: boolean): void {
    this.utcBase = utc - Math.floor(this.monoMs() / 1000);
    if (frozen !== undefined) {
      this.utcBase = utc;
      this.frozen = frozen;
      this.monoBase = Date.now();
      this.lastTick = this.monoMs();
    }
    this.changed();
  }
  tz(): TimeZone {
    return TimeZone.parse(String(this.config['timezone'])) ?? TimeZone.utc();
  }

  // ---- events ------------------------------------------------------------

  subscribe(fn: Listener): () => void {
    this.listeners.add(fn);
    return () => this.listeners.delete(fn);
  }
  get streamCount(): number {
    return this.listeners.size;
  }
  emit(type: string, fields: Json = {}): void {
    const data = { type, utc: this.utc(), mono_ms: Math.floor(this.monoMs()), ...fields };
    for (const l of this.listeners) l('event', data);
    this.record(type, data);
  }

  // ---- history (as zf_core historyFromEvent and zf_web historyRecordToJson) ----

  history: Json[] = [];
  private historyId = 0;
  static readonly HISTORY_CAPACITY = 1200;

  addHistory(rec: Json): void {
    const utc = Number(rec['utc'] ?? this.utc());
    this.history.push({ id: ++this.historyId, utc, time_valid: utc >= 1735689600, ...rec });
    if (this.history.length > Device.HISTORY_CAPACITY) this.history.splice(0, 50); // a chunk at a time
  }

  private record(type: string, d: Json): void {
    const pick = (...keys: string[]) => Object.fromEntries(keys.map((k) => [k, d[k] ?? null]));
    const base = { utc: d['utc'] };
    switch (type) {
      case 'run_ended':
        return this.addHistory({ ...base, type: 'run', ...pick('zone', 'program_id', 'source', 'end', 'planned_s', 'actual_s', 'current_ma', 'litres') });
      case 'program_started':
        return this.addHistory({ ...base, type, ...pick('program_id', 'source', 'late') });
      case 'program_skipped':
        return this.addHistory({ ...base, type, ...pick('program_id', 'reason') });
      case 'program_missed':
        return this.addHistory({ ...base, type, ...pick('program_id', 'scheduled_utc') });
      case 'run_dropped':
        return this.addHistory({ ...base, type, ...pick('zone', 'program_id', 'source', 'reason') });
      case 'fault_raised':
      case 'fault_cleared':
        return this.addHistory({ ...base, type, ...pick('zone', 'code') });
      case 'rain_delay_changed':
        return this.addHistory({ ...base, type: 'rain_delay', until_utc: d['until_utc'] ?? null });
      case 'sensor_changed':
        return this.addHistory({ ...base, type: 'sensor', ...pick('sensor', 'active') });
      case 'maintenance_changed':
        return this.addHistory({ ...base, type: 'maintenance', on: d['on'] });
      case 'pause_expired':
        return this.addHistory({ ...base, type });
      case 'config_applied':
        return this.addHistory({ ...base, type: 'config_changed' });
      case 'time_status_changed':
        return this.addHistory({ ...base, type: 'time_set', status: d['status'] });
    }
  }

  historyPage(q: { limit: number; before?: number; after?: number; since?: number; zone?: number }): Json {
    const match = this.history.filter(
      (r) =>
        (q.before === undefined || Number(r['id']) < q.before) &&
        (q.after === undefined || Number(r['id']) > q.after) &&
        (q.since === undefined || Number(r['utc']) >= q.since) &&
        (q.zone === undefined || r['zone'] === q.zone),
    );
    const records = match.slice(-q.limit).reverse();
    const more = match.length > q.limit;
    return {
      records,
      next_before: more ? (records[records.length - 1]?.['id'] ?? null) : null,
      newest_id: this.history[this.history.length - 1]?.['id'] ?? null,
      oldest_id: this.history[0]?.['id'] ?? null,
      capacity: Device.HISTORY_CAPACITY,
      unsaved: 0,
    };
  }

  /** A believable week for the demo: daily runs, a rain skip, a fault, a restart after an update. */
  private seedHistory(): void {
    const tz = this.tz();
    const now = this.utc();
    const today = tz.localDay(now);
    const at = (day: number, h: number, m: number) => tz.fromLocal(day, h * 3600 + m * 60);
    const runs = (start: number, programId: number, zones: [number, number][]) => {
      this.addHistory({ utc: start, type: 'program_started', program_id: programId, source: 'schedule', late: false });
      let t0 = start;
      for (const [zone, sec] of zones) {
        const s = Math.round(sec * 0.8);
        this.addHistory({
          utc: t0 + s, type: 'run', zone, program_id: programId, source: 'schedule', end: 'completed', planned_s: s, actual_s: s,
          current_ma: 240 + (zone - 1) * 15, litres: Math.round(((6 + (zone - 1) * 1.5) * s) / 6) / 10,
        });
        t0 += s + 2;
      }
    };
    for (let d = 7; d >= 1; d--) {
      const day = today - d;
      const wd = (((day + 4) % 7) + 7) % 7;
      if (d === 3) {
        this.addHistory({ utc: at(day, 4, 10), type: 'ota', stage: 'started', source: 'upload', error: null });
        this.addHistory({ utc: at(day, 4, 11), type: 'ota', stage: 'installed', source: 'upload', error: null });
        this.addHistory({ utc: at(day, 4, 11) + 4, type: 'boot', reset_reason: 'software' });
        this.addHistory({ utc: at(day, 4, 11) + 9, type: 'time_set', status: 'synced' });
      }
      if (d === 4) {
        this.addHistory({ utc: at(day, 2, 40), type: 'sensor', sensor: 'rain', active: true });
        this.addHistory({ utc: at(day, 5, 30), type: 'program_skipped', program_id: 1, reason: 'sensor_inhibit' });
        this.addHistory({ utc: at(day, 11, 5), type: 'sensor', sensor: 'rain', active: false });
        continue;
      }
      if ([0, 1, 3, 5].includes(wd)) runs(at(day, 5, 30), 1, [[1, 1200], [2, 1500]]);
      if (d % 2 === 1) {
        runs(at(day, 6, 15), 2, [[3, 900], [4, 600], [5, 300]]);
        runs(at(day, 19, 30), 2, [[3, 900], [4, 600], [5, 300]]);
      }
      if (d === 2) {
        this.addHistory({ utc: at(day, 18, 2), type: 'run', zone: 5, program_id: null, source: 'manual_zone', end: 'requested', planned_s: 600, actual_s: 214, current_ma: 300, litres: 4.1 });
        this.addHistory({ utc: at(day, 19, 41), type: 'fault_raised', zone: 4, code: 'zone_open_circuit' });
        this.addHistory({ utc: at(day, 20, 3), type: 'fault_cleared', zone: 4, code: 'zone_open_circuit' });
      }
    }
  }
  changed(): void {
    this.sequence++;
  }
  sendOta(): void {
    for (const l of this.listeners) l('ota', this.ota);
  }

  // ---- configuration helpers ----------------------------------------------

  zoneCount(): number {
    return Number(this.config['zone_count'] ?? 8);
  }
  zoneCfg(i: number): Json {
    return ((this.config['zones'] as Json[])[i] ?? {}) as Json;
  }
  programs(): Json[] {
    return ((this.config['schedule'] as Json)['programs'] as Json[]) ?? [];
  }
  seasonal(): number {
    return Number((this.config['schedule'] as Json)['seasonal_percent'] ?? 100);
  }
  minRestS(): number {
    return Number((this.config['driver'] as Json)['min_rest_s'] ?? 1800);
  }

  // ---- run logic -------------------------------------------------------

  inhibited(): boolean {
    return !this.sensors.ac || this.maintenance;
  }

  /** Mirrors the API's snapshot checks and the scheduler's RunError. Returns an error code or null. */
  checkZoneRun(zone: number): { code: string; retryAfter?: number } | null {
    const z = this.zones[zone - 1];
    if (!z || zone > this.zoneCount()) return { code: 'invalid_zone' };
    if (this.zoneCfg(zone - 1)['enabled'] === false) return { code: 'zone_disabled' };
    if (z.lockedOut) return { code: 'locked_out' };
    if (z.restMs > 0) return { code: 'zone_resting', retryAfter: Math.ceil(z.restMs / 1000) };
    if (this.master.lockedOut && (this.config['master'] as Json)['enabled']) return { code: 'master_locked_out' };
    if (this.inhibited()) return { code: 'inhibited' };
    return null;
  }

  runZone(zone: number, seconds: number): number {
    const max = Number(this.zoneCfg(zone - 1)['max_run_s'] ?? 3600);
    const d = Math.min(seconds, max, HARD_MAX_S);
    this.queue.push({ id: this.nextId++, zone: zone - 1, durationSec: d, remainingMs: d * 1000, source: 'manual_zone', programId: 0 });
    this.emit('queue_changed');
    this.process();
    return d;
  }

  runProgram(id: number, source: Entry['source'] = 'manual_program'): string | null {
    const p = this.programs().find((x) => x['id'] === id);
    if (!p) return 'unknown_program';
    if (this.inhibited()) return 'inhibited';
    if (this.master.lockedOut && (this.config['master'] as Json)['enabled']) return 'master_locked_out';
    if (this.queue.some((e) => e.programId === id && e.source !== 'manual_zone')) return 'already_queued';
    const pct = this.seasonal();
    const entries: Entry[] = [];
    const durations = (p['durations_s'] as number[]) ?? [];
    for (let z = 0; z < this.zoneCount(); z++) {
      if (this.zoneCfg(z)['enabled'] === false) continue;
      const base = durations[z] ?? 0;
      const d = p['seasonal'] === false ? base : Math.floor((base * pct + 50) / 100);
      if (d > 0) entries.push({ id: this.nextId++, zone: z, durationSec: d, remainingMs: d * 1000, source, programId: id });
    }
    if (!entries.length) return 'nothing_to_run';
    if (this.queue.length + entries.length > 64) return 'queue_full';
    this.queue.push(...entries);
    this.emit('program_started', { program_id: id, source, late: false });
    this.emit('queue_changed');
    this.process();
    return null;
  }

  private setZone(i: number, state: ZoneRt['state'], reason: string): void {
    const z = this.zones[i]!;
    if (z.state === state) return;
    const wasOpen = z.state === 'open';
    z.state = state;
    if (state === 'open') {
      const head = this.queue.find((e) => e.zone === i);
      z.openedMs = this.monoMs();
      z.plannedS = Math.ceil(z.remainingMs / 1000);
      z.source = head?.source ?? 'manual_zone';
      z.programId = head?.programId ?? 0;
    }
    if (state === 'off') {
      z.lastStop = reason;
      z.remainingMs = 0;
    }
    this.emit('zone_changed', { zone: i + 1, state, reason });
    if (wasOpen && state === 'off') {
      // As the firmware's RunEnded event (history and the stream).
      const actual = Math.round((this.monoMs() - z.openedMs) / 1000);
      const ma = 240 + i * 15;
      z.learnedMa = ma;
      const flow = (this.config['sensors'] as Record<string, Json>)['flow']?.['enabled'] === true;
      this.emit('run_ended', {
        zone: i + 1, program_id: z.programId || null, source: z.source, end: reason, planned_s: z.plannedS, actual_s: actual,
        current_ma: ma, litres: flow ? Math.round(((6 + i * 1.5) * actual) / 6) / 10 : null,
      });
    }
    const anyOpen = this.zones.some((x) => x.state === 'open');
    const masterWanted = anyOpen && (this.config['master'] as Json)['enabled'] === true;
    if (masterWanted !== this.master.on) {
      this.master.on = masterWanted;
      this.emit('master_changed', { on: masterWanted });
    }
    this.changed();
  }

  /** Starts the head entry when nothing runs; drops entries whose zone cannot start. */
  process(): void {
    for (let guard = 0; guard < 70; guard++) {
      if (this.queueState === 'paused') return;
      const head = this.queue[0];
      if (!head) {
        if (this.queueState !== 'idle') {
          this.queueState = 'idle';
          this.changed();
        }
        return;
      }
      if (this.queueState === 'running') return;
      const z = this.zones[head.zone]!;
      const blocked = z.lockedOut ? 'locked_out' : z.restMs > 0 ? 'resting' : this.zoneCfg(head.zone)['enabled'] === false ? 'disabled' : this.inhibited() ? 'inhibited' : null;
      if (blocked) {
        this.queue.shift();
        this.emit('run_dropped', { zone: head.zone + 1, program_id: head.programId || null, source: head.source, reason: blocked });
        this.emit('queue_changed');
        continue;
      }
      z.remainingMs = head.remainingMs;
      this.queueState = 'running';
      this.setZone(head.zone, 'open', 'started');
      this.emit('queue_changed');
      return;
    }
  }

  stopHead(reason: string): void {
    const head = this.queue[0];
    if (head && this.queueState === 'running') this.setZone(head.zone, 'off', reason);
  }

  stopAll(): void {
    this.stopHead('requested');
    this.queue = [];
    this.queueState = 'idle';
    this.emit('queue_changed');
    this.changed();
  }
  pause(): void {
    if (this.queueState === 'paused') return;
    const head = this.queue[0];
    if (head && this.queueState === 'running') {
      head.remainingMs = this.zones[head.zone]!.remainingMs;
      this.stopHead('requested');
    }
    this.queueState = this.queue.length ? 'paused' : 'idle';
    this.pausedAt = this.monoMs();
    this.emit('queue_changed');
    this.changed();
  }
  pausedAt = 0;
  resume(): void {
    if (this.queueState !== 'paused') return;
    this.queueState = 'idle';
    this.emit('queue_changed');
    this.process();
    this.changed();
  }
  skip(): void {
    if (!this.queue.length) return;
    this.stopHead('requested');
    this.queue.shift();
    if (this.queueState === 'running') this.queueState = 'idle';
    this.emit('queue_changed');
    this.process();
    this.changed();
  }

  raiseFault(code: string, zone: number | null): void {
    if (zone != null) {
      const z = this.zones[zone - 1]!;
      z.fault = code;
      z.lockedOut = true;
      if (this.queue[0]?.zone === zone - 1 && this.queueState === 'running') {
        this.setZone(zone - 1, 'off', 'fault');
        this.queue.shift();
        this.queueState = 'idle';
      }
    }
    if (code === 'ac_lost') {
      this.sensors.ac = false;
      this.stopAll();
    }
    if (code === 'current_sense_unavailable') this.sensors.senseOk = false;
    if (code === 'master_locked_out') this.master.lockedOut = true;
    if (code === 'short_circuit' || code === 'over_current_trip') this.stopAll();
    this.faults.add(code);
    this.emit('fault_raised', { code, zone });
    this.process();
    this.changed();
  }

  clearFaults(): void {
    for (const code of [...this.faults]) {
      if (code === 'ac_lost' && !this.sensors.ac) continue;
      if (code === 'current_sense_unavailable' && !this.sensors.senseOk) continue;
      if (code === 'time_invalid' && this.timeStatus === 'unset') continue;
      this.faults.delete(code);
      this.emit('fault_cleared', { code, zone: null });
    }
    this.zones.forEach((z, i) => {
      if (z.lockedOut || z.fault !== 'none') {
        const code = z.fault === 'none' ? 'zone_open_circuit' : z.fault;
        z.lockedOut = false;
        z.fault = 'none';
        this.emit('fault_cleared', { code, zone: i + 1 });
      }
    });
    this.master.lockedOut = false;
    this.master.faultCount = 0;
    this.changed();
  }

  setRainDelay(until: number): void {
    this.rainDelayUntil = until;
    if (until > this.utc()) {
      const before = this.queue.length;
      if (this.queue[0]?.source === 'schedule') this.stopHead('requested');
      this.queue = this.queue.filter((e) => e.source !== 'schedule');
      if (before !== this.queue.length) this.queueState = 'idle';
    }
    this.emit('rain_delay_changed', { until_utc: until || null });
    this.process();
    this.changed();
  }

  /** Advances the simulation. Called by the server every 200 ms. */
  tick(): void {
    const now = this.monoMs();
    const dt = Math.max(0, now - this.lastTick);
    this.lastTick = now;
    if (dt === 0) return;
    this.zones.forEach((z, i) => {
      if (z.state === 'open') {
        z.remainingMs -= dt;
        z.onMs += dt;
        const max = Number(this.zoneCfg(i)['max_run_s'] ?? 3600) * 1000;
        if (z.onMs >= max) {
          z.restMs = this.minRestS() * 1000;
          z.onMs = 0;
          this.setZone(i, 'off', 'max_run_time');
          if (this.queue[0]?.zone === i) this.queue.shift();
          this.queueState = 'idle';
        } else if (z.remainingMs <= 0) {
          this.setZone(i, 'off', 'completed');
          this.queue.shift();
          this.queueState = 'idle';
          this.emit('queue_changed');
        }
      } else if (z.restMs > 0) {
        z.restMs = Math.max(0, z.restMs - dt);
        if (z.restMs === 0) this.changed();
      } else if (z.onMs > 0) {
        z.onMs = Math.max(0, z.onMs - dt / 4); // closed time slowly ends a run of back-to-back runs
      }
    });
    this.readings();
    this.sensors.flowTotal += (this.sensors.flowLpm * dt) / 60000;
    if (this.queueState === 'paused') {
      const timeout = Number((this.config['schedule'] as Json)['pause_timeout_min'] ?? 120) * 60000;
      if (now - this.pausedAt >= timeout) {
        this.stopAll();
        this.emit('pause_expired');
      }
    }
    if (this.rainDelayUntil && this.rainDelayUntil <= this.utc()) {
      this.rainDelayUntil = 0;
      this.emit('rain_delay_changed', { until_utc: null });
    }
    this.process();
    if (this.queueState === 'running') this.changed();
  }

  /** Flow and valve current follow the open zones (also with a frozen clock). */
  readings(): void {
    let flow = 0;
    let current = 0;
    this.zones.forEach((z, i) => {
      if (z.state === 'open') {
        flow += 6 + i * 1.5;
        current += 240 + i * 15;
      }
    });
    if (this.master.on) current += 180;
    this.sensors.flowLpm = flow;
    this.sensors.currentMa = current;
  }

  // ---- JSON views (same shapes as zf_web/src/json_map.cpp) ----------------

  zoneJson(i: number): Json {
    const c = this.zoneCfg(i);
    const z = this.zones[i]!;
    return {
      id: i + 1,
      name: c['name'] ?? '',
      enabled: c['enabled'] !== false,
      max_run_s: c['max_run_s'] ?? 3600,
      current_min_ma: c['current_min_ma'] ?? 80,
      current_max_ma: c['current_max_ma'] ?? 700,
      state: z.state,
      remaining_s: Math.max(0, Math.ceil(z.remainingMs / 1000)),
      last_stop: z.lastStop,
      fault: z.fault,
      locked_out: z.lockedOut,
      resting: z.restMs > 0,
      rest_remaining_s: Math.ceil(z.restMs / 1000),
      default_run_s: c['default_run_s'] ?? 600,
      learned_current_ma: z.learnedMa > 0 ? z.learnedMa : null,
    };
  }

  queueJson(): Json {
    const head = this.queue[0];
    return {
      state: this.queueState,
      pause_timeout_min: (this.config['schedule'] as Json)['pause_timeout_min'] ?? 120,
      length: this.queue.length,
      current_remaining_s: head ? Math.ceil((this.queueState === 'running' ? this.zones[head.zone]!.remainingMs : head.remainingMs) / 1000) : 0,
      items: this.queue.slice(0, 8).map((e) => ({ id: e.id, zone: e.zone + 1, duration_s: e.durationSec, source: e.source, program_id: e.programId || null })),
      ...this.pauseJson(),
    };
  }

  private pauseJson(): { paused_s: number; stop_in_s: number | null } {
    if (this.queueState !== 'paused') return { paused_s: 0, stop_in_s: null };
    const paused = Math.floor((this.monoMs() - this.pausedAt) / 1000);
    const limit = Number((this.config['schedule'] as Json)['pause_timeout_min'] ?? 120) * 60;
    return { paused_s: paused, stop_in_s: Math.max(0, limit - paused) };
  }

  sensorsJson(): Json {
    this.readings();
    const s = this.config['sensors'] as Record<string, Json>;
    return {
      rain: { enabled: s['rain']?.['enabled'] === true, active: this.sensors.rainActive },
      spare: { enabled: s['spare']?.['enabled'] === true, active: this.sensors.spareActive },
      flow: { enabled: s['flow']?.['enabled'] === true, rate_lpm: Math.round(this.sensors.flowLpm * 10) / 10, total_l: Math.round(this.sensors.flowTotal * 10) / 10 },
      ac_present: this.sensors.ac,
      valve_current_ma: this.sensors.senseOk ? Math.round(this.sensors.currentMa) : null,
      current_sense_available: this.sensors.senseOk,
      over_current_tripped: this.faults.has('over_current_trip'),
    };
  }

  faultsJson(): Json {
    return {
      active: [...this.faults].filter((c) => c !== 'zone'),
      zones: this.zones
        .map((z, i) => ({ zone: i + 1, fault: z.fault, locked_out: z.lockedOut }))
        .filter((z, i) => i < this.zoneCount() && (z.fault !== 'none' || z.locked_out)),
    };
  }

  rainJson(): Json {
    const active = this.rainDelayUntil > this.utc();
    return { active, until_utc: this.rainDelayUntil || null, remaining_s: active ? this.rainDelayUntil - this.utc() : 0 };
  }

  statusJson(): Json {
    const masterEnabled = (this.config['master'] as Json)['enabled'] === true;
    return {
      sequence: this.sequence,
      utc: this.utc(),
      time_status: this.timeStatus,
      uptime_s: Math.floor((this.monoMs() - this.bootMono) / 1000) + 86400 * 3 + 7380,
      zones: Array.from({ length: this.zoneCount() }, (_, i) => this.zoneJson(i)),
      master: { enabled: masterEnabled, on: this.master.on, locked_out: this.master.lockedOut, fault_count: this.master.faultCount },
      queue: this.queueJson(),
      sensors: this.sensorsJson(),
      rain_delay: this.rainJson(),
      inhibit: {
        sensor: this.sensors.rainActive && (this.config['sensors'] as Record<string, Json>)['rain']?.['enabled'] === true,
        no_ac: !this.sensors.ac,
        maintenance: this.maintenance,
        master_lockout: this.master.lockedOut && masterEnabled,
      },
      faults: this.faultsJson(),
      maintenance: this.maintenance,
      config_revision: 1,
      network: this.network,
      ota: this.ota,
    };
  }

  timeJson(): Json {
    const tz = this.tz();
    const utc = this.utc();
    const l = tz.toLocal(utc);
    const p = (n: number) => String(n).padStart(2, '0');
    return {
      utc,
      status: this.timeStatus,
      rtc_present: true,
      timezone: this.config['timezone'],
      local:
        this.timeStatus === 'unset'
          ? null
          : `${l.date.year}-${p(l.date.month)}-${p(l.date.day)}T${p(Math.floor(l.secondOfDay / 3600))}:${p(Math.floor(l.secondOfDay / 60) % 60)}:${p(l.secondOfDay % 60)}`,
      utc_offset_s: l.utcOffsetSec,
      dst: l.dst,
    };
  }

  firmwareJson(): Json {
    return {
      version: '0.3.0-mock',
      idf_version: 'v5.5.6',
      build_date: '2026-11-10',
      running_slot: 'ota_0',
      running_state: 'valid',
      next_slot: 'ota_1',
      other_slot_version: '0.2.1',
      ota: this.ota,
    };
  }

  infoJson(): Json {
    return {
      api_version: '1.0.0',
      device_name: this.config['device_name'],
      hostname: 'zoneflow-ab12',
      mac: '24:0a:c4:12:ab:12',
      board: 'zoneflow-rev-a (mock)',
      firmware: { version: '0.3.0-mock', project: 'zoneflow-fw', idf_version: 'v5.5.6', build_date: '2026-11-10' },
      uptime_s: Math.floor((this.monoMs() - this.bootMono) / 1000) + 86400 * 3 + 7380,
      reset_reason: 'poweron',
      heap: { free: 7_912_448, min_free: 7_801_344, largest_block: 7_733_248, internal_free: 151_552, internal_min_free: 132_096 },
      http_stack_free_min: 6_912,
    };
  }
}

// Application state as signals. The controller is the source of truth: the
// UI renders the last `state` message of the event stream and never shows it
// as live once the stream is lost.
import { computed, signal } from '@preact/signals';
import type { AuthStatus, Config, OtaStatus, Status } from '../api/client.ts';
import { TimeZone } from '../lib/tz.ts';

export type Conn = 'connecting' | 'live' | 'lost' | 'paused' | 'busy';

export const auth = signal<AuthStatus | null>(null);
export const status = signal<Status | null>(null);
/** performance.now() when `status` arrived; countdowns run from it. */
export const statusAt = signal(0);
export const conn = signal<Conn>('connecting');
export const config = signal<Config | null>(null);
export const ota = signal<OtaStatus | null>(null);
/** Increments once a second while the page is visible; read it to re-render countdowns. */
export const tick = signal(0);
/** Set right after a first-run password setup, so the onboarding steps follow. */
export const onboarding = signal(false);
/** The login sheet is open (read-only visitor tried to change something). */
export const loginOpen = signal(false);

export const signedIn = computed(() => auth.value?.authenticated === true);
export const live = computed(() => conn.value === 'live');

export const timeZone = computed(() => {
  const s = config.value?.timezone;
  return (s ? TimeZone.parse(s) : null) ?? TimeZone.utc();
});

/** Seconds since `status` arrived, while live; frozen otherwise. */
export function elapsedSec(): number {
  void tick.value;
  if (conn.value !== 'live') return 0;
  return Math.max(0, (performance.now() - statusAt.value) / 1000);
}

/** Controller clock (status.utc advanced by local elapsed time). */
export function deviceNow(): number {
  const s = status.value;
  if (!s?.utc) return Math.floor(Date.now() / 1000);
  return Math.floor(s.utc + elapsedSec());
}

export interface ZoneInfo {
  id: number;
  name: string;
  enabled: boolean;
  maxRunSec: number;
  defaultRunSec: number;
  currentMinMa: number;
  currentMaxMa: number;
  /** Measured with this zone open alone since the controller started; null until then. */
  learnedMa: number | null;
}

export const zoneCount = computed(() => config.value?.zone_count ?? status.value?.zones?.length ?? 8);

/** Zones in use (1..zone_count), merged from status and config. */
export const zones = computed<ZoneInfo[]>(() => {
  const st = status.value?.zones ?? [];
  const cz = config.value?.zones ?? [];
  const out: ZoneInfo[] = [];
  for (let i = 0; i < zoneCount.value; i++) {
    const s = st[i];
    const c = cz[i];
    // Settings from the configuration (reloaded right after a save), else
    // from the last state message.
    out.push({
      id: i + 1,
      name: (c?.name ?? s?.name ?? '').trim(),
      enabled: c?.enabled ?? s?.enabled ?? true,
      maxRunSec: c?.max_run_s ?? s?.max_run_s ?? 3600,
      defaultRunSec: c?.default_run_s ?? s?.default_run_s ?? 600,
      currentMinMa: c?.current_min_ma ?? s?.current_min_ma ?? 80,
      currentMaxMa: c?.current_max_ma ?? s?.current_max_ma ?? 700,
      learnedMa: s?.learned_current_ma ?? null,
    });
  }
  return out;
});

export const programs = computed(() => config.value?.schedule?.programs ?? []);

export function zoneLabel(id: number | null | undefined, name?: string): string {
  if (id == null) return '';
  const n = name ?? zones.value[id - 1]?.name;
  return n ? n : `Zone ${id}`;
}

// ---- event log (this page session only) ----------------------------------

export interface LogEntry {
  seq: number;
  utc: number;
  type: string;
  data: Record<string, unknown>;
}
export const eventLog = signal<LogEntry[]>([]);
let logSeq = 0;
export function logEvent(data: Record<string, unknown>): void {
  const entry: LogEntry = {
    seq: ++logSeq,
    utc: typeof data['utc'] === 'number' ? (data['utc'] as number) : deviceNow(),
    type: String(data['type'] ?? ''),
    data,
  };
  eventLog.value = [entry, ...eventLog.value].slice(0, 200);
}

// ---- toasts ----------------------------------------------------------------

export interface Toast {
  id: number;
  text: string;
  kind: 'info' | 'error' | 'success';
}
export const toasts = signal<Toast[]>([]);
let toastSeq = 0;
export function toast(text: string, kind: Toast['kind'] = 'info', ms = kind === 'error' ? 8000 : 4000): void {
  const id = ++toastSeq;
  toasts.value = [...toasts.value.filter((x) => x.text !== text), { id, text, kind }].slice(-3);
  setTimeout(() => dismissToast(id), ms);
}
export function dismissToast(id: number): void {
  toasts.value = toasts.value.filter((x) => x.id !== id);
}

// ---- pending run commands --------------------------------------------------

/** Run commands sent without a verdict yet ("zone:3", "program:2") -> expiry (ms). */
export const pending = signal<Record<string, number>>({});
export function setPending(key: string, ms = 6000): void {
  pending.value = { ...pending.value, [key]: performance.now() + ms };
  setTimeout(() => clearPending(key), ms);
}
export function clearPending(key: string): void {
  if (!(key in pending.value)) return;
  const next = { ...pending.value };
  delete next[key];
  pending.value = next;
}

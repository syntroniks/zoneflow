// The live connection: one EventSource on /api/v1/events.
//
// - `state` messages replace the status snapshot; nothing else does.
// - The device sends `state` at least every 30 s, so 40 s of silence means
//   the stream is dead even if the socket looks open: the UI marks the state
//   as lost and reconnects.
// - The device allows 3 streams; the stream is closed when the tab has been
//   hidden for a minute and reopened when it becomes visible.
import { batch } from '@preact/signals';
import { BASE, call, setCsrf } from '../api/client.ts';
import type { Network, OtaStatus, Status } from '../api/client.ts';
import { handleEvent } from './events.ts';
import { auth, conn, ota, status, statusAt, tick } from './store.ts';

const SILENCE_MS = 40_000;
const HIDDEN_CLOSE_MS = 60_000;

let es: EventSource | null = null;
let watchdog: ReturnType<typeof setTimeout> | undefined;
let retry: ReturnType<typeof setTimeout> | undefined;
let hiddenTimer: ReturnType<typeof setTimeout> | undefined;
let failures = 0;
let started = false;

function armWatchdog(): void {
  clearTimeout(watchdog);
  watchdog = setTimeout(() => {
    conn.value = 'lost';
    reopen(0);
  }, SILENCE_MS);
}

function close(): void {
  clearTimeout(watchdog);
  clearTimeout(retry);
  es?.close();
  es = null;
}

function reopen(delayMs: number): void {
  close();
  retry = setTimeout(open, delayMs);
}

async function recheckAuth(): Promise<void> {
  try {
    const a = await call('get', '/auth');
    setCsrf(a.csrf);
    auth.value = a;
  } catch {
    // keep the last known state
  }
}

/** Why did the stream fail? EventSource does not say, so ask once. */
async function diagnose(): Promise<void> {
  try {
    const a = await call('get', '/auth');
    auth.value = a;
    if (a.protect_reads && !a.authenticated) return; // the auth gate takes over
  } catch {
    // unreachable: keep retrying
  }
  if (failures >= 3) {
    // 503 means three streams are already open elsewhere.
    const ctl = new AbortController();
    try {
      const res = await fetch(`${BASE}/events`, { signal: ctl.signal, cache: 'no-store' });
      if (res.status === 503) conn.value = 'busy';
    } catch {
      // ignore
    } finally {
      ctl.abort();
    }
  }
}

function open(): void {
  close();
  if (conn.value !== 'lost' && conn.value !== 'busy') conn.value = status.value ? 'lost' : 'connecting';
  const src = new EventSource(`${BASE}/events`);
  es = src;
  armWatchdog();
  src.addEventListener('state', (m) => {
    const s = JSON.parse((m as MessageEvent<string>).data) as Status;
    failures = 0;
    // Back after a gap: the controller may have restarted, which ends every
    // session, so ask again instead of finding out on the next write.
    if (conn.value !== 'live' && status.value) void recheckAuth();
    batch(() => {
      status.value = s;
      statusAt.value = performance.now();
      if (s.ota) ota.value = s.ota;
      conn.value = 'live';
    });
    armWatchdog();
  });
  src.addEventListener('event', (m) => {
    armWatchdog();
    handleEvent(JSON.parse((m as MessageEvent<string>).data) as Record<string, unknown>);
  });
  src.addEventListener('network', (m) => {
    armWatchdog();
    const n = JSON.parse((m as MessageEvent<string>).data) as Network;
    if (status.value) status.value = { ...status.value, network: n };
  });
  src.addEventListener('ota', (m) => {
    armWatchdog();
    ota.value = JSON.parse((m as MessageEvent<string>).data) as OtaStatus;
  });
  src.onerror = () => {
    if (es !== src) return;
    if (conn.value === 'live') conn.value = 'lost';
    if (src.readyState === EventSource.CLOSED) {
      failures++;
      void diagnose();
      reopen(Math.min(30_000, 1000 * 2 ** Math.min(failures, 5)));
    }
  };
}

/** Starts the stream (once) and the 1 Hz tick. */
export function startLive(): void {
  if (started) {
    if (!es) open();
    return;
  }
  started = true;
  open();
  setInterval(() => {
    if (document.visibilityState === 'visible') tick.value++;
  }, 1000);
  document.addEventListener('visibilitychange', () => {
    clearTimeout(hiddenTimer);
    if (document.visibilityState === 'hidden') {
      hiddenTimer = setTimeout(() => {
        close();
        conn.value = 'paused';
      }, HIDDEN_CLOSE_MS);
    } else if (!es) {
      open();
    }
  });
  window.addEventListener('online', () => reconnectNow());
}

export function reconnectNow(): void {
  failures = 0;
  if (conn.value === 'busy') conn.value = 'lost';
  reopen(0);
}

export function stopLive(): void {
  close();
}

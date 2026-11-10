// Commands sent to the controller. Every write goes through here so errors
// are reported the same way. Results are shown from the next `state`
// message, not from the 202 answer (web-ui-contract.md).
import { ApiError, call } from '../api/client.ts';
import type { Config } from '../api/client.ts';
import { t } from '../i18n/index.ts';
import { errorText } from './errors.ts';
import { auth, clearPending, config, loginOpen, setPending, signedIn, toast, zoneLabel } from './store.ts';

/** Opens the sign-in sheet for a read-only visitor; true if already signed in. */
export function ensureSignedIn(): boolean {
  if (signedIn.value) return true;
  loginOpen.value = true;
  return false;
}

async function guarded<T>(fn: () => Promise<T>, ctx: { zone?: string } = {}): Promise<T | null> {
  if (!ensureSignedIn()) return null;
  try {
    return await fn();
  } catch (e) {
    if (e instanceof ApiError && e.status === 401) {
      loginOpen.value = true;
      toast(t('session_expired'), 'error');
    } else {
      toast(errorText(e, ctx), 'error');
    }
    return null;
  }
}

export async function refreshAuth(): Promise<void> {
  try {
    auth.value = await call('get', '/auth');
  } catch {
    // keep the last known state; the stream reports connectivity
  }
}

export async function loadConfig(): Promise<void> {
  try {
    config.value = await call('get', '/config');
  } catch {
    // reads may be protected or the device unreachable; retried on the next config_applied or login
  }
}

/** Run one zone. The answer can be a refusal (409) or "no verdict yet" (watch for command_rejected). */
export async function runZone(zone: number, seconds: number): Promise<boolean> {
  const name = zoneLabel(zone);
  const r = await guarded(() => call('post', '/zones/{zone}/run', { params: { zone }, body: { duration_s: seconds } }), {
    zone: name,
  });
  if (!r) return false;
  if (r.confirmed === false) {
    setPending(`zone:${zone}`);
    toast(t('run_pending'));
  } else {
    setPending(`zone:${zone}`, 3000);
    toast(t('run_started', { zone: name }), 'success');
  }
  return true;
}

export async function runProgram(id: number, name: string): Promise<boolean> {
  const r = await guarded(() => call('post', '/programs/{id}/run', { params: { id } }));
  if (!r) return false;
  setPending(`program:${id}`, r.confirmed === false ? 6000 : 3000);
  toast(r.confirmed === false ? t('run_pending') : t('program_started_toast', { name }), r.confirmed === false ? 'info' : 'success');
  return true;
}

export async function stopZone(zone: number): Promise<void> {
  clearPending(`zone:${zone}`);
  await guarded(() => call('post', '/zones/{zone}/stop', { params: { zone } }), { zone: zoneLabel(zone) });
}

export const stopAll = () => guarded(() => call('post', '/queue/stop'));
export const pauseQueue = () => guarded(() => call('post', '/queue/pause'));
export const resumeQueue = () => guarded(() => call('post', '/queue/resume'));
export const skipQueue = () => guarded(() => call('post', '/queue/skip'));
export const clearFaults = () => guarded(() => call('post', '/faults/clear'));

export const setRainDelayHours = (hours: number) => guarded(() => call('put', '/rain-delay', { body: { hours } }));
export const setRainDelayUntil = (until_utc: number) => guarded(() => call('put', '/rain-delay', { body: { until_utc } }));
export const clearRainDelay = () => guarded(() => call('delete', '/rain-delay'));

/** Writes a modified copy of the whole configuration (fields with no route of their own). */
export async function updateConfig(change: (c: Config) => void): Promise<boolean> {
  if (!ensureSignedIn()) return false;
  try {
    const current = await call('get', '/config');
    const next = structuredClone(current);
    change(next);
    await call('put', '/config', { body: next });
    config.value = next;
    toast(t('saved'), 'success');
    return true;
  } catch (e) {
    if (e instanceof ApiError && e.status === 401) loginOpen.value = true;
    toast(errorText(e), 'error');
    return false;
  }
}

/** Throws ApiError so forms can put `error.path` next to the field. */
export async function patchZone(zone: number, body: { name?: string; enabled?: boolean; max_run_s?: number; current_min_ma?: number; current_max_ma?: number }) {
  const z = await call('patch', '/zones/{zone}', { params: { zone }, body });
  await loadConfig();
  return z;
}

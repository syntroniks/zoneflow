// Upcoming starts for the dashboard and program list, from the shared
// preview logic (lib/preview.ts) and the controller's clock and time zone.
import { previewRuns, type PlannedRun, type PreviewProgram } from '../lib/preview.ts';
import { config, deviceNow, programs, status, timeZone, zones } from './store.ts';

export function plannedRuns(list: PreviewProgram[] = programs.value, horizonSec = 7 * 86400): PlannedRun[] {
  const s = status.value;
  return previewRuns({
    programs: list,
    now: deviceNow(),
    tz: timeZone.value,
    zoneCount: zones.value.length,
    zoneEnabled: zones.value.map((z) => z.enabled),
    seasonalPercent: config.value?.schedule?.seasonal_percent ?? 100,
    rainDelayUntil: s?.rain_delay?.active ? s.rain_delay.until_utc : null,
    horizonSec,
  });
}

export function nextPlanned(): PlannedRun | null {
  return plannedRuns().find((r) => !r.skip) ?? null;
}

const LAST_RUN_KEY = 'zoneflow.lastRunMin';

/** Last manual run duration in minutes (per browser, a convenience only). */
export function lastRunMinutes(): number {
  try {
    const v = Number(localStorage.getItem(LAST_RUN_KEY));
    return v >= 1 && v <= 240 ? v : 10;
  } catch {
    return 10;
  }
}

export function rememberRunMinutes(m: number): void {
  try {
    localStorage.setItem(LAST_RUN_KEY, String(m));
  } catch {
    // private mode: ignore
  }
}

// Client-side preview of upcoming scheduled starts. Follows the rules of
// zf_core Scheduler (scheduler.cpp): day rules, start times in the
// controller's time zone (POSIX TZ), DST gaps and repeats, seasonal
// adjustment, disabled zones and rain delay. Checked against the same
// vectors as the C++ scheduler: firmware/zoneflow-fw/test/vectors/.
//
// It lists planned starts. It does not model the run queue: a start whose
// program is still running from an earlier start is skipped by the device
// (already_queued) but shown here.
import { floorMod, civilFromDays, parseClockTime, parseIsoDate, weekdayFromDays } from './civil.ts';
import type { TimeZone } from './tz.ts';

export const WEEKDAY_KEYS = ['sun', 'mon', 'tue', 'wed', 'thu', 'fri', 'sat'] as const;
export type WeekdayKey = (typeof WEEKDAY_KEYS)[number];

/** Program fields the preview needs, in the API's JSON form. */
export interface PreviewProgram {
  id?: number;
  name?: string;
  enabled?: boolean;
  starts?: string[];
  days?: {
    type: 'weekdays' | 'interval' | 'odd' | 'even';
    weekdays?: string[];
    every?: number;
    anchor?: string;
  };
  durations_s?: number[];
  seasonal?: boolean;
}

export interface PreviewInput {
  programs: PreviewProgram[];
  /** Controller time (status.utc), Unix seconds. */
  now: number;
  tz: TimeZone;
  zoneCount: number;
  /** Index 0 = zone 1; missing entries count as enabled. */
  zoneEnabled: boolean[];
  seasonalPercent: number;
  /** 0 or null: none. */
  rainDelayUntil?: number | null;
  /** Length of the window after `now`; default 7 days. */
  horizonSec?: number;
}

export type SkipReason = 'rain_delay' | 'nothing_to_run';

export interface PlannedRun {
  programId: number;
  name: string;
  /** Start instant, Unix seconds UTC. */
  at: number;
  /** Local day serial and minute of the start time as written in the program. */
  localDay: number;
  minute: number;
  /** After seasonal adjustment; only zones that will run. Zone numbers are 1-based. */
  zones: { zone: number; seconds: number }[];
  totalSec: number;
  skip?: SkipReason;
}

interface DayRule {
  matches(day: number): boolean;
}

export function dayRule(days: PreviewProgram['days']): DayRule {
  const type = days?.type ?? 'weekdays';
  if (type === 'interval') {
    const every = days?.every ?? 1;
    const anchor = days?.anchor ? (parseIsoDate(days.anchor) ?? 0) : 0;
    return { matches: (d) => every > 0 && floorMod(d - anchor, every) === 0 };
  }
  if (type === 'odd') return { matches: (d) => civilFromDays(d).day % 2 === 1 };
  if (type === 'even') return { matches: (d) => civilFromDays(d).day % 2 === 0 };
  // weekdays; absent list = every day (config_json.cpp default mask 0x7F)
  let mask = 0x7f;
  if (days?.weekdays) {
    mask = 0;
    for (const w of days.weekdays) {
      const i = WEEKDAY_KEYS.indexOf(w as WeekdayKey);
      if (i >= 0) mask |= 1 << i;
    }
  }
  return { matches: (d) => (mask & (1 << weekdayFromDays(d))) !== 0 };
}

/** Scheduler::adjustedDurationSec. */
export function adjustedSeconds(base: number, seasonal: boolean, percent: number): number {
  if (!seasonal) return base;
  return Math.floor((base * percent + 50) / 100);
}

/** Sorted, de-duplicated start minutes (Scheduler::configure). Invalid entries are dropped. */
export function startMinutes(starts: string[] | undefined): number[] {
  const out = new Set<number>();
  for (const s of (starts ?? []).slice(0, 4)) {
    const m = parseClockTime(s);
    if (m !== null) out.add(m);
  }
  return [...out].sort((a, b) => a - b);
}

export function previewRuns(input: PreviewInput): PlannedRun[] {
  const { tz, now } = input;
  const end = now + (input.horizonSec ?? 7 * 86400);
  const runs: PlannedRun[] = [];
  const zoneCount = Math.max(0, Math.min(8, input.zoneCount));
  for (const p of input.programs) {
    if (p.enabled === false) continue;
    const minutes = startMinutes(p.starts);
    if (minutes.length === 0) continue;
    const rule = dayRule(p.days);
    const zones: { zone: number; seconds: number }[] = [];
    for (let z = 0; z < zoneCount; z++) {
      if (input.zoneEnabled[z] === false) continue;
      const s = adjustedSeconds(p.durations_s?.[z] ?? 0, p.seasonal !== false, input.seasonalPercent);
      if (s > 0) zones.push({ zone: z + 1, seconds: s });
    }
    const totalSec = zones.reduce((a, b) => a + b.seconds, 0);
    const firstDay = tz.localDay(now) - 1;
    const lastDay = tz.localDay(end) + 1;
    let lastAt = -Infinity;
    for (let day = firstDay; day <= lastDay; day++) {
      if (!rule.matches(day)) continue;
      for (const minute of minutes) {
        const at = tz.fromLocal(day, minute * 60);
        if (at <= now || at > end) continue;
        const run: PlannedRun = {
          programId: p.id ?? 0,
          name: p.name ?? '',
          at,
          localDay: day,
          minute,
          zones,
          totalSec,
        };
        if (input.rainDelayUntil && at < input.rainDelayUntil) run.skip = 'rain_delay';
        else if (zones.length === 0) run.skip = 'nothing_to_run';
        // Two starts that map to the same instant (both inside a DST gap)
        // run once; the device reports the earlier one as missed.
        if (at === lastAt) runs[runs.length - 1] = run;
        else runs.push(run);
        lastAt = at;
      }
    }
  }
  runs.sort((a, b) => a.at - b.at || a.programId - b.programId);
  return runs;
}

/** The next start that will actually run, or null. */
export function nextRun(input: PreviewInput): PlannedRun | null {
  return previewRuns(input).find((r) => !r.skip) ?? null;
}

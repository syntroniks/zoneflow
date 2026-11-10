// The preview must agree with the C++ Scheduler: both run the shared vectors
// in firmware/zoneflow-fw/test/vectors/schedule_preview.json.
import { readFileSync } from 'node:fs';
import { describe, expect, it } from 'vitest';
import { daysFromCivil } from '../../src/lib/civil.ts';
import { adjustedSeconds, dayRule, nextRun, previewRuns, startMinutes, type PreviewProgram } from '../../src/lib/preview.ts';
import { TimeZone } from '../../src/lib/tz.ts';

interface Case {
  name: string;
  timezone: string;
  now_utc: number;
  zone_count: number;
  zones_enabled: boolean[];
  seasonal_percent: number;
  rain_delay_until: number;
  horizon_s: number;
  programs: PreviewProgram[];
  expected: { program_id: number; at_utc: number; zones: [number, number][]; skip: string | null }[];
}

const vectors = JSON.parse(
  readFileSync(new URL('../../../test/vectors/schedule_preview.json', import.meta.url), 'utf8'),
) as { cases: Case[] };

describe('shared schedule vectors', () => {
  it('has enough cases', () => expect(vectors.cases.length).toBeGreaterThanOrEqual(10));
  it.each(vectors.cases.map((c) => [c.name, c] as const))('%s', (_n, c) => {
    const runs = previewRuns({
      programs: c.programs,
      now: c.now_utc,
      tz: TimeZone.parse(c.timezone)!,
      zoneCount: c.zone_count,
      zoneEnabled: c.zones_enabled,
      seasonalPercent: c.seasonal_percent,
      rainDelayUntil: c.rain_delay_until,
      horizonSec: c.horizon_s,
    });
    expect(
      runs.map((r) => ({
        program_id: r.programId,
        at_utc: r.at,
        zones: r.skip ? [] : r.zones.map((z) => [z.zone, z.seconds]),
        skip: r.skip ?? null,
      })),
    ).toEqual(c.expected);
  });
});

// Hand-checked anchors, independent of the generator.
describe('preview anchors', () => {
  const berlin = TimeZone.parse('CET-1CEST,M3.5.0,M10.5.0/3')!;
  const utc = (y: number, m: number, d: number, h = 0, mi = 0) => daysFromCivil(y, m, d) * 86400 + h * 3600 + mi * 60;

  it('runs a start inside the spring-forward gap at the end of the gap', () => {
    const runs = previewRuns({
      programs: [{ id: 1, starts: ['02:30'], days: { type: 'weekdays', weekdays: ['sun'] }, durations_s: [60] }],
      now: utc(2026, 3, 28),
      tz: berlin,
      zoneCount: 1,
      zoneEnabled: [true],
      seasonalPercent: 100,
      horizonSec: 2 * 86400,
    });
    expect(runs.map((r) => r.at)).toEqual([utc(2026, 3, 29, 1, 0)]); // 03:00 CEST
  });

  it('runs a start in the repeated hour once, at the first occurrence', () => {
    const runs = previewRuns({
      programs: [{ id: 1, starts: ['02:30'], days: { type: 'weekdays', weekdays: ['sun'] }, durations_s: [60] }],
      now: utc(2026, 10, 24),
      tz: berlin,
      zoneCount: 1,
      zoneEnabled: [true],
      seasonalPercent: 100,
      horizonSec: 2 * 86400,
    });
    expect(runs.map((r) => r.at)).toEqual([utc(2026, 10, 25, 0, 30)]); // 02:30 CEST
  });

  it('rounds seasonal adjustment like the firmware', () => {
    expect(adjustedSeconds(601, true, 150)).toBe(902); // (601*150+50)/100 = 902.0
    expect(adjustedSeconds(33, true, 150)).toBe(50); // 49.5 + 0.5
    expect(adjustedSeconds(600, false, 0)).toBe(600);
  });

  it('sorts and de-duplicates start times and drops invalid ones', () => {
    expect(startMinutes(['18:30', '06:00', '06:00', '24:00', 'x'])).toEqual([360, 1110]);
  });

  it('matches interval days before the anchor', () => {
    const r = dayRule({ type: 'interval', every: 3, anchor: '2026-06-10' });
    const a = daysFromCivil(2026, 6, 10);
    expect([a - 3, a - 1, a, a + 3].map((d) => r.matches(d))).toEqual([true, false, true, true]);
  });

  it('nextRun skips starts held by a rain delay', () => {
    const now = utc(2026, 6, 1, 3);
    const n = nextRun({
      programs: [{ id: 9, name: 'Daily', starts: ['06:00'], days: { type: 'weekdays' }, durations_s: [300] }],
      now,
      tz: berlin,
      zoneCount: 1,
      zoneEnabled: [true],
      seasonalPercent: 100,
      rainDelayUntil: utc(2026, 6, 2, 12),
    });
    expect(n?.at).toBe(utc(2026, 6, 3, 4)); // Wed 06:00 CEST
  });
});

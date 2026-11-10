// Writes firmware/zoneflow-fw/test/vectors/schedule_preview.json: schedule
// cases and the starts the web UI preview (src/lib/preview.ts) computes for
// them. The C++ host test schedule_vectors_test.cpp runs the real Scheduler
// on the same cases and must find the same starts, so the UI and the device
// cannot drift apart unnoticed.
//   node scripts/gen-vectors.ts          write the file
//   node scripts/gen-vectors.ts --check  fail if it is stale (CI)
import { readFileSync, writeFileSync } from 'node:fs';
import { fileURLToPath } from 'node:url';
import { daysFromCivil } from '../src/lib/civil.ts';
import { previewRuns, type PreviewProgram } from '../src/lib/preview.ts';
import { TimeZone } from '../src/lib/tz.ts';

const out = fileURLToPath(new URL('../../test/vectors/schedule_preview.json', import.meta.url));

const utc = (y: number, mo: number, d: number, h = 0, mi = 0, s = 0) =>
  daysFromCivil(y, mo, d) * 86400 + h * 3600 + mi * 60 + s;

interface Case {
  name: string;
  note: string;
  timezone: string;
  now_utc: number;
  zone_count: number;
  zones_enabled: boolean[];
  seasonal_percent: number;
  rain_delay_until: number;
  horizon_s: number;
  programs: PreviewProgram[];
}

const all8 = [true, true, true, true, true, true, true, true];
const base = { zone_count: 8, zones_enabled: all8, seasonal_percent: 100, rain_delay_until: 0, horizon_s: 7 * 86400 };
const BERLIN = 'CET-1CEST,M3.5.0,M10.5.0/3';

const cases: Case[] = [
  {
    ...base,
    name: 'utc_weekdays_and_odd',
    note: 'Weekday rule with two starts listed out of order, a disabled program, odd days without seasonal adjustment.',
    timezone: 'UTC0',
    now_utc: utc(2026, 6, 1, 5, 0, 30),
    programs: [
      { id: 1, name: 'Lawn', enabled: true, starts: ['18:30', '06:00'], days: { type: 'weekdays', weekdays: ['mon', 'wed', 'fri'] }, durations_s: [600, 0, 300], seasonal: true },
      { id: 2, name: 'Off', enabled: false, starts: ['07:00'], days: { type: 'weekdays' }, durations_s: [60] },
      { id: 3, name: 'Beds', enabled: true, starts: ['07:15'], days: { type: 'odd' }, durations_s: [0, 0, 0, 900], seasonal: false },
    ],
  },
  {
    ...base,
    name: 'berlin_spring_forward',
    note: 'Starts at 02:15, 02:30 and 03:00 on the change day all fall at the end of the gap (01:00Z) and run once. 01:45 is before the gap.',
    timezone: BERLIN,
    now_utc: utc(2026, 3, 27, 12, 0, 30),
    horizon_s: 4 * 86400,
    programs: [
      { id: 7, name: 'Gap', starts: ['02:30', '02:15', '03:00'], days: { type: 'weekdays' }, durations_s: [300] },
      { id: 8, name: 'Before gap', starts: ['01:45'], days: { type: 'even' }, durations_s: [0, 120] },
    ],
  },
  {
    ...base,
    name: 'berlin_fall_back',
    note: '02:30 on 2026-10-25 exists twice; it runs once, at the first occurrence (00:30Z, CEST).',
    timezone: BERLIN,
    now_utc: utc(2026, 10, 23, 12, 0, 30),
    horizon_s: 4 * 86400,
    programs: [{ id: 4, name: 'Repeat', starts: ['02:30'], days: { type: 'weekdays' }, durations_s: [60, 60] }],
  },
  {
    ...base,
    name: 'new_york_interval_default_dst_rule',
    note: 'DST name without rules takes the US rule M3.2.0,M11.1.0; interval every 3 days from an anchor in the past.',
    timezone: 'EST5EDT',
    now_utc: utc(2026, 3, 5, 15, 0, 30),
    programs: [{ id: 11, name: 'Every 3', starts: ['23:30', '05:00'], days: { type: 'interval', every: 3, anchor: '2026-03-01' }, durations_s: [0, 0, 0, 0, 0, 0, 0, 1800] }],
  },
  {
    ...base,
    name: 'sydney_dst_end',
    note: 'Southern hemisphere: DST ends 2026-04-05 03:00 local; 02:30 that day is repeated and runs once (15:30Z on the 4th). Even days on a second program.',
    timezone: 'AEST-10AEDT,M10.1.0,M4.1.0/3',
    now_utc: utc(2026, 4, 2, 6, 0, 30),
    programs: [
      { id: 5, name: 'Daily', starts: ['02:30'], days: { type: 'weekdays' }, durations_s: [450] },
      { id: 6, name: 'Even', starts: ['20:00'], days: { type: 'even' }, durations_s: [0, 450] },
    ],
  },
  {
    ...base,
    name: 'seasonal_and_zone_limits',
    note: 'Seasonal 150 % with rounding, a disabled zone, durations past zone_count are ignored, a program with seasonal off.',
    timezone: 'UTC0',
    zone_count: 4,
    zones_enabled: [true, false, true, true, true, true, true, true],
    seasonal_percent: 150,
    now_utc: utc(2026, 7, 10, 0, 0, 30),
    horizon_s: 3 * 86400,
    programs: [
      { id: 1, name: 'Scaled', starts: ['06:00'], days: { type: 'weekdays' }, durations_s: [601, 600, 33, 0, 900, 900], seasonal: true },
      { id: 2, name: 'Fixed', starts: ['06:00'], days: { type: 'weekdays' }, durations_s: [0, 0, 0, 120], seasonal: false },
    ],
  },
  {
    ...base,
    name: 'seasonal_zero_nothing_to_run',
    note: 'Seasonal 0 % leaves nothing to run for seasonal programs; the device skips them (nothing_to_run).',
    timezone: 'UTC0',
    seasonal_percent: 0,
    now_utc: utc(2026, 7, 10, 0, 0, 30),
    horizon_s: 2 * 86400,
    programs: [
      { id: 1, name: 'Scaled', starts: ['06:00'], days: { type: 'weekdays' }, durations_s: [600], seasonal: true },
      { id: 2, name: 'Fixed', starts: ['07:00'], days: { type: 'weekdays' }, durations_s: [600], seasonal: false },
    ],
  },
  {
    ...base,
    name: 'rain_delay',
    note: 'Starts before the end of the rain delay are skipped (rain_delay).',
    timezone: BERLIN,
    now_utc: utc(2026, 6, 1, 3, 0, 30),
    rain_delay_until: utc(2026, 6, 2, 15, 0, 0),
    horizon_s: 4 * 86400,
    programs: [{ id: 9, name: 'Daily', starts: ['06:00', '17:30'], days: { type: 'weekdays' }, durations_s: [300] }],
  },
  {
    ...base,
    name: 'weekday_lists',
    note: 'An empty weekday list never runs; a missing one means every day.',
    timezone: 'UTC0',
    now_utc: utc(2026, 6, 3, 0, 0, 30),
    horizon_s: 3 * 86400,
    programs: [
      { id: 1, name: 'Never', starts: ['06:00'], days: { type: 'weekdays', weekdays: [] }, durations_s: [60] },
      { id: 2, name: 'Every day', starts: ['06:00'], days: { type: 'weekdays' }, durations_s: [60] },
    ],
  },
  {
    ...base,
    name: 'leap_year_odd_even',
    note: 'Odd days across 2028-02-29 (odd) and 03-01 (odd): two odd days in a row. Even days skip both.',
    timezone: 'UTC0',
    now_utc: utc(2028, 2, 26, 12, 0, 30),
    horizon_s: 5 * 86400,
    programs: [
      { id: 1, name: 'Odd', starts: ['05:00'], days: { type: 'odd' }, durations_s: [60] },
      { id: 2, name: 'Even', starts: ['05:10'], days: { type: 'even' }, durations_s: [60] },
    ],
  },
  {
    ...base,
    name: 'interval_anchor_in_future_and_year_end',
    note: 'An anchor after today counts backwards too; the window crosses New Year.',
    timezone: 'UTC0',
    now_utc: utc(2026, 12, 28, 8, 0, 30),
    programs: [{ id: 3, name: 'Every 2', starts: ['04:00'], days: { type: 'interval', every: 2, anchor: '2027-01-15' }, durations_s: [60] }],
  },
  {
    ...base,
    name: 'half_hour_offset',
    note: 'Quoted zone name with a half-hour offset (India, no DST). 00:00 local is 18:30Z the day before.',
    timezone: '<+0530>-5:30',
    now_utc: utc(2026, 6, 1, 17, 0, 30),
    horizon_s: 3 * 86400,
    programs: [{ id: 2, name: 'Midnight', starts: ['00:00'], days: { type: 'weekdays', weekdays: ['tue', 'thu'] }, durations_s: [60] }],
  },
];

const vectors = {
  description:
    'Schedule preview vectors shared by the web UI (web/tests/unit/preview.test.ts) and the C++ Scheduler ' +
    '(test/host/schedule_vectors_test.cpp). Generated by web/scripts/gen-vectors.ts; expected starts list every ' +
    'start in (now_utc, now_utc + horizon_s]. zones are [zone (1-based), seconds after seasonal adjustment], empty for a skipped start.',
  cases: cases.map((c) => {
    const tz = TimeZone.parse(c.timezone);
    if (!tz) throw new Error(`bad zone in ${c.name}`);
    const runs = previewRuns({
      programs: c.programs,
      now: c.now_utc,
      tz,
      zoneCount: c.zone_count,
      zoneEnabled: c.zones_enabled,
      seasonalPercent: c.seasonal_percent,
      rainDelayUntil: c.rain_delay_until,
      horizonSec: c.horizon_s,
    });
    return {
      ...c,
      expected: runs.map((r) => ({
        program_id: r.programId,
        at_utc: r.at,
        // A skipped start queues nothing, so the device has no durations for it.
        zones: r.skip ? [] : r.zones.map((z) => [z.zone, z.seconds]),
        skip: r.skip ?? null,
      })),
    };
  }),
};

const text = JSON.stringify(vectors, null, 1) + '\n';
if (process.argv.includes('--check')) {
  if (readFileSync(out, 'utf8') !== text) {
    console.error('test/vectors/schedule_preview.json is stale; run `node scripts/gen-vectors.ts`.');
    process.exit(1);
  }
  console.log('schedule vectors up to date');
} else {
  writeFileSync(out, text);
  console.log(`wrote ${out}`);
}

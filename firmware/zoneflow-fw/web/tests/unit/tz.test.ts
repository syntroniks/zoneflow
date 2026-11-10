import { describe, expect, it } from 'vitest';
import { civilFromDays, daysFromCivil, parseClockTime, parseIsoDate, weekdayFromDays } from '../../src/lib/civil.ts';
import { TimeZone } from '../../src/lib/tz.ts';

describe('civil', () => {
  it('round-trips day serials', () => {
    for (let d = -800_000; d < 800_000; d += 997) {
      const c = civilFromDays(d);
      expect(daysFromCivil(c.year, c.month, c.day)).toBe(d);
    }
  });
  it('knows weekdays', () => {
    expect(weekdayFromDays(0)).toBe(4); // 1970-01-01 Thursday
    expect(weekdayFromDays(daysFromCivil(2026, 6, 1))).toBe(1); // Monday
  });
  it('parses like the firmware', () => {
    expect(parseClockTime('23:59')).toBe(1439);
    expect(parseClockTime('24:00')).toBeNull();
    expect(parseClockTime('6:00')).toBeNull();
    expect(parseIsoDate('2028-02-29')).toBe(daysFromCivil(2028, 2, 29));
    expect(parseIsoDate('2026-02-29')).toBeNull();
  });
});

// Same cases as test/host/time_zone_test.cpp where they overlap.
describe('TimeZone.parse', () => {
  it.each([
    ['UTC0', true],
    ['CET-1CEST,M3.5.0,M10.5.0/3', true],
    ['EST5EDT', true],
    ['<+0530>-5:30', true],
    ['IST-2IDT,M3.4.4/26,M10.5.0', true],
    ['', false],
    ['UT0', false], // names need 3 letters
    ['GMT0', true],
    ['U0', false],
    ['CET', false],
    ['CET-1CEST,M3.5.0', false],
    ['CET-1CEST,M13.5.0,M10.5.0', false],
    ['Europe/Berlin', false],
  ])('%s -> %s', (s, ok) => expect(TimeZone.parse(s) !== null).toBe(ok));

  it('computes Berlin offsets around the 2026 changes', () => {
    const tz = TimeZone.parse('CET-1CEST,M3.5.0,M10.5.0/3')!;
    const start = daysFromCivil(2026, 3, 29) * 86400 + 3600; // 01:00Z
    expect(tz.offsetAt(start - 1)).toBe(3600);
    expect(tz.offsetAt(start)).toBe(7200);
    const end = daysFromCivil(2026, 10, 25) * 86400 + 3600;
    expect(tz.offsetAt(end - 1)).toBe(7200);
    expect(tz.offsetAt(end)).toBe(3600);
  });

  it('handles the southern hemisphere', () => {
    const tz = TimeZone.parse('AEST-10AEDT,M10.1.0,M4.1.0/3')!;
    expect(tz.isDstAt(daysFromCivil(2026, 1, 15) * 86400)).toBe(true);
    expect(tz.isDstAt(daysFromCivil(2026, 7, 15) * 86400)).toBe(false);
  });
});

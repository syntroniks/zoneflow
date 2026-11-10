import { describe, expect, it } from 'vitest';
import { daysFromCivil } from '../../src/lib/civil.ts';
import { formatCountdown, formatDayLabel, formatDuration, formatOffset, formatTime } from '../../src/lib/format.ts';
import { TimeZone } from '../../src/lib/tz.ts';

describe('format', () => {
  it('formats durations', () => {
    expect(formatDuration(45)).toBe('45 s');
    expect(formatDuration(600)).toBe('10 min');
    expect(formatDuration(450)).toBe('7 min 30 s');
    expect(formatDuration(3900)).toBe('1 h 5 min');
    expect(formatDuration(7200)).toBe('2 h');
    expect(formatDuration(3 * 86400 + 7200)).toBe('3 d 2 h');
    expect(formatDuration(-5)).toBe('0 s');
  });
  it('formats countdowns', () => {
    expect(formatCountdown(0)).toBe('0:00');
    expect(formatCountdown(59.2)).toBe('1:00');
    expect(formatCountdown(754)).toBe('12:34');
    expect(formatCountdown(3723)).toBe('1:02:03');
  });
  it('shows times in the controller zone, not the machine zone', () => {
    const tz = TimeZone.parse('CET-1CEST,M3.5.0,M10.5.0/3')!;
    const t = daysFromCivil(2026, 6, 15) * 86400 + 4 * 3600; // 04:00Z = 06:00 CEST
    expect(formatTime(t, tz)).toMatch(/^0?6:00/);
    expect(formatDayLabel(t, tz, t - 3600)).toBe('Today');
    expect(formatDayLabel(t + 86400, tz, t)).toBe('Tomorrow');
  });
  it('formats offsets', () => {
    expect(formatOffset(19800)).toBe('+05:30');
    expect(formatOffset(-18000)).toBe('−05:00');
  });
});

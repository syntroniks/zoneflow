import { describe, expect, it } from 'vitest';
import { TIME_ZONES } from '../../src/lib/timezones.ts';
import { TimeZone } from '../../src/lib/tz.ts';

// Offset in seconds east of UTC that ICU (the browser's tz database) gives.
const formatters = new Map<string, Intl.DateTimeFormat>();
function icuOffset(zone: string, utc: number): number {
  let f = formatters.get(zone);
  if (!f) {
    f = new Intl.DateTimeFormat('en-US', { timeZone: zone, timeZoneName: 'longOffset' });
    formatters.set(zone, f);
  }
  const s = f
    .formatToParts(new Date(utc * 1000))
    .find((p) => p.type === 'timeZoneName')!.value; // "GMT+05:30" or "GMT"
  const m = /GMT([+-])(\d{2}):(\d{2})/.exec(s);
  if (!m) return 0;
  return (m[1] === '-' ? -1 : 1) * (Number(m[2]) * 3600 + Number(m[3]) * 60);
}

describe('time zone list', () => {
  it.each(TIME_ZONES)('%s (%s) matches the tz database through 2026-2028', (iana, posix) => {
    const tz = TimeZone.parse(posix);
    expect(tz).not.toBeNull();
    const start = Date.UTC(2026, 0, 1) / 1000;
    const end = Date.UTC(2029, 0, 1) / 1000;
    const bad: string[] = [];
    for (let t = start; t < end; t += 3600) {
      const want = icuOffset(iana, t);
      const got = tz!.offsetAt(t);
      if (want !== got) bad.push(`${new Date(t * 1000).toISOString()} icu ${want} ours ${got}`);
      if (bad.length > 3) break;
    }
    expect(bad).toEqual([]);
  });
});

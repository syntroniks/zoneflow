import { afterEach, describe, expect, it } from 'vitest';
import { clockFormat, localeUses24h } from '../../src/lib/clock.ts';
import { formatMinutes, formatTime } from '../../src/lib/format.ts';
import { TimeZone } from '../../src/lib/tz.ts';

afterEach(() => {
  clockFormat.value = 'auto';
});

describe('clock format', () => {
  it('knows which locales use 24-hour time', () => {
    expect(localeUses24h('de-DE')).toBe(true);
    expect(localeUses24h('en-US')).toBe(false);
  });
  it('one setting drives every displayed time', () => {
    const utc = TimeZone.utc();
    clockFormat.value = '24';
    expect(formatMinutes(18 * 60 + 30)).toBe('18:30');
    expect(formatTime(18 * 3600 + 30 * 60, utc)).toBe('18:30');
    clockFormat.value = '12';
    expect(formatMinutes(18 * 60 + 30)).toMatch(/^6:30\s?PM$/i);
    expect(formatTime(18 * 3600 + 30 * 60, utc)).toMatch(/^6:30\s?PM$/i);
    expect(formatMinutes(0)).toMatch(/^12:00\s?AM$/i);
  });
});

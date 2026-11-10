// One setting for how times are shown and entered: the browser's convention,
// 24-hour or 12-hour. Kept per browser (a viewing preference, not controller
// state); the controller always stores 24-hour "HH:MM".
import { signal } from '@preact/signals';

export type ClockFormat = 'auto' | '24' | '12';
const KEY = 'zoneflow.clock';

function load(): ClockFormat {
  try {
    const v = localStorage.getItem(KEY);
    return v === '24' || v === '12' ? v : 'auto';
  } catch {
    return 'auto';
  }
}

export const clockFormat = signal<ClockFormat>(typeof localStorage === 'undefined' ? 'auto' : load());

export function setClockFormat(f: ClockFormat): void {
  clockFormat.value = f;
  try {
    localStorage.setItem(KEY, f);
  } catch {
    // private mode: this visit only
  }
}

/** The browser locale's own convention. */
export function localeUses24h(locale?: string): boolean {
  try {
    const parts = new Intl.DateTimeFormat(locale, { hour: 'numeric' }).formatToParts(new Date(Date.UTC(2020, 0, 1, 15)));
    return !parts.some((p) => p.type === 'dayPeriod');
  } catch {
    return true;
  }
}

export function uses24h(): boolean {
  const f = clockFormat.value;
  if (f === 'auto') return localeUses24h(typeof navigator === 'undefined' ? undefined : navigator.language);
  return f === '24';
}

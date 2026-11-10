// Formatting of durations and of controller-local times. Times are shown in
// the controller's time zone (its POSIX TZ string), not the phone's: a start
// at 06:00 is 06:00 in the garden wherever the phone is.
import { t } from '../i18n/index.ts';
import { pad2 } from './civil.ts';
import type { TimeZone } from './tz.ts';

/** Human duration: "45 s", "12 min", "7 min 30 s", "1 h 5 min", "2 d 3 h". */
export function formatDuration(totalSec: number): string {
  const s = Math.max(0, Math.round(totalSec));
  if (s < 60) return t('dur_s', { s });
  const m = Math.floor(s / 60);
  const rs = s % 60;
  if (m < 60) return rs ? t('dur_ms', { m, s: rs }) : t('dur_m', { m });
  const h = Math.floor(m / 60);
  const rm = m % 60;
  if (h < 48) return rm ? t('dur_hm', { h, m: rm }) : t('dur_h', { h });
  const d = Math.floor(h / 24);
  const rh = h % 24;
  return rh ? t('dur_dh', { d, h: rh }) : t('dur_d', { d });
}

/** Countdown "m:ss" or "h:mm:ss". */
export function formatCountdown(totalSec: number): string {
  const s = Math.max(0, Math.ceil(totalSec));
  const h = Math.floor(s / 3600);
  const m = Math.floor((s % 3600) / 60);
  const r = s % 60;
  return h ? `${h}:${pad2(m)}:${pad2(r)}` : `${m}:${pad2(r)}`;
}

function locale(): string | undefined {
  return typeof navigator === 'undefined' ? undefined : navigator.language;
}

/** A Date whose UTC fields are the controller's local wall clock. */
function shifted(utc: number, tz: TimeZone): Date {
  return new Date((utc + tz.offsetAt(utc)) * 1000);
}

const fmtCache = new Map<string, Intl.DateTimeFormat>();
function fmt(opts: Intl.DateTimeFormatOptions): Intl.DateTimeFormat {
  const key = JSON.stringify(opts);
  let f = fmtCache.get(key);
  if (!f) {
    f = new Intl.DateTimeFormat(locale(), { ...opts, timeZone: 'UTC' });
    fmtCache.set(key, f);
  }
  return f;
}

/** Wall-clock time at the controller, in the phone's 12/24 h convention. */
export function formatTime(utc: number, tz: TimeZone): string {
  return fmt({ hour: 'numeric', minute: '2-digit' }).format(shifted(utc, tz));
}

export function formatDate(utc: number, tz: TimeZone): string {
  return fmt({ weekday: 'short', day: 'numeric', month: 'short' }).format(shifted(utc, tz));
}

export function formatDateTime(utc: number, tz: TimeZone): string {
  return fmt({ weekday: 'short', day: 'numeric', month: 'short', hour: 'numeric', minute: '2-digit' }).format(
    shifted(utc, tz),
  );
}

export function formatWeekdayShort(daySerial: number): string {
  return fmt({ weekday: 'short' }).format(new Date(daySerial * 86400 * 1000));
}

/** Day label relative to the controller's today: "Today", "Tomorrow" or "Wed 3 Jun". */
export function formatDayLabel(utc: number, tz: TimeZone, nowUtc: number): string {
  const d = tz.localDay(utc) - tz.localDay(nowUtc);
  if (d === 0) return t('today');
  if (d === 1) return t('tomorrow');
  return formatDate(utc, tz);
}

/** "Today 06:00", "Tomorrow 18:30", "Wed 3 Jun 06:00". */
export function formatWhen(utc: number, tz: TimeZone, nowUtc: number): string {
  return `${formatDayLabel(utc, tz, nowUtc)} ${formatTime(utc, tz)}`;
}

/** "+01:00" */
export function formatOffset(sec: number): string {
  const sign = sec < 0 ? '−' : '+';
  const a = Math.abs(sec);
  return `${sign}${pad2(Math.floor(a / 3600))}:${pad2(Math.floor((a % 3600) / 60))}`;
}

export function formatNumber(n: number, digits = 0): string {
  return new Intl.NumberFormat(locale(), { maximumFractionDigits: digits, minimumFractionDigits: digits }).format(n);
}

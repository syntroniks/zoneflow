// Proleptic Gregorian calendar on day serials (days since 1970-01-01).
// Port of zf_core/include/zf/time/civil.hpp (after H. Hinnant). Keep the two
// in step: the schedule preview must give the device's answers.

export const SECONDS_PER_DAY = 86400;

export interface CivilDate {
  year: number;
  month: number; // 1..12
  day: number; // 1..31
}

export function floorDiv(a: number, b: number): number {
  return Math.floor(a / b);
}

export function floorMod(a: number, b: number): number {
  return a - floorDiv(a, b) * b;
}

export function daysFromCivil(y: number, m: number, d: number): number {
  y -= m <= 2 ? 1 : 0;
  const era = floorDiv(y, 400);
  const yoe = y - era * 400;
  const doy = Math.floor((153 * (m + (m > 2 ? -3 : 9)) + 2) / 5) + d - 1;
  const doe = yoe * 365 + Math.floor(yoe / 4) - Math.floor(yoe / 100) + doy;
  return era * 146097 + doe - 719468;
}

export function civilFromDays(z: number): CivilDate {
  z += 719468;
  const era = floorDiv(z, 146097);
  const doe = z - era * 146097;
  const yoe = Math.floor((doe - Math.floor(doe / 1460) + Math.floor(doe / 36524) - Math.floor(doe / 146096)) / 365);
  const y = yoe + era * 400;
  const doy = doe - (365 * yoe + Math.floor(yoe / 4) - Math.floor(yoe / 100));
  const mp = Math.floor((5 * doy + 2) / 153);
  const d = doy - Math.floor((153 * mp + 2) / 5) + 1;
  const m = mp < 10 ? mp + 3 : mp - 9;
  return { year: y + (m <= 2 ? 1 : 0), month: m, day: d };
}

/** 0 = Sunday ... 6 = Saturday. */
export function weekdayFromDays(z: number): number {
  return floorMod(z + 4, 7);
}

export function isLeapYear(y: number): boolean {
  return (y % 4 === 0 && y % 100 !== 0) || y % 400 === 0;
}

const DAYS_IN_MONTH = [31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31];

export function daysInMonth(y: number, m: number): number {
  return m === 2 && isLeapYear(y) ? 29 : (DAYS_IN_MONTH[m - 1] ?? 30);
}

/** "YYYY-MM-DD" to a day serial, as zf_core parseIsoDate; null if invalid. */
export function parseIsoDate(s: string): number | null {
  const m = /^(\d{4})-(\d{2})-(\d{2})$/.exec(s);
  if (!m) return null;
  const y = Number(m[1]);
  const mo = Number(m[2]);
  const d = Number(m[3]);
  if (mo < 1 || mo > 12 || d < 1 || d > daysInMonth(y, mo)) return null;
  return daysFromCivil(y, mo, d);
}

export function formatIsoDate(day: number): string {
  const c = civilFromDays(day);
  return `${String(c.year).padStart(4, '0')}-${pad2(c.month)}-${pad2(c.day)}`;
}

/** "HH:MM" to minutes after midnight, as zf_core parseClockTime; null if invalid. */
export function parseClockTime(s: string): number | null {
  const m = /^([0-2][0-9]):([0-5][0-9])$/.exec(s);
  if (!m) return null;
  const h = Number(m[1]);
  if (h > 23) return null;
  return h * 60 + Number(m[2]);
}

export function formatClockTime(minutes: number): string {
  return `${pad2(Math.floor(minutes / 60))}:${pad2(minutes % 60)}`;
}

export function pad2(n: number): string {
  return String(n).padStart(2, '0');
}

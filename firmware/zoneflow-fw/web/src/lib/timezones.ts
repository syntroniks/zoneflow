// Common time zones and their POSIX TZ strings (the form the controller
// stores, from the last line of each tzdata zone file). Checked against the
// browser's own time zone data by tests/unit/timezones.test.ts.
// Zones whose rules are changing in current tzdata releases (for example
// America/Vancouver, permanent UTC-7 in tzdata 2026e) are left out; the
// POSIX field takes them by hand.
export const TIME_ZONES: ReadonlyArray<readonly [iana: string, posix: string]> = [
  ['UTC', 'UTC0'],
  ['Europe/London', 'GMT0BST,M3.5.0/1,M10.5.0'],
  ['Europe/Dublin', 'GMT0IST,M3.5.0/1,M10.5.0'],
  ['Europe/Lisbon', 'WET0WEST,M3.5.0/1,M10.5.0'],
  ['Europe/Amsterdam', 'CET-1CEST,M3.5.0,M10.5.0/3'],
  ['Europe/Berlin', 'CET-1CEST,M3.5.0,M10.5.0/3'],
  ['Europe/Brussels', 'CET-1CEST,M3.5.0,M10.5.0/3'],
  ['Europe/Copenhagen', 'CET-1CEST,M3.5.0,M10.5.0/3'],
  ['Europe/Madrid', 'CET-1CEST,M3.5.0,M10.5.0/3'],
  ['Europe/Oslo', 'CET-1CEST,M3.5.0,M10.5.0/3'],
  ['Europe/Paris', 'CET-1CEST,M3.5.0,M10.5.0/3'],
  ['Europe/Prague', 'CET-1CEST,M3.5.0,M10.5.0/3'],
  ['Europe/Rome', 'CET-1CEST,M3.5.0,M10.5.0/3'],
  ['Europe/Stockholm', 'CET-1CEST,M3.5.0,M10.5.0/3'],
  ['Europe/Vienna', 'CET-1CEST,M3.5.0,M10.5.0/3'],
  ['Europe/Warsaw', 'CET-1CEST,M3.5.0,M10.5.0/3'],
  ['Europe/Zurich', 'CET-1CEST,M3.5.0,M10.5.0/3'],
  ['Europe/Athens', 'EET-2EEST,M3.5.0/3,M10.5.0/4'],
  ['Europe/Bucharest', 'EET-2EEST,M3.5.0/3,M10.5.0/4'],
  ['Europe/Helsinki', 'EET-2EEST,M3.5.0/3,M10.5.0/4'],
  ['Europe/Kyiv', 'EET-2EEST,M3.5.0/3,M10.5.0/4'],
  ['Europe/Istanbul', '<+03>-3'],
  ['Europe/Moscow', 'MSK-3'],
  ['Atlantic/Reykjavik', 'GMT0'],
  ['Africa/Cairo', 'EET-2EEST,M4.5.5/0,M10.5.4/24'],
  ['Africa/Johannesburg', 'SAST-2'],
  ['Africa/Lagos', 'WAT-1'],
  ['Africa/Nairobi', 'EAT-3'],
  ['Asia/Dubai', '<+04>-4'],
  ['Asia/Jerusalem', 'IST-2IDT,M3.4.4/26,M10.5.0'],
  ['Asia/Riyadh', '<+03>-3'],
  ['Asia/Tehran', '<+0330>-3:30'],
  ['Asia/Karachi', 'PKT-5'],
  ['Asia/Kolkata', 'IST-5:30'],
  ['Asia/Kathmandu', '<+0545>-5:45'],
  ['Asia/Dhaka', '<+06>-6'],
  ['Asia/Bangkok', '<+07>-7'],
  ['Asia/Jakarta', 'WIB-7'],
  ['Asia/Singapore', '<+08>-8'],
  ['Asia/Shanghai', 'CST-8'],
  ['Asia/Hong_Kong', 'HKT-8'],
  ['Asia/Manila', 'PST-8'],
  ['Asia/Taipei', 'CST-8'],
  ['Asia/Seoul', 'KST-9'],
  ['Asia/Tokyo', 'JST-9'],
  ['Australia/Perth', 'AWST-8'],
  ['Australia/Darwin', 'ACST-9:30'],
  ['Australia/Adelaide', 'ACST-9:30ACDT,M10.1.0,M4.1.0/3'],
  ['Australia/Brisbane', 'AEST-10'],
  ['Australia/Sydney', 'AEST-10AEDT,M10.1.0,M4.1.0/3'],
  ['Australia/Melbourne', 'AEST-10AEDT,M10.1.0,M4.1.0/3'],
  ['Australia/Hobart', 'AEST-10AEDT,M10.1.0,M4.1.0/3'],
  ['Pacific/Auckland', 'NZST-12NZDT,M9.5.0,M4.1.0/3'],
  ['Pacific/Honolulu', 'HST10'],
  ['America/Anchorage', 'AKST9AKDT,M3.2.0,M11.1.0'],
  ['America/Los_Angeles', 'PST8PDT,M3.2.0,M11.1.0'],
  ['America/Phoenix', 'MST7'],
  ['America/Denver', 'MST7MDT,M3.2.0,M11.1.0'],
  ['America/Chicago', 'CST6CDT,M3.2.0,M11.1.0'],
  ['America/Mexico_City', 'CST6'],
  ['America/New_York', 'EST5EDT,M3.2.0,M11.1.0'],
  ['America/Toronto', 'EST5EDT,M3.2.0,M11.1.0'],
  ['America/Halifax', 'AST4ADT,M3.2.0,M11.1.0'],
  ['America/St_Johns', 'NST3:30NDT,M3.2.0,M11.1.0'],
  ['America/Bogota', '<-05>5'],
  ['America/Lima', '<-05>5'],
  ['America/Santiago', '<-04>4<-03>,M9.1.6/24,M4.1.6/24'],
  ['America/Sao_Paulo', '<-03>3'],
  ['America/Argentina/Buenos_Aires', '<-03>3'],
];

/** Display label: "Europe/Berlin" -> "Berlin (Europe)". */
export function zoneLabel(iana: string): string {
  if (iana === 'UTC') return 'UTC';
  const parts = iana.split('/');
  const city = (parts[parts.length - 1] ?? iana).replace(/_/g, ' ');
  return `${city} (${parts[0]})`;
}

/** The browser's zone, if it is in the list. */
export function guessZone(): readonly [string, string] | undefined {
  try {
    const name = Intl.DateTimeFormat().resolvedOptions().timeZone;
    return TIME_ZONES.find(([z]) => z === name);
  } catch {
    return undefined;
  }
}

/** The first listed zone using this POSIX string, for showing a stored setting. */
export function zoneForPosix(posix: string): string | undefined {
  const guess = guessZone();
  if (guess && guess[1] === posix) return guess[0];
  return TIME_ZONES.find(([, p]) => p === posix)?.[0];
}

// POSIX TZ time zone ("CET-1CEST,M3.5.0,M10.5.0/3"). Port of
// zf_core/src/time_zone.cpp so the browser computes the same local times and
// start instants as the controller, whatever zone the phone itself is in.
import {
  SECONDS_PER_DAY,
  civilFromDays,
  daysFromCivil,
  daysInMonth,
  floorDiv,
  isLeapYear,
  weekdayFromDays,
  type CivilDate,
} from './civil.ts';

type RuleKind = 'J1' | 'J0' | 'M';

interface Rule {
  kind: RuleKind;
  day: number;
  month: number;
  week: number;
  weekday: number;
  timeSec: number;
}

export interface LocalDateTime {
  date: CivilDate;
  daySerial: number;
  secondOfDay: number;
  weekday: number; // 0 = Sunday
  utcOffsetSec: number;
  dst: boolean;
}

const isDigit = (c: string) => c >= '0' && c <= '9';
const isAlpha = (c: string) => (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');

class Cursor {
  private i = 0;
  private readonly s: string;
  constructor(s: string) {
    this.s = s;
  }
  done() {
    return this.i >= this.s.length;
  }
  peek() {
    return this.done() ? '\0' : (this.s[this.i] as string);
  }
  consume(c: string) {
    if (this.done() || this.peek() !== c) return false;
    this.i++;
    return true;
  }
  name(): boolean {
    let n = 0;
    if (this.consume('<')) {
      while (!this.done() && this.peek() !== '>') {
        const c = this.peek();
        if (!(isAlpha(c) || isDigit(c) || c === '+' || c === '-')) return false;
        this.i++;
        n++;
      }
      return this.consume('>') && n >= 3;
    }
    while (isAlpha(this.peek())) {
      this.i++;
      n++;
    }
    return n >= 3;
  }
  number(max: number): number | null {
    if (!isDigit(this.peek())) return null;
    let v = 0;
    let digits = 0;
    while (isDigit(this.peek())) {
      v = v * 10 + (this.peek().charCodeAt(0) - 48);
      this.i++;
      if (++digits > 3 || v > max) return null;
    }
    return v;
  }
  hms(maxHours: number): number | null {
    let sign = 1;
    if (this.peek() === '+' || this.peek() === '-') {
      sign = this.peek() === '-' ? -1 : 1;
      this.i++;
    }
    const h = this.number(maxHours);
    if (h === null) return null;
    let m = 0;
    let s = 0;
    if (this.consume(':')) {
      const mm = this.number(59);
      if (mm === null) return null;
      m = mm;
      if (this.consume(':')) {
        const ss = this.number(59);
        if (ss === null) return null;
        s = ss;
      }
    }
    return sign * (h * 3600 + m * 60 + s);
  }
  atOffset() {
    const c = this.peek();
    return c === '+' || c === '-' || isDigit(c);
  }
}

function parseRule(c: Cursor): Rule | null {
  const rule: Rule = { kind: 'M', day: 0, month: 0, week: 0, weekday: 0, timeSec: 7200 };
  if (c.consume('J')) {
    const v = c.number(365);
    if (v === null || v < 1) return null;
    rule.kind = 'J1';
    rule.day = v;
  } else if (c.consume('M')) {
    const m = c.number(12);
    if (m === null || m < 1 || !c.consume('.')) return null;
    const w = c.number(5);
    if (w === null || w < 1 || !c.consume('.')) return null;
    const d = c.number(6);
    if (d === null) return null;
    rule.month = m;
    rule.week = w;
    rule.weekday = d;
  } else if (isDigit(c.peek())) {
    const v = c.number(365);
    if (v === null) return null;
    rule.kind = 'J0';
    rule.day = v;
  } else {
    return null;
  }
  if (c.consume('/')) {
    const t = c.hms(167);
    if (t === null) return null;
    rule.timeSec = t;
  }
  return rule;
}

function ruleLocalSeconds(year: number, rule: Rule): number {
  const jan1 = daysFromCivil(year, 1, 1);
  let day = jan1;
  if (rule.kind === 'J1') {
    day = jan1 + rule.day - 1 + (isLeapYear(year) && rule.day >= 60 ? 1 : 0);
  } else if (rule.kind === 'J0') {
    day = jan1 + rule.day;
  } else {
    const first = daysFromCivil(year, rule.month, 1);
    const firstWd = weekdayFromDays(first);
    let dom = 1 + ((rule.weekday + 7 - firstWd) % 7) + (rule.week - 1) * 7;
    const dim = daysInMonth(year, rule.month);
    while (dom > dim) dom -= 7;
    day = first + dom - 1;
  }
  return day * SECONDS_PER_DAY + rule.timeSec;
}

const US_START: Rule = { kind: 'M', day: 0, month: 3, week: 2, weekday: 0, timeSec: 7200 };
const US_END: Rule = { kind: 'M', day: 0, month: 11, week: 1, weekday: 0, timeSec: 7200 };

export class TimeZone {
  readonly stdOffset: number;
  readonly dstOffset: number;
  readonly hasDst: boolean;
  private readonly start: Rule;
  private readonly end: Rule;

  private constructor(stdOffset: number, dstOffset: number, hasDst: boolean, start: Rule, end: Rule) {
    this.stdOffset = stdOffset;
    this.dstOffset = dstOffset;
    this.hasDst = hasDst;
    this.start = start;
    this.end = end;
  }

  static utc(): TimeZone {
    return new TimeZone(0, 0, false, US_START, US_END);
  }

  /** Same grammar as TimeZone::parse in zf_core; null when the device would refuse it. */
  static parse(posix: string): TimeZone | null {
    const c = new Cursor(posix);
    if (!c.name()) return null;
    const off = c.hms(24);
    if (off === null) return null;
    const std = -off;
    if (c.done()) return new TimeZone(std, std, false, US_START, US_END);
    if (!c.name()) return null;
    let dst = std + 3600;
    if (c.atOffset()) {
      const o = c.hms(24);
      if (o === null) return null;
      dst = -o;
    }
    if (c.done()) return new TimeZone(std, dst, true, US_START, US_END);
    if (!c.consume(',')) return null;
    const start = parseRule(c);
    if (!start || !c.consume(',')) return null;
    const end = parseRule(c);
    if (!end || !c.done()) return null;
    return new TimeZone(std, dst, true, start, end);
  }

  private transitions(year: number) {
    return {
      dstStart: ruleLocalSeconds(year, this.start) - this.stdOffset,
      dstEnd: ruleLocalSeconds(year, this.end) - this.dstOffset,
    };
  }

  isDstAt(t: number): boolean {
    if (!this.hasDst) return false;
    const year = civilFromDays(floorDiv(t + this.stdOffset, SECONDS_PER_DAY)).year;
    const tr = this.transitions(year);
    if (tr.dstStart < tr.dstEnd) return t >= tr.dstStart && t < tr.dstEnd;
    return !(t >= tr.dstEnd && t < tr.dstStart);
  }

  offsetAt(t: number): number {
    return this.isDstAt(t) ? this.dstOffset : this.stdOffset;
  }

  localDay(t: number): number {
    return floorDiv(t + this.offsetAt(t), SECONDS_PER_DAY);
  }

  toLocal(t: number): LocalDateTime {
    const dst = this.isDstAt(t);
    const off = dst ? this.dstOffset : this.stdOffset;
    const local = t + off;
    const day = floorDiv(local, SECONDS_PER_DAY);
    return {
      date: civilFromDays(day),
      daySerial: day,
      secondOfDay: local - day * SECONDS_PER_DAY,
      weekday: weekdayFromDays(day),
      utcOffsetSec: off,
      dst,
    };
  }

  /**
   * Local wall-clock time to UTC. Repeated hour: the earlier instant.
   * Skipped hour: the transition instant (first valid instant after the gap).
   */
  fromLocal(localDaySerial: number, secondOfDay: number): number {
    const local = localDaySerial * SECONDS_PER_DAY + secondOfDay;
    const viaStd = local - this.stdOffset;
    if (!this.hasDst) return viaStd;
    const viaDst = local - this.dstOffset;
    const stdValid = this.offsetAt(viaStd) === this.stdOffset;
    const dstValid = this.offsetAt(viaDst) === this.dstOffset;
    if (stdValid && dstValid) return Math.min(viaStd, viaDst);
    if (stdValid) return viaStd;
    if (dstValid) return viaDst;
    const tr = this.transitions(civilFromDays(localDaySerial).year);
    const dist = (x: number) => Math.abs(x - viaStd);
    return dist(tr.dstStart) <= dist(tr.dstEnd) ? tr.dstStart : tr.dstEnd;
  }
}

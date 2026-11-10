// Start-time entry that follows the one clock-format setting (lib/clock.ts):
// hour and minute (and AM/PM for 12-hour) as native selects, which give a
// wheel on phones and work with the keyboard. The value is always 24-hour
// "HH:MM", as the controller stores it.
import { useId } from 'preact/hooks';
import { t } from '../i18n/index.ts';
import { pad2, parseClockTime } from '../lib/civil.ts';
import { uses24h } from '../lib/clock.ts';

const MINUTES = Array.from({ length: 60 }, (_, i) => i);

export function TimeInput({ label, value, onChange, disabled, invalid, describedBy, id }: { label: string; value: string; onChange: (v: string) => void; disabled?: boolean; invalid?: boolean; describedBy?: string; id?: string }) {
  const gen = useId();
  const base = id ?? gen;
  const m = parseClockTime(value) ?? 0;
  const h = Math.floor(m / 60);
  const min = m % 60;
  const h24 = uses24h();
  const pm = h >= 12;
  const set = (hour: number, minute: number) => onChange(`${pad2(hour)}:${pad2(minute)}`);
  const common = { disabled, 'aria-invalid': invalid || undefined, 'aria-describedby': describedBy };
  return (
    <fieldset class="time-input" aria-describedby={describedBy}>
      <legend class="sr-only">{label}</legend>
      <select
        id={base}
        class="input"
        aria-label={`${label}: ${t('time_hour')}`}
        value={h24 ? h : h % 12 === 0 ? 12 : h % 12}
        onChange={(e) => {
          const v = Number((e.currentTarget as HTMLSelectElement).value);
          set(h24 ? v : (v % 12) + (pm ? 12 : 0), min);
        }}
        {...common}
      >
        {(h24 ? Array.from({ length: 24 }, (_, i) => i) : [12, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11]).map((x) => (
          <option key={x} value={x}>
            {h24 ? pad2(x) : x}
          </option>
        ))}
      </select>
      <span aria-hidden="true" class="time-sep">
        :
      </span>
      <select
        class="input"
        aria-label={`${label}: ${t('time_minute')}`}
        value={min}
        onChange={(e) => set(h, Number((e.currentTarget as HTMLSelectElement).value))}
        {...common}
      >
        {MINUTES.map((x) => (
          <option key={x} value={x}>
            {pad2(x)}
          </option>
        ))}
      </select>
      {!h24 && (
        <select
          class="input"
          aria-label={`${label}: ${t('time_ampm')}`}
          value={pm ? 'pm' : 'am'}
          onChange={(e) => set(((e.currentTarget as HTMLSelectElement).value === 'pm' ? 12 : 0) + (h % 12), min)}
          {...common}
        >
          <option value="am">{t('time_am')}</option>
          <option value="pm">{t('time_pm')}</option>
        </select>
      )}
    </fieldset>
  );
}

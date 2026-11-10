// Rain delay: skip scheduled programs until a time. Manual runs still work.
import { useState } from 'preact/hooks';
import { Banner, Button, Card, Choice, PageTitle, TextField, confirmAction } from '../components/ui.tsx';
import { t, tn } from '../i18n/index.ts';
import { formatIsoDate, parseIsoDate } from '../lib/civil.ts';
import { formatDateTime, formatDuration } from '../lib/format.ts';
import { clearRainDelay, setRainDelayHours, setRainDelayUntil } from '../state/actions.ts';
import { deviceNow, elapsedSec, status, timeZone } from '../state/store.ts';

const PRESETS = [12, 24, 48, 72, 168];

export function RainScreen() {
  const s = status.value;
  const rd = s?.rain_delay;
  const tz = timeZone.value;
  const [hours, setHours] = useState<number | -1>(24);
  const [date, setDate] = useState(() => formatIsoDate(tz.localDay(deviceNow()) + 2));
  const [busy, setBusy] = useState(false);
  const noClock = s?.time_status === 'unset';
  const today = tz.localDay(deviceNow());
  const day = parseIsoDate(date);
  const customUntil = day !== null ? tz.fromLocal(day, 0) : null;
  const customBad = hours === -1 && (customUntil === null || day! <= today || day! > today + 30);

  const label = (h: number) => (h % 24 === 0 ? tn('rain_days', h / 24) : tn('rain_hours', h));
  return (
    <div class="page narrow">
      <PageTitle title={t('rain_title')} back="/more" />
      <p class="muted">{t('rain_intro')}</p>
      <Card>
        {rd?.active && rd.until_utc ? (
          <div class="stack">
            <Banner severity="info" icon="rain" title={t('rain_until', { when: formatDateTime(rd.until_utc, tz) })}>
              {t('rain_remaining', { time: formatDuration(Math.max(0, (rd.remaining_s ?? 0) - elapsedSec())) })}
            </Banner>
            <Button
              variant="danger"
              block
              onClick={async () => {
                if (await confirmAction({ title: t('rain_clear'), body: t('rain_clear_confirm'), confirmLabel: t('rain_clear') })) void clearRainDelay();
              }}
            >
              {t('rain_clear')}
            </Button>
          </div>
        ) : (
          <p class="big-state">{t('rain_none')}</p>
        )}
      </Card>
      <Card title={t('rain_set_for')}>
        {noClock ? (
          <Banner severity="warning" title={t('rain_needs_time')} />
        ) : (
          <form
            class="stack"
            onSubmit={async (e) => {
              e.preventDefault();
              setBusy(true);
              if (hours === -1) {
                if (customUntil !== null) await setRainDelayUntil(customUntil);
              } else await setRainDelayHours(hours);
              setBusy(false);
            }}
          >
            <Choice
              kind="chips"
              label={t('rain_set_for')}
              hideLabel
              value={hours}
              onChange={setHours}
              options={[...PRESETS.map((h) => ({ value: h, label: label(h) })), { value: -1 as const, label: t('rain_custom') }]}
            />
            {hours === -1 && (
              <TextField
                label={t('rain_custom')}
                type="date"
                value={date}
                min={formatIsoDate(today + 1)}
                max={formatIsoDate(today + 30)}
                onInput={setDate}
                help={t('rain_max')}
                error={customBad ? t('rain_max') : null}
              />
            )}
            <Button variant="primary" type="submit" size="lg" block icon="rain" loading={busy} disabled={customBad}>
              {rd?.active ? `${t('rain_set')} (${hours === -1 ? date : label(hours)})` : t('rain_set')}
            </Button>
          </form>
        )}
      </Card>
    </div>
  );
}

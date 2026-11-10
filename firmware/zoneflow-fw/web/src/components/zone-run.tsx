// Duration picker and start button for a manual zone run, used by the run
// screen and the dashboard's quick-run sheet.
import { useState } from 'preact/hooks';
import { t } from '../i18n/index.ts';
import { formatDuration } from '../lib/format.ts';
import { runZone } from '../state/actions.ts';
import { lastRunMinutes, rememberRunMinutes } from '../state/schedule.ts';
import { status } from '../state/store.ts';
import type { ZoneInfo } from '../state/store.ts';
import { Button, Choice, Stepper } from './ui.tsx';

export const PRESET_MINUTES = [1, 5, 10, 15, 20, 30, 45, 60];

export function DurationPicker({ minutes, onChange }: { minutes: number; onChange: (m: number) => void }) {
  const preset = PRESET_MINUTES.includes(minutes);
  const [custom, setCustom] = useState(!preset);
  return (
    <div class="stack-sm">
      <Choice
        kind="chips"
        label={t('run_duration')}
        value={custom ? -1 : minutes}
        onChange={(v) => {
          if (v === -1) {
            setCustom(true);
          } else {
            setCustom(false);
            onChange(v);
          }
        }}
        options={[...PRESET_MINUTES.map((m) => ({ value: m, label: formatDuration(m * 60) })), { value: -1, label: t('run_custom') }]}
      />
      {custom && <Stepper label={t('run_custom_minutes')} value={minutes} min={1} max={240} onChange={onChange} unit={t('minutes_short')} />}
    </div>
  );
}

export function ZoneRunControls({ zone, onStarted }: { zone: ZoneInfo; onStarted?: () => void }) {
  // The zone's own default run time first; the last manual choice otherwise.
  const [minutes, setMinutes] = useState(() => (zone.defaultRunSec > 0 ? Math.max(1, Math.round(zone.defaultRunSec / 60)) : lastRunMinutes()));
  const [busy, setBusy] = useState(false);
  const capped = minutes * 60 > zone.maxRunSec;
  const busyQueue = status.value?.queue?.state && status.value.queue.state !== 'idle';
  const name = zone.name || t('zone_n', { n: zone.id });
  return (
    <div class="stack">
      <DurationPicker minutes={minutes} onChange={setMinutes} />
      {capped && <p class="note">{t('run_capped', { time: formatDuration(zone.maxRunSec) })}</p>}
      {busyQueue && <p class="note">{t('run_queued_note')}</p>}
      <Button
        variant="primary"
        size="lg"
        block
        icon="play"
        loading={busy}
        disabled={!zone.enabled}
        onClick={async () => {
          setBusy(true);
          rememberRunMinutes(minutes);
          const ok = await runZone(zone.id, minutes * 60);
          setBusy(false);
          if (ok) onStarted?.();
        }}
      >
        {t('run_go', { zone: name, time: formatDuration(Math.min(minutes * 60, zone.maxRunSec)) })}
      </Button>
    </div>
  );
}

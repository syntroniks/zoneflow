// Manual run: zone or program, how long, go. From the dashboard: Run tab,
// zone, start (the last duration is remembered) = three taps.
import { useState } from 'preact/hooks';
import { Button, Choice, Empty, PageTitle } from '../components/ui.tsx';
import { ZoneRunControls } from '../components/zone-run.tsx';
import { t } from '../i18n/index.ts';
import { adjustedSeconds } from '../lib/preview.ts';
import { formatDuration } from '../lib/format.ts';
import { navigate } from '../router.ts';
import { runProgram } from '../state/actions.ts';
import { config, programs, status, zoneLabel, zones } from '../state/store.ts';
import { zoneStateLabel } from './home.tsx';

export function programTotalSec(p: { durations_s?: number[]; seasonal?: boolean }): number {
  const pct = config.value?.schedule?.seasonal_percent ?? 100;
  return zones.value.reduce((sum, z, i) => sum + (z.enabled ? adjustedSeconds(p.durations_s?.[i] ?? 0, p.seasonal !== false, pct) : 0), 0);
}

export function RunScreen() {
  const [mode, setMode] = useState<'zone' | 'program'>('zone');
  const [zoneId, setZoneId] = useState<number | null>(null);
  const [progId, setProgId] = useState<number | null>(null);
  const [busy, setBusy] = useState(false);
  const enabledZones = zones.value.filter((z) => z.enabled);
  const zone = zones.value.find((z) => z.id === zoneId) ?? null;
  const prog = programs.value.find((p) => p.id === progId) ?? null;
  return (
    <div class="page">
      <PageTitle title={t('run_title')} />
      <Choice
        label={t('run_title')}
        hideLabel
        value={mode}
        onChange={setMode}
        options={[
          { value: 'zone', label: t('run_zone_tab') },
          { value: 'program', label: t('run_program_tab') },
        ]}
      />
      {mode === 'zone' ? (
        enabledZones.length === 0 ? (
          <Empty>{t('run_no_zones')}</Empty>
        ) : (
          <div class="stack">
            <fieldset class="pick-list">
              <legend>{t('run_choose_zone')}</legend>
              {enabledZones.map((z) => {
                const st = zoneStateLabel(status.value?.zones?.[z.id - 1], z);
                return (
                  <label key={z.id} class={zoneId === z.id ? 'pick is-checked' : 'pick'}>
                    <input type="radio" name="zone" checked={zoneId === z.id} onChange={() => setZoneId(z.id)} />
                    <span class="zone-num">{z.id}</span>
                    <span class="pick-name">{zoneLabel(z.id)}</span>
                    <span class={`pick-meta tone-${st.tone}`}>{st.text}</span>
                  </label>
                );
              })}
            </fieldset>
            {zone && (
              <div class="sticky-action">
                <ZoneRunControls zone={zone} onStarted={() => navigate('/')} />
              </div>
            )}
          </div>
        )
      ) : programs.value.length === 0 ? (
        <Empty icon="calendar">{t('run_no_programs')}</Empty>
      ) : (
        <div class="stack">
          <fieldset class="pick-list">
            <legend>{t('run_choose_program')}</legend>
            {programs.value.map((p) => (
              <label key={p.id} class={progId === p.id ? 'pick is-checked' : 'pick'}>
                <input type="radio" name="program" checked={progId === p.id} onChange={() => setProgId(p.id ?? null)} />
                <span class="pick-name">{p.name || t('program_name_default', { n: p.id ?? 0 })}</span>
                <span class="pick-meta">{formatDuration(programTotalSec(p))}</span>
              </label>
            ))}
          </fieldset>
          {prog && (
            <div class="sticky-action">
              <p class="muted">{t('run_program_total', { time: formatDuration(programTotalSec(prog)) })}</p>
              <Button
                variant="primary"
                size="lg"
                block
                icon="play"
                loading={busy}
                onClick={async () => {
                  setBusy(true);
                  const ok = await runProgram(prog.id ?? 0, prog.name ?? '');
                  setBusy(false);
                  if (ok) navigate('/');
                }}
              >
                {t('run_go_program', { name: prog.name || t('program_name_default', { n: prog.id ?? 0 }) })}
              </Button>
            </div>
          )}
        </div>
      )}
    </div>
  );
}

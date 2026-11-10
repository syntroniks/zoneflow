// Zone list and zone settings: name, enabled, maximum run time, current
// limits, safety state (resting, locked out), test run.
import { useEffect, useState } from 'preact/hooks';
import { ApiError } from '../api/client.ts';
import { Banner, Button, Card, KeyValue, PageTitle, Row, Stepper, Switch, TextField, confirmAction } from '../components/ui.tsx';
import { t, tk } from '../i18n/index.ts';
import { formatCountdown, formatDuration } from '../lib/format.ts';
import { setNavigationGuard } from '../router.ts';
import { clearFaults, ensureSignedIn, patchZone, runZone, stopZone } from '../state/actions.ts';
import { faultCheck, faultTitle } from '../state/alerts.ts';
import { errorText } from '../state/errors.ts';
import { config, elapsedSec, signedIn, status, toast, zoneLabel, zones } from '../state/store.ts';
import { zoneStateLabel } from './home.tsx';

export function ZonesScreen() {
  const count = zones.value.length;
  return (
    <div class="page">
      <PageTitle title={t('zones_title')} />
      <Card>
        <div class="rows">
          {zones.value.map((z) => {
            const st = zoneStateLabel(status.value?.zones?.[z.id - 1], z);
            return (
              <Row key={z.id} href={`/zones/${z.id}`} trailing={<span class={`badge tone-${st.tone}`}>{st.text}</span>}>
                <span class="zone-num">{z.id}</span>
                <span class="row-text">
                  <span class="row-title">{zoneLabel(z.id)}</span>
                  <span class="row-sub">
                    {t('zone_max_run')}: {formatDuration(z.maxRunSec)}
                  </span>
                </span>
              </Row>
            );
          })}
        </div>
      </Card>
      {count < 8 && <p class="muted small">{t('zones_beyond_count', { n: count })}</p>}
    </div>
  );
}

export function ZoneDetailScreen({ id }: { id: number }) {
  const info = zones.value.find((z) => z.id === id);
  const z = status.value?.zones?.[id - 1];
  const rest = (config.value?.driver as { min_rest_s?: number } | undefined)?.min_rest_s ?? 1800;
  const [form, setForm] = useState({ name: '', enabled: true, maxMin: 60, minMa: 80, maxMa: 700 });
  const [dirty, setDirty] = useState(false);
  const [errors, setErrors] = useState<Record<string, string>>({});
  const [busy, setBusy] = useState(false);

  useEffect(() => {
    if (info && !dirty)
      setForm({ name: info.name, enabled: info.enabled, maxMin: Math.round(info.maxRunSec / 60), minMa: info.currentMinMa, maxMa: info.currentMaxMa });
  }, [info?.name, info?.enabled, info?.maxRunSec, info?.currentMinMa, info?.currentMaxMa]);

  useEffect(() => {
    setNavigationGuard(dirty ? () => window.confirm(t('program_unsaved')) : null);
    return () => setNavigationGuard(null);
  }, [dirty]);

  if (!info) return <div class="page narrow"><PageTitle title={t('zone_n', { n: id })} back="/zones" /></div>;
  const set = (patch: Partial<typeof form>) => {
    setForm({ ...form, ...patch });
    setDirty(true);
  };
  const save = async (e: Event) => {
    e.preventDefault();
    if (!ensureSignedIn()) return;
    setBusy(true);
    setErrors({});
    try {
      await patchZone(id, { name: form.name.trim(), enabled: form.enabled, max_run_s: form.maxMin * 60, current_min_ma: form.minMa, current_max_ma: form.maxMa });
      setDirty(false);
      toast(t('saved'), 'success');
    } catch (x) {
      if (x instanceof ApiError && x.path) setErrors({ [x.path]: x.message });
      toast(errorText(x), 'error');
    } finally {
      setBusy(false);
    }
  };
  const st = zoneStateLabel(z, info);
  const running = z?.state === 'open' || z?.state === 'waiting';
  return (
    <div class="page narrow">
      <PageTitle title={zoneLabel(id)} back="/zones">
        <span class={`badge tone-${st.tone}`}>{st.text}</span>
      </PageTitle>
      {z?.locked_out && (
        <Banner
          severity="error"
          title={`${t('zone_locked', { zone: zoneLabel(id) })}: ${faultTitle(z.fault ?? '')}`}
          action={
            <Button
              size="sm"
              onClick={async () => {
                if (await confirmAction({ title: t('clear_faults'), body: t('clear_faults_confirm'), confirmLabel: t('clear_faults') })) void clearFaults();
              }}
            >
              {t('clear_faults')}
            </Button>
          }
        >
          {faultCheck(z.fault ?? '')}
        </Banner>
      )}
      {z?.resting && (
        <Banner severity="warning" title={t('zone_resting', { zone: zoneLabel(id) })}>
          {t('zone_resting_detail', { time: formatDuration(z.rest_remaining_s ?? 0) })}
        </Banner>
      )}
      <Card title={t('zone_test')}>
        <p class="muted">{t('zone_test_help')}</p>
        {running ? (
          <div class="row-actions">
            <span class="tabular big">{formatCountdown(Math.max(0, (z?.remaining_s ?? 0) - elapsedSec()))}</span>
            <Button variant="danger" icon="stop" onClick={() => void stopZone(id)}>
              {t('stop')}
            </Button>
          </div>
        ) : (
          <div class="row-actions wrap">
            {[1, 2, 5].map((m) => (
              <Button key={m} icon="play" disabled={!info.enabled || z?.locked_out || z?.resting} onClick={() => void runZone(id, m * 60)}>
                {t('zone_test_go', { time: formatDuration(m * 60) })}
              </Button>
            ))}
          </div>
        )}
      </Card>
      <form onSubmit={save} class="card stack" aria-label={t('edit')}>
        <TextField label={t('zone_name')} help={t('zone_name_help')} value={form.name} maxLength={23} onInput={(v) => set({ name: v })} error={errors['name']} placeholder={t('zone_n', { n: id })} disabled={!signedIn.value} />
        <Switch label={t('zone_enabled')} help={t('zone_enabled_help')} checked={form.enabled} onChange={(v) => set({ enabled: v })} disabled={!signedIn.value} />
        <Stepper label={t('zone_max_run')} help={t('zone_max_run_help', { rest: formatDuration(rest) })} value={form.maxMin} min={1} max={240} step={5} unit={t('minutes_short')} onChange={(v) => set({ maxMin: v })} error={errors['max_run_s']} />
        <fieldset class="group">
          <legend>{t('zone_current_window')}</legend>
          <p class="field-help">{t('zone_current_help')}</p>
          <div class="two-col">
            <Stepper label={t('zone_current_min')} value={form.minMa} min={0} max={3000} step={10} onChange={(v) => set({ minMa: v })} error={errors['current_min_ma']} />
            <Stepper label={t('zone_current_max')} value={form.maxMa} min={0} max={3000} step={10} onChange={(v) => set({ maxMa: v })} error={errors['current_max_ma']} />
          </div>
          <p class="field-help">{t('zone_learned_absent')}</p>
        </fieldset>
        <KeyValue items={[[t('zone_last_stop'), tk(`last_stop_${z?.last_stop ?? 'none'}`)]]} />
        <div class="form-actions">
          <Button variant="primary" type="submit" loading={busy} disabled={!dirty}>
            {t('save')}
          </Button>
        </div>
      </form>
    </div>
  );
}

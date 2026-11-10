// Dashboard: what is watering (time left, stop always at hand), what needs
// attention in plain language, the queue, the next scheduled start, zones
// and sensors.
import { useState } from 'preact/hooks';
import type { Status } from '../api/client.ts';
import { Icon } from '../components/icons.tsx';
import { Banner, Button, Card, LinkButton, Progress, Sheet, confirmAction } from '../components/ui.tsx';
import { ZoneRunControls } from '../components/zone-run.tsx';
import { t } from '../i18n/index.ts';
import { formatCountdown, formatDateTime, formatDuration, formatNumber, formatWhen } from '../lib/format.ts';
import { onLinkClick } from '../router.ts';
import { clearFaults, pauseQueue, resumeQueue, skipQueue, stopAll } from '../state/actions.ts';
import { deriveAlerts } from '../state/alerts.ts';
import { nextPlanned } from '../state/schedule.ts';
import { config, conn, deviceNow, elapsedSec, pending, programs, status, timeZone, zoneLabel, zones, type ZoneInfo } from '../state/store.ts';

type ZoneStatus = NonNullable<Status['zones']>[number];

export function zoneStateLabel(z: ZoneStatus | undefined, info: ZoneInfo): { text: string; tone: string } {
  if (!info.enabled) return { text: t('zone_state_disabled'), tone: 'muted' };
  if (z?.locked_out) return { text: t('zone_state_locked'), tone: 'error' };
  if (z?.state === 'open') return { text: t('zone_state_open'), tone: 'water' };
  if (z?.state === 'waiting') return { text: t('zone_state_waiting'), tone: 'water' };
  if (z?.resting) return { text: t('zone_state_resting'), tone: 'warning' };
  return { text: t('zone_state_off'), tone: 'muted' };
}

function NowCard() {
  const s = status.value;
  const q = s?.queue;
  const tz = timeZone.value;
  const live = conn.value === 'live';
  const el = elapsedSec();
  const open = s?.zones?.find((z) => z.state === 'open') ?? s?.zones?.find((z) => z.state === 'waiting');
  const head = q?.items?.[0];
  const state = q?.state ?? 'idle';

  if (!s || state === 'idle') {
    const next = nextPlanned();
    return (
      <section class="card now now-idle" id="now-card" aria-labelledby="now-h">
        <h2 id="now-h" class="now-title">
          {t('now_idle')}
        </h2>
        <p class="now-next">
          <Icon name="clock" size={18} />
          {s?.time_status === 'unset'
            ? t('next_run_no_time')
            : next
              ? t('next_run', { name: next.name || t('program_name_default', { n: next.programId }), when: formatWhen(next.at, tz, deviceNow()) })
              : programs.value.length
                ? t('next_run_none')
                : t('program_none')}
        </p>
        <LinkButton href="/run" variant="primary" icon="play" block>
          {t('run_something')}
        </LinkButton>
      </section>
    );
  }

  const zoneId = open?.id ?? head?.zone ?? null;
  const total = head?.duration_s ?? open?.remaining_s ?? 0;
  const left = Math.max(0, (open?.remaining_s ?? q?.current_remaining_s ?? 0) - el);
  const prog = head?.program_id ? programs.value.find((p) => p.id === head.program_id) : undefined;
  const source = head?.source === 'manual_zone' ? t('now_manual') : t('now_from_program', { name: prog?.name ?? `#${head?.program_id ?? ''}` });
  const rest = (q?.items ?? []).slice(1);
  const more = Math.max(0, (q?.length ?? 0) - 1 - rest.length);
  const paused = state === 'paused';
  return (
    <section class={`card now ${paused ? 'now-paused' : 'now-running'} ${live ? '' : 'is-stale'}`} id="now-card" aria-labelledby="now-h">
      <p class="now-kicker">
        <span class={paused ? 'pulse paused' : 'pulse'} aria-hidden="true" />
        <span id="now-h">{paused ? t('now_paused') : open?.state === 'waiting' ? t('now_waiting') : t('now_running')}</span>
        <span class="now-source">{source}</span>
      </p>
      <p class="now-zone">
        <span class="zone-num" aria-hidden="true">
          {zoneId}
        </span>
        <span class="now-zone-name">{zoneLabel(zoneId)}</span>
      </p>
      {paused ? (
        <p class="muted">{t('now_paused_detail', { time: formatDuration((q?.pause_timeout_min ?? 120) * 60) })}</p>
      ) : (
        <>
          <p class="now-clock tabular" aria-live="off">
            {formatCountdown(left)}
            <span class="now-clock-of">{t('clock_left', { time: '' }).trim()}</span>
          </p>
          <Progress value={total - left} max={total} label={t('clock_left', { time: formatCountdown(left) })} />
        </>
      )}
      <div class="now-actions">
        {paused ? (
          <Button icon="play" onClick={() => void resumeQueue()}>
            {t('resume')}
          </Button>
        ) : (
          <Button icon="pause" onClick={() => void pauseQueue()}>
            {t('pause')}
          </Button>
        )}
        <Button icon="skip" onClick={() => void skipQueue()} disabled={(q?.length ?? 0) < 1}>
          {t('skip_zone')}
        </Button>
        <Button variant="danger" icon="stop" class="now-stop" onClick={() => void stopAll()}>
          {t('stop_all')}
        </Button>
      </div>
      {rest.length > 0 && (
        <div class="queue">
          <h3>{t('queue_title')}</h3>
          <ol>
            {rest.map((it) => (
              <li key={it.id}>
                <span class="zone-num sm" aria-hidden="true">
                  {it.zone}
                </span>
                <span class="queue-name">{zoneLabel(it.zone)}</span>
                <span class="tabular muted">{formatDuration(it.duration_s ?? 0)}</span>
              </li>
            ))}
          </ol>
          {more > 0 && <p class="muted small">{t('queue_more', { n: more })}</p>}
        </div>
      )}
    </section>
  );
}

function AlertsCard() {
  const s = status.value;
  if (!s) return null;
  const tz = timeZone.value;
  const alerts = deriveAlerts(s, (id) => zoneLabel(id), (u) => formatDateTime(u, tz));
  if (!alerts.length) return null;
  const clearable = alerts.some((a) => a.clearable);
  return (
    <section class="alerts" aria-label={t('alerts_title')}>
      {alerts.map((a) => (
        <Banner
          key={a.id}
          severity={a.severity}
          title={a.title}
          action={
            a.href ? (
              <a href={a.href} onClick={onLinkClick} class="link">
                {a.id === 'rain' ? t('nav_rain_delay') : t('nav_settings')}
              </a>
            ) : undefined
          }
        >
          {a.detail}
        </Banner>
      ))}
      {clearable && (
        <Button
          variant="tonal"
          icon="check"
          onClick={async () => {
            if (await confirmAction({ title: t('clear_faults'), body: t('clear_faults_confirm'), confirmLabel: t('clear_faults') })) void clearFaults();
          }}
        >
          {t('clear_faults')}
        </Button>
      )}
    </section>
  );
}

function ZoneTile({ info, onPick }: { info: ZoneInfo; onPick: (z: ZoneInfo) => void }) {
  const z = status.value?.zones?.[info.id - 1];
  const st = zoneStateLabel(z, info);
  const isPending = `zone:${info.id}` in pending.value;
  const running = z?.state === 'open';
  const left = running ? Math.max(0, (z?.remaining_s ?? 0) - elapsedSec()) : 0;
  return (
    <li>
      <button type="button" class={`tile tone-${st.tone} ${isPending ? 'is-pending' : ''}`} onClick={() => onPick(info)} disabled={!info.enabled}>
        <span class="tile-top">
          <span class="zone-num">{info.id}</span>
          <span class={`dot tone-${st.tone}`} aria-hidden="true" />
        </span>
        <span class="tile-name">{zoneLabel(info.id)}</span>
        <span class="tile-state">
          {isPending ? t('now_waiting') : st.text}
          {running && <span class="tabular"> · {formatCountdown(left)}</span>}
          {z?.resting && !running && <span class="tabular"> · {formatDuration(z.rest_remaining_s ?? 0)}</span>}
        </span>
      </button>
    </li>
  );
}

export function QuickRunSheet({ zone, onClose }: { zone: ZoneInfo | null; onClose: () => void }) {
  const z = zone ? status.value?.zones?.[zone.id - 1] : undefined;
  return (
    <Sheet open={!!zone} onClose={onClose} title={zone ? zoneLabel(zone.id) : ''}>
      {zone && (
        <>
          {z?.state === 'open' && <Banner severity="info" title={t('zone_state_open')} />}
          {z?.resting && <Banner severity="warning" title={t('zone_resting', { zone: zoneLabel(zone.id) })}>{t('zone_resting_detail', { time: formatDuration(z.rest_remaining_s ?? 0) })}</Banner>}
          {z?.locked_out && <Banner severity="error" title={t('zone_locked', { zone: zoneLabel(zone.id) })} />}
          <ZoneRunControls zone={zone} onStarted={onClose} />
        </>
      )}
    </Sheet>
  );
}

function ZonesCard() {
  const [pick, setPick] = useState<ZoneInfo | null>(null);
  return (
    <Card title={t('zones_title')} action={<a href="/zones" onClick={onLinkClick} class="link">{t('edit')}</a>}>
      <ul class="tiles">
        {zones.value.map((z) => (
          <ZoneTile key={z.id} info={z} onPick={setPick} />
        ))}
      </ul>
      <QuickRunSheet zone={pick} onClose={() => setPick(null)} />
    </Card>
  );
}

function SensorsCard() {
  const s = status.value?.sensors;
  const cfgSensors = (config.value?.sensors ?? {}) as Record<string, { enabled?: boolean } | undefined>;
  if (!s) return null;
  const master = status.value?.master;
  const rows: { icon: Parameters<typeof Icon>[0]['name']; label: string; value: string; tone?: string }[] = [
    {
      icon: 'rain',
      label: t('rain_sensor'),
      value: s.rain?.enabled ?? cfgSensors['rain']?.enabled ? (s.rain?.active ? t('rain_wet') : t('rain_dry')) : t('not_fitted'),
      tone: s.rain?.active ? 'water' : undefined,
    },
    {
      icon: 'flow',
      label: t('flow'),
      value: s.flow?.enabled
        ? `${t('flow_rate', { rate: formatNumber(s.flow.rate_lpm ?? 0, 1) })} · ${t('flow_total', { total: formatNumber(s.flow.total_l ?? 0, 0) })}`
        : t('not_fitted'),
    },
    {
      icon: 'bolt',
      label: t('valve_current'),
      value: s.current_sense_available === false ? t('unknown') : t('current_ma', { ma: s.valve_current_ma ?? 0 }),
      tone: s.current_sense_available === false ? 'warning' : undefined,
    },
    { icon: 'power', label: t('ac_power'), value: s.ac_present ? t('ac_present') : t('ac_missing'), tone: s.ac_present ? undefined : 'error' },
  ];
  if (master?.enabled) rows.push({ icon: 'drop', label: t('master_valve'), value: master.on ? t('master_on') : t('master_off') });
  if (s.spare?.enabled) rows.push({ icon: 'info', label: t('spare_input'), value: s.spare.active ? t('input_active') : t('input_idle') });
  return (
    <Card title={t('sensors_title')}>
      <ul class="sensor-list">
        {rows.map((r) => (
          <li key={r.label} class={r.tone ? `tone-${r.tone}` : undefined}>
            <Icon name={r.icon} size={20} />
            <span class="sensor-label">{r.label}</span>
            <span class="sensor-value">{r.value}</span>
          </li>
        ))}
      </ul>
    </Card>
  );
}

export function HomeScreen() {
  return (
    <div class="page home">
      <h1 class="sr-only" tabIndex={-1}>
        {t('home_title')}
      </h1>
      <div class="home-grid">
        <div class="home-col">
          <NowCard />
          <AlertsCard />
        </div>
        <div class="home-col">
          <ZonesCard />
          <SensorsCard />
        </div>
      </div>
    </div>
  );
}

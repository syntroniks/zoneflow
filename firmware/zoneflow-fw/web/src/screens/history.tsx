// Events received since the page was opened. The API has no history route
// yet (requested from the API owner), so this says plainly what it covers.
import { Card, Empty, PageTitle } from '../components/ui.tsx';
import { t } from '../i18n/index.ts';
import { formatDateTime, formatTime } from '../lib/format.ts';
import { faultTitle } from '../state/alerts.ts';
import { reasonText } from '../state/errors.ts';
import { dropReasonText } from '../state/events.ts';
import { eventLog, programs, timeZone, zoneLabel, type LogEntry } from '../state/store.ts';
import { tk } from '../i18n/index.ts';

function programName(id: unknown): string {
  const p = programs.value.find((x) => x.id === id);
  return p?.name || `#${String(id)}`;
}

export function describeEvent(e: LogEntry): string | null {
  const d = e.data;
  const zone = zoneLabel(d['zone'] as number | null);
  const tz = timeZone.value;
  switch (e.type) {
    case 'zone_changed':
      return t(d['state'] === 'open' ? 'ev_zone_open' : d['state'] === 'waiting' ? 'ev_zone_waiting' : 'ev_zone_off', { zone });
    case 'master_changed':
      return t(d['on'] ? 'ev_master_on' : 'ev_master_off');
    case 'program_started':
      return t(d['late'] ? 'ev_program_started_late' : 'ev_program_started', { name: programName(d['program_id']) });
    case 'program_skipped':
      return t('ev_program_skipped', { name: programName(d['program_id']), reason: tk(`skip_${String(d['reason'])}`, undefined, String(d['reason'])) });
    case 'program_missed':
      return t('ev_program_missed', { name: programName(d['program_id']), when: formatDateTime(Number(d['scheduled_utc']), tz) });
    case 'sensor_changed':
      return t(d['active'] ? 'ev_sensor_on' : 'ev_sensor_off', { sensor: tk(`sensor_${String(d['sensor'])}`) });
    case 'fault_raised':
    case 'fault_cleared': {
      const f = faultTitle(String(d['code'])) + (d['zone'] ? ` (${zone})` : '');
      return t(e.type === 'fault_raised' ? 'ev_fault_raised' : 'ev_fault_cleared', { fault: f });
    }
    case 'rain_delay_changed':
      return d['until_utc'] ? t('ev_rain_delay_set', { when: formatDateTime(Number(d['until_utc']), tz) }) : t('ev_rain_delay_cleared');
    case 'config_applied':
      return t('ev_config_applied');
    case 'time_status_changed':
      return t('ev_time_status', { status: tk(`time_status_${String(d['status'])}`) });
    case 'maintenance_changed':
      return t(d['on'] ? 'ev_maintenance_on' : 'ev_maintenance_off');
    case 'command_rejected':
      return t('ev_command_rejected', { reason: reasonText(String(d['error']), { zone }) });
    case 'run_dropped':
      return t('ev_run_dropped', { zone, reason: dropReasonText(String(d['reason'])) });
    case 'pause_expired':
      return t('ev_pause_expired');
    default:
      return null; // queue_changed and unknown types are noise here
  }
}

const TONE: Record<string, string> = {
  fault_raised: 'error',
  command_rejected: 'error',
  run_dropped: 'warning',
  program_missed: 'warning',
  program_skipped: 'warning',
  zone_changed: 'water',
  program_started: 'water',
};

export function ActivityScreen() {
  const tz = timeZone.value;
  const items = eventLog.value.map((e) => ({ e, text: describeEvent(e) })).filter((x) => x.text);
  return (
    <div class="page narrow">
      <PageTitle title={t('activity_title')} back="/more" />
      <p class="muted">{t('activity_intro')}</p>
      <Card>
        {items.length === 0 ? (
          <Empty icon="activity">{t('activity_empty')}</Empty>
        ) : (
          <ol class="log">
            {items.map(({ e, text }) => (
              <li key={e.seq} class={`tone-${TONE[e.type] ?? 'muted'}`}>
                <time class="tabular">{formatTime(e.utc, tz)}</time>
                <span>{text}</span>
              </li>
            ))}
          </ol>
        )}
      </Card>
    </div>
  );
}

// Controller events from the stream: notifications, the session log, and
// verdicts on run commands sent without one.
import { t } from '../i18n/index.ts';
import { loadConfig } from './actions.ts';
import { reasonText } from './errors.ts';
import { clearPending, logEvent, pending, toast, zoneLabel } from './store.ts';

const DROP_KEYS: Record<string, Parameters<typeof t>[0]> = {
  resting: 'drop_resting',
  locked_out: 'drop_locked_out',
  disabled: 'drop_disabled',
  inhibited: 'drop_inhibited',
  master_locked_out: 'drop_master_locked_out',
};

export function dropReasonText(reason: string): string {
  const k = DROP_KEYS[reason];
  return k ? t(k) : reason.replace(/_/g, ' ');
}

export function handleEvent(e: Record<string, unknown>): void {
  logEvent(e);
  const zone = typeof e['zone'] === 'number' ? (e['zone'] as number) : null;
  switch (e['type']) {
    case 'command_rejected': {
      const key = zone != null ? `zone:${zone}` : `program:${String(e['program_id'])}`;
      // Only report refusals of commands this page sent and is waiting for;
      // other clients' refusals are in the activity log.
      if (key in pending.value) {
        clearPending(key);
        toast(t('run_rejected', { reason: reasonText(String(e['error']), { zone: zoneLabel(zone) }) }), 'error');
      }
      break;
    }
    case 'run_dropped':
      toast(t('run_dropped', { zone: zoneLabel(zone), reason: dropReasonText(String(e['reason'])) }), 'error');
      break;
    case 'pause_expired':
      toast(t('pause_expired'));
      break;
    case 'config_applied':
      void loadConfig();
      break;
    case 'zone_changed':
      if (zone != null && e['state'] !== 'off') clearPending(`zone:${zone}`);
      break;
    case 'program_started':
      clearPending(`program:${String(e['program_id'])}`);
      break;
  }
}

// Derives the "needs attention" list from a status snapshot. Pure, tested in
// tests/unit/alerts.test.ts.
import type { Status } from '../api/client.ts';
import { t, tk } from '../i18n/index.ts';
import { formatDuration } from '../lib/format.ts';

export type Severity = 'error' | 'warning' | 'info';

export interface Alert {
  id: string;
  severity: Severity;
  title: string;
  detail: string;
  /** Offer "clear faults" (latched faults and lockouts). */
  clearable?: boolean;
  /** Link to a screen that fixes it. */
  href?: string;
}

/** Faults that clear themselves when the cause goes away. */
const CONDITIONS = new Set(['ac_lost', 'time_invalid', 'current_sense_unavailable', 'sensor_supply', 'mains_signal', 'not_commissioned']);
/** Faults that are warnings, not errors: watering carries on. */
const WARNINGS = new Set(['current_sense_unavailable', 'flow_without_zones', 'sensor_supply', 'mains_signal', 'not_commissioned']);
/** Output-stage faults that may need a confirmed repair (hardware design 19.7:
 * steps 1, 2, 2b, 5b and 9); faults.hardware.repair_needed says whether this one does. */
const REPAIR = new Set(['keepalive_detector', 'keepalive_bridged', 'relay_gate_bleed']);

export function faultTitle(code: string): string {
  return tk(`fault_${code}`, undefined, t('fault_unknown', { code }));
}

/** "Zone 3: no current" when the fault belongs to zones (or the master
 * valve), the plain title otherwise. Same words as the display and ESPHome:
 * zf/fault_text.hpp, checked by tests/unit/fault-text.test.ts. */
export function faultTitleFor(code: string, where: string[]): string {
  if (!where.length) return faultTitle(code);
  const zone = where.join(', ');
  return tk(`fault_${code}_zone`, { zone }, `${faultTitle(code)} ${t('fault_on_zone', { zone })}`);
}

export function faultCheck(code: string): string {
  return tk(`fault_${code}_check`, undefined, t('fault_unknown_check'));
}

export function deriveAlerts(s: Status, zoneName: (id: number) => string, untilText: (utc: number) => string): Alert[] {
  const out: Alert[] = [];
  const zones = s.zones ?? [];
  const zoneFaults = s.faults?.zones ?? [];
  const seen = new Set<string>();

  for (const code of s.faults?.active ?? []) {
    if (code === 'time_invalid') continue; // shown below with its own wording
    const affected = zoneFaults.filter((z) => z.fault === code).map((z) => zoneName(z.zone ?? 0));
    out.push({
      id: `fault:${code}`,
      severity: WARNINGS.has(code) ? 'warning' : 'error',
      title: faultTitleFor(code, affected),
      detail: faultCheck(code),
      // A fault of a safety layer needs the repair confirmed; clearing faults does not clear it.
      clearable: !CONDITIONS.has(code) && !(REPAIR.has(code) && s.faults?.hardware?.repair_needed),
    });
    seen.add(code);
  }
  // Zone faults whose code is not in the active list (latched lockouts).
  for (const z of zoneFaults) {
    if (!z.locked_out || seen.has(z.fault ?? '')) continue;
    out.push({
      id: `locked:${z.zone}`,
      severity: 'error',
      title: t('zone_locked', { zone: zoneName(z.zone ?? 0) }),
      detail: z.fault && z.fault !== 'none' ? faultCheck(z.fault) : t('clear_faults_confirm'),
      clearable: true,
    });
  }
  if (s.master?.locked_out && !seen.has('master_locked_out')) {
    out.push({
      id: 'master',
      severity: 'error',
      title: faultTitle('master_locked_out'),
      detail: faultCheck('master_locked_out'),
      clearable: true,
    });
  }
  if (s.sensors?.current_sense_available === false && !seen.has('current_sense_unavailable')) {
    out.push({ id: 'blind', severity: 'warning', title: t('current_blind'), detail: t('current_blind_detail') });
  }
  if (s.time_status === 'unset') {
    out.push({ id: 'time', severity: 'warning', title: t('time_unset'), detail: t('time_unset_detail'), href: '/settings/time' });
  }
  if (s.maintenance) {
    out.push({ id: 'maint', severity: 'info', title: t('maintenance_on'), detail: t('maintenance_detail') });
  }
  if (s.rain_delay?.active && s.rain_delay.until_utc) {
    out.push({
      id: 'rain',
      severity: 'info',
      title: t('rain_delay_active', { when: untilText(s.rain_delay.until_utc) }),
      detail: t('rain_delay_active_detail'),
      href: '/rain-delay',
    });
  }
  if (s.inhibit?.sensor) {
    // The rain sensor, or the spare input set to hold programs like it.
    const spare = !s.sensors?.rain?.active && !!s.sensors?.spare?.active;
    out.push({ id: 'sensor', severity: 'info', title: t(spare ? 'spare_inhibit' : 'sensor_inhibit'), detail: t('sensor_inhibit_detail') });
  }
  for (const z of zones) {
    if (z.resting && (z.rest_remaining_s ?? 0) > 0) {
      out.push({
        id: `rest:${z.id}`,
        severity: 'info',
        title: t('zone_resting', { zone: zoneName(z.id ?? 0) }),
        detail: t('zone_resting_detail', { time: formatDuration(z.rest_remaining_s ?? 0) }),
      });
    }
  }
  const rank: Record<Severity, number> = { error: 0, warning: 1, info: 2 };
  return out.sort((a, b) => rank[a.severity] - rank[b.severity]);
}

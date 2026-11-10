// History: what the controller recorded (GET /api/v1/history), grouped by
// day in the controller's time zone, with a seven-day runtime and water
// summary per zone. New records arrive while the page is open: every event
// on the stream triggers a fetch of records newer than the newest shown.
import { useEffect, useMemo, useRef, useState } from 'preact/hooks';
import { call, type HistoryPage, type HistoryRecord } from '../api/client.ts';
import { Banner, Button, Card, Empty, Field, PageTitle, Spinner } from '../components/ui.tsx';
import { t, tk } from '../i18n/index.ts';
import { formatDayLabel, formatDuration, formatNumber, formatTime } from '../lib/format.ts';
import { faultTitle } from '../state/alerts.ts';
import { errorText } from '../state/errors.ts';
import { dropReasonText } from '../state/events.ts';
import { deviceNow, eventLog, programs, timeZone, zoneLabel, zones } from '../state/store.ts';

const PAGE = 100;
const SUMMARY_DAYS = 7;

function programName(id: number | null): string {
  if (id == null) return t('now_manual');
  const p = programs.value.find((x) => x.id === id);
  return p?.name || `#${id}`;
}

export function describeRecord(r: HistoryRecord): { text: string; detail?: string; tone: string } {
  const tz = timeZone.value;
  switch (r.type) {
    case 'run': {
      const short = r.actual_s + 5 < r.planned_s;
      const parts = [
        short ? t('hist_run_of', { actual: formatDuration(r.actual_s), planned: formatDuration(r.planned_s) }) : formatDuration(r.actual_s),
        r.source === 'manual_zone' ? t('now_manual') : programName(r.program_id),
      ];
      if (r.current_ma != null) parts.push(t('current_ma', { ma: r.current_ma }));
      if (r.litres != null) parts.push(t('hist_litres', { l: formatNumber(r.litres, r.litres < 10 ? 1 : 0) }));
      const endKey = r.end === 'completed' ? null : `hist_end_${r.end}`;
      return {
        text: t('hist_run', { zone: zoneLabel(r.zone) }) + (endKey ? ` (${tk(endKey, undefined, r.end)})` : ''),
        detail: parts.join(' · '),
        tone: r.end === 'fault' || r.end === 'ac_lost' ? 'error' : r.end === 'max_run_time' ? 'warning' : 'water',
      };
    }
    case 'program_started':
      return { text: t(r.late ? 'ev_program_started_late' : 'ev_program_started', { name: programName(r.program_id) }), tone: 'water' };
    case 'program_skipped':
      return { text: t('ev_program_skipped', { name: programName(r.program_id), reason: tk(`skip_${r.reason}`, undefined, r.reason) }), tone: 'warning' };
    case 'program_missed':
      return { text: t('ev_program_missed', { name: programName(r.program_id), when: formatTime(r.scheduled_utc, tz) }), tone: 'warning' };
    case 'run_dropped':
      return { text: t('ev_run_dropped', { zone: zoneLabel(r.zone), reason: dropReasonText(r.reason) }), tone: 'warning' };
    case 'fault_raised':
    case 'fault_cleared': {
      const f = faultTitle(r.code) + (r.zone != null ? ` (${zoneLabel(r.zone)})` : '');
      return { text: t(r.type === 'fault_raised' ? 'ev_fault_raised' : 'ev_fault_cleared', { fault: f }), tone: r.type === 'fault_raised' ? 'error' : 'muted' };
    }
    case 'boot':
      return { text: t('hist_boot'), detail: tk(`hist_reset_${r.reset_reason}`, undefined, r.reset_reason), tone: r.reset_reason === 'panic' || r.reset_reason === 'watchdog' || r.reset_reason === 'brownout' ? 'warning' : 'muted' };
    case 'time_set':
      return { text: t('ev_time_status', { status: tk(`time_status_${r.status}`) }), tone: r.status === 'unset' ? 'warning' : 'muted' };
    case 'config_changed':
      return { text: t('ev_config_applied'), tone: 'muted' };
    case 'rain_delay':
      return { text: r.until_utc != null ? t('ev_rain_delay_set', { when: `${formatDayLabel(r.until_utc, tz, deviceNow())} ${formatTime(r.until_utc, tz)}` }) : t('ev_rain_delay_cleared'), tone: 'muted' };
    case 'sensor':
      return { text: t(r.active ? 'ev_sensor_on' : 'ev_sensor_off', { sensor: tk(`sensor_${r.sensor}`) }), tone: r.sensor === 'ac' && !r.active ? 'error' : 'muted' };
    case 'maintenance':
      return { text: t(r.on ? 'ev_maintenance_on' : 'ev_maintenance_off'), tone: 'muted' };
    case 'pause_expired':
      return { text: t('ev_pause_expired'), tone: 'warning' };
    case 'ota':
      return {
        text: tk(`hist_ota_${r.stage}`),
        detail: r.error ? tk(`ota_err_${r.error}`, undefined, r.error) : undefined,
        tone: r.stage === 'failed' ? 'error' : 'muted',
      };
  }
}

interface ZoneUse {
  zone: number;
  seconds: number;
  litres: number | null;
  runs: number;
}

/** Runtime and volume per zone over the last `days` days (runs with a valid time). */
export function summarise(records: HistoryRecord[], since: number): ZoneUse[] {
  const by = new Map<number, ZoneUse>();
  for (const r of records) {
    if (r.type !== 'run' || !r.time_valid || r.utc < since || r.zone == null) continue;
    const u = by.get(r.zone) ?? { zone: r.zone, seconds: 0, litres: null, runs: 0 };
    u.seconds += r.actual_s;
    u.runs += 1;
    if (r.litres != null) u.litres = (u.litres ?? 0) + r.litres;
    by.set(r.zone, u);
  }
  return [...by.values()].sort((a, b) => a.zone - b.zone);
}

function Summary({ records, complete }: { records: HistoryRecord[]; complete: boolean }) {
  const since = deviceNow() - SUMMARY_DAYS * 86400;
  const use = summarise(records, since);
  const max = Math.max(1, ...use.map((u) => u.seconds));
  return (
    <Card title={t('hist_summary', { days: SUMMARY_DAYS })}>
      {use.length === 0 ? (
        <p class="muted">{t('hist_summary_none')}</p>
      ) : (
        <ul class="usage">
          {use.map((u) => (
            <li key={u.zone}>
              <span class="usage-name">
                <span class="zone-num sm" aria-hidden="true">
                  {u.zone}
                </span>
                {zoneLabel(u.zone)}
              </span>
              <span class="usage-bar" aria-hidden="true">
                <span style={{ width: `${(u.seconds / max) * 100}%` }} />
              </span>
              <span class="usage-value tabular">
                {formatDuration(u.seconds)}
                {u.litres != null && ` · ${t('hist_litres', { l: formatNumber(u.litres, 0) })}`}
              </span>
            </li>
          ))}
        </ul>
      )}
      {!complete && <p class="field-help">{t('hist_summary_partial')}</p>}
    </Card>
  );
}

export function ActivityScreen() {
  const [records, setRecords] = useState<HistoryRecord[]>([]);
  const [page, setPage] = useState<HistoryPage | null>(null);
  const [zone, setZone] = useState(0);
  const [error, setError] = useState<string | null>(null);
  const [loading, setLoading] = useState(true);
  const [olderBusy, setOlderBusy] = useState(false);
  const newest = useRef(0);

  const zoneQuery = zone ? { zone } : {};
  const loadFirst = async () => {
    setLoading(true);
    try {
      // Enough for the summary: page back until a record is older than its window.
      const since = deviceNow() - SUMMARY_DAYS * 86400;
      let p = await call('get', '/history', { query: { limit: PAGE, ...zoneQuery } });
      let all = p.records;
      for (let i = 0; i < 5 && p.next_before != null && (all[all.length - 1]?.utc ?? 0) >= since; i++) {
        p = await call('get', '/history', { query: { limit: PAGE, before: p.next_before, ...zoneQuery } });
        all = [...all, ...p.records];
      }
      newest.current = all[0]?.id ?? 0;
      setRecords(all);
      setPage(p);
      setError(null);
    } catch (e) {
      setError(errorText(e));
    } finally {
      setLoading(false);
    }
  };

  useEffect(() => {
    void loadFirst();
  }, [zone]);

  // Something happened (an event on the stream): fetch what is new.
  const seq = eventLog.value[0]?.seq ?? 0;
  useEffect(() => {
    if (!seq || loading) return;
    const timer = setTimeout(async () => {
      try {
        const p = await call('get', '/history', { query: { limit: PAGE, after: newest.current, ...zoneQuery } });
        if (p.records.length) {
          newest.current = p.records[0]?.id ?? newest.current;
          setRecords((r) => [...p.records, ...r]);
        }
      } catch {
        // the next event retries
      }
    }, 1500);
    return () => clearTimeout(timer);
  }, [seq]);

  const older = async () => {
    if (page?.next_before == null) return;
    setOlderBusy(true);
    try {
      const p = await call('get', '/history', { query: { limit: PAGE, before: page.next_before, ...zoneQuery } });
      setRecords((r) => [...r, ...p.records]);
      setPage(p);
    } catch (e) {
      setError(errorText(e));
    }
    setOlderBusy(false);
  };

  const tz = timeZone.value;
  const now = deviceNow();
  const groups = useMemo(() => {
    const out: { key: string; label: string; items: HistoryRecord[] }[] = [];
    for (const r of records) {
      const key = r.time_valid ? String(tz.localDay(r.utc)) : 'unknown';
      let g = out[out.length - 1];
      if (!g || g.key !== key) {
        g = { key, label: r.time_valid ? formatDayLabel(r.utc, tz, now) : t('hist_time_unknown'), items: [] };
        out.push(g);
      }
      g.items.push(r);
    }
    return out;
  }, [records, tz, Math.floor(now / 3600)]);

  return (
    <div class="page narrow">
      <PageTitle title={t('activity_title')} back="/more" />
      <p class="muted">{t('hist_intro')}</p>
      <Field label={t('hist_filter')}>
        {({ id }) => (
          <select id={id} class="input" value={zone} onChange={(e) => setZone(Number((e.currentTarget as HTMLSelectElement).value))}>
            <option value={0}>{t('hist_all_zones')}</option>
            {zones.value.map((z) => (
              <option key={z.id} value={z.id}>
                {zoneLabel(z.id)}
              </option>
            ))}
          </select>
        )}
      </Field>
      {error && <Banner severity="error" title={error} />}
      {loading ? (
        <Spinner label={t('loading')} />
      ) : (
        <>
          <Summary records={records} complete={page?.next_before == null || (records[records.length - 1]?.utc ?? 0) < now - SUMMARY_DAYS * 86400} />
          {records.length === 0 ? (
            <Card>
              <Empty icon="activity">{t('hist_empty')}</Empty>
            </Card>
          ) : (
            groups.map((g) => (
              <section key={g.key} class="card hist-day" aria-label={g.label}>
                <h2>{g.label}</h2>
                <ol class="log">
                  {g.items.map((r) => {
                    const d = describeRecord(r);
                    return (
                      <li key={r.id} class={`tone-${d.tone}`}>
                        <time class="tabular">{r.time_valid ? formatTime(r.utc, tz) : '—'}</time>
                        <span class="log-text">
                          <span>{d.text}</span>
                          {d.detail && <small>{d.detail}</small>}
                        </span>
                      </li>
                    );
                  })}
                </ol>
              </section>
            ))
          )}
          {page?.next_before != null && (
            <Button onClick={older} loading={olderBusy} class="older">
              {t('hist_older')}
            </Button>
          )}
          {page && page.unsaved > 0 && <p class="field-help">{t('hist_unsaved', { n: page.unsaved })}</p>}
        </>
      )}
    </div>
  );
}

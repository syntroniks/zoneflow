// Program list and editor. The editor previews the next seven days with the
// controller's own rules (lib/preview.ts), including the other programs, so
// overlaps are visible before saving.
import { useEffect, useMemo, useState } from 'preact/hooks';
import { ApiError, call, type Program } from '../api/client.ts';
import { Icon } from '../components/icons.tsx';
import { Banner, Button, Card, Choice, Empty, Field, LinkButton, PageTitle, Stepper, Switch, TextField, confirmAction } from '../components/ui.tsx';
import { t, tn } from '../i18n/index.ts';
import { daysFromCivil, formatIsoDate } from '../lib/civil.ts';
import { formatDayLabel, formatDuration, formatTime, formatWeekdayShort, formatWhen } from '../lib/format.ts';
import { WEEKDAY_KEYS, adjustedSeconds, type PlannedRun } from '../lib/preview.ts';
import { navigate, onLinkClick, setNavigationGuard } from '../router.ts';
import { ensureSignedIn, loadConfig, runProgram } from '../state/actions.ts';
import { errorText } from '../state/errors.ts';
import { plannedRuns } from '../state/schedule.ts';
import { config, deviceNow, programs, signedIn, timeZone, toast, zoneLabel, zones } from '../state/store.ts';
import { programTotalSec } from './run.tsx';

// Monday first. 2024-01-07 was a Sunday: day serial + i gives weekday i.
const SUNDAY = daysFromCivil(2024, 1, 7);
const DISPLAY_ORDER = [1, 2, 3, 4, 5, 6, 0];
const dayName = (i: number) => formatWeekdayShort(SUNDAY + i);

export function daysSummary(d: Program['days']): string {
  const type = d?.type ?? 'weekdays';
  if (type === 'interval') return tn('days_every_n', d?.every ?? 1);
  if (type === 'odd') return t('days_odd');
  if (type === 'even') return t('days_even');
  const list = (d?.weekdays ?? WEEKDAY_KEYS) as readonly string[];
  if (list.length === 7) return t('days_all');
  if (list.length === 0) return t('days_none_selected');
  return DISPLAY_ORDER.filter((i) => list.includes(WEEKDAY_KEYS[i] as string)).map(dayName).join(', ');
}

export function ProgramsScreen() {
  const list = programs.value;
  const runs = plannedRuns();
  const now = deviceNow();
  const tz = timeZone.value;
  return (
    <div class="page">
      <PageTitle title={t('programs_title')}>
        {list.length < 16 && (
          <LinkButton href="/programs/new" variant="primary" icon="plus">
            {t('program_new')}
          </LinkButton>
        )}
      </PageTitle>
      {list.length === 0 ? (
        <Empty icon="calendar">{t('program_none')}</Empty>
      ) : (
        <ul class="program-list">
          {list.map((p) => {
            const next = runs.find((r) => r.programId === p.id && !r.skip);
            const name = p.name || t('program_name_default', { n: p.id ?? 0 });
            const zoneCount = zones.value.filter((z, i) => z.enabled && (p.durations_s?.[i] ?? 0) > 0).length;
            return (
              <li key={p.id} class={`card program ${p.enabled === false ? 'is-off' : ''}`}>
                <a href={`/programs/${p.id}`} onClick={onLinkClick} class="program-main">
                  <span class="program-name">
                    {name}
                    {p.enabled === false && <span class="badge tone-muted">{t('program_disabled')}</span>}
                  </span>
                  <span class="program-sub">
                    {daysSummary(p.days)} · {(p.starts ?? []).join(', ') || '—'}
                  </span>
                  <span class="program-sub">
                    {tn('program_zones', zoneCount)} · {formatDuration(programTotalSec(p))}
                  </span>
                  <span class="program-next">
                    <Icon name="clock" size={16} />
                    {p.enabled !== false && next ? t('program_next', { when: formatWhen(next.at, tz, now) }) : t('program_never')}
                  </span>
                </a>
                <Button icon="play" onClick={() => void runProgram(p.id ?? 0, name)} aria-label={`${t('program_run_now')}: ${name}`}>
                  {t('program_run_now')}
                </Button>
              </li>
            );
          })}
        </ul>
      )}
    </div>
  );
}

// ---- editor -------------------------------------------------------------------

interface Draft {
  id?: number;
  name: string;
  enabled: boolean;
  starts: string[];
  dayType: 'weekdays' | 'interval' | 'odd' | 'even';
  weekdays: string[];
  every: number;
  anchor: string;
  durations: number[];
  seasonal: boolean;
  obeySensor: boolean;
}

function toDraft(p: Program | undefined, nextId: number): Draft {
  const today = formatIsoDate(timeZone.value.localDay(deviceNow()));
  return {
    id: p?.id,
    name: p?.name ?? t('program_name_default', { n: nextId }),
    enabled: p?.enabled ?? true,
    starts: p?.starts?.length ? [...p.starts] : ['06:00'],
    dayType: p?.days?.type ?? 'weekdays',
    weekdays: p?.days?.weekdays ?? (p ? [...WEEKDAY_KEYS] : ['mon', 'wed', 'fri']),
    every: p?.days?.every ?? 2,
    anchor: p?.days?.anchor ?? today,
    durations: Array.from({ length: 8 }, (_, i) => p?.durations_s?.[i] ?? (p ? 0 : i < zones.value.length ? 600 : 0)),
    seasonal: p?.seasonal ?? true,
    obeySensor: p?.obey_sensor ?? true,
  };
}

function fromDraft(d: Draft): Program {
  const days: NonNullable<Program['days']> = { type: d.dayType };
  if (d.dayType === 'weekdays') days.weekdays = WEEKDAY_KEYS.filter((k) => d.weekdays.includes(k));
  if (d.dayType === 'interval') {
    days.every = d.every;
    days.anchor = d.anchor;
  }
  return {
    ...(d.id ? { id: d.id } : {}),
    name: d.name.trim(),
    enabled: d.enabled,
    starts: d.starts.filter(Boolean),
    days,
    durations_s: d.durations.slice(0, 8),
    seasonal: d.seasonal,
    obey_sensor: d.obeySensor,
  };
}

function Preview({ draft }: { draft: Program }) {
  const tz = timeZone.value;
  const now = deviceNow();
  const others = programs.value.filter((p) => p.id !== draft.id || draft.id === undefined);
  const id = draft.id ?? -1;
  const runs = plannedRuns([...others, { ...draft, id }]);
  const days = new Map<number, PlannedRun[]>();
  for (let i = 0; i < 7; i++) days.set(tz.localDay(now) + i, []);
  for (const r of runs) days.get(tz.localDay(r.at))?.push(r);
  const mine = runs.filter((r) => r.programId === id);
  return (
    <Card title={t('program_preview')}>
      <p class="field-help">{t('program_preview_help')}</p>
      {draft.enabled !== false && mine.length === 0 && <p class="note">{t('program_preview_empty')}</p>}
      <ol class="preview">
        {[...days.entries()].map(([day, list]) => (
          <li key={day}>
            <span class="preview-day">{formatDayLabel(tz.fromLocal(day, 43200), tz, now)}</span>
            <span class="preview-runs">
              {list.length === 0 && <span class="muted">—</span>}
              {list.map((r) => (
                <span key={`${r.programId}-${r.at}`} class={`run-chip ${r.programId === id ? 'is-mine' : ''} ${r.skip ? 'is-skipped' : ''}`} title={r.name}>
                  <span class="tabular">{formatTime(r.at, tz)}</span>
                  {r.programId === id ? ` · ${formatDuration(r.totalSec)}` : ` · ${r.name}`}
                  {r.skip && ` (${t(r.skip === 'rain_delay' ? 'preview_skip_rain_delay' : 'preview_skip_nothing_to_run')})`}
                </span>
              ))}
            </span>
          </li>
        ))}
      </ol>
    </Card>
  );
}

export function ProgramEditScreen({ id }: { id: number | 'new' }) {
  const existing = id === 'new' ? undefined : programs.value.find((p) => p.id === id);
  const nextId = Math.max(0, ...programs.value.map((p) => p.id ?? 0)) + 1;
  const [draft, setDraft] = useState<Draft>(() => toDraft(existing, nextId));
  const [dirty, setDirty] = useState(false);
  const [errors, setErrors] = useState<Record<string, string>>({});
  const [busy, setBusy] = useState(false);
  const pct = config.value?.schedule?.seasonal_percent ?? 100;

  // A program loaded after the first render (deep link before config arrived).
  useEffect(() => {
    if (existing && !dirty) setDraft(toDraft(existing, nextId));
  }, [existing?.id, config.value]);

  useEffect(() => {
    setNavigationGuard(dirty ? () => window.confirm(t('program_unsaved')) : null);
    return () => setNavigationGuard(null);
  }, [dirty]);

  const set = (patch: Partial<Draft>) => {
    setDraft({ ...draft, ...patch });
    setDirty(true);
  };
  const program = useMemo(() => fromDraft(draft), [draft]);
  const total = zones.value.reduce((s, z, i) => s + (z.enabled ? adjustedSeconds(draft.durations[i] ?? 0, draft.seasonal, pct) : 0), 0);

  if (id !== 'new' && !existing && config.value) {
    return (
      <div class="page">
        <PageTitle title={t('programs_title')} back="/programs" />
        <Empty>{t('err_unknown_program')}</Empty>
      </div>
    );
  }

  const save = async (e: Event) => {
    e.preventDefault();
    if (!ensureSignedIn()) return;
    setBusy(true);
    setErrors({});
    try {
      if (id === 'new') await call('post', '/programs', { body: program });
      else await call('put', '/programs/{id}', { params: { id }, body: program });
      setDirty(false);
      setNavigationGuard(null);
      await loadConfig();
      toast(t('program_saved'), 'success');
      navigate('/programs');
    } catch (x) {
      // Field errors go next to the field; every error is also announced,
      // so a path the form has no field for is never silent.
      if (x instanceof ApiError && x.path) {
        setErrors({ [x.path]: x.message });
        requestAnimationFrame(() => document.querySelector<HTMLElement>('[aria-invalid="true"]')?.focus());
      }
      toast(errorText(x), 'error');
    } finally {
      setBusy(false);
    }
  };

  const remove = async () => {
    if (id === 'new' || !ensureSignedIn()) return;
    if (!(await confirmAction({ title: t('program_delete'), body: t('program_delete_confirm', { name: draft.name }), confirmLabel: t('delete'), danger: true }))) return;
    try {
      await call('delete', '/programs/{id}', { params: { id } });
      setDirty(false);
      setNavigationGuard(null);
      await loadConfig();
      toast(t('program_deleted'), 'success');
      navigate('/programs');
    } catch (x) {
      toast(errorText(x), 'error');
    }
  };

  const ro = !signedIn.value;
  return (
    <div class="page">
      <PageTitle title={id === 'new' ? t('program_new') : draft.name || t('programs_title')} back="/programs">
        {id !== 'new' && (
          <Button icon="play" onClick={() => void runProgram(id, draft.name)} disabled={dirty}>
            {t('program_run_now')}
          </Button>
        )}
      </PageTitle>
      <form class="editor" onSubmit={save} noValidate>
        <div class="editor-main">
          <Card>
            <div class="stack">
              <TextField label={t('program_name')} value={draft.name} maxLength={31} onInput={(v) => set({ name: v })} error={errors['name']} disabled={ro} />
              <Switch label={t('program_enabled')} checked={draft.enabled} onChange={(v) => set({ enabled: v })} disabled={ro} />
            </div>
          </Card>

          <Card title={t('program_starts')}>
            <p class="field-help">{t('program_starts_help')}</p>
            <ul class="starts">
              {draft.starts.map((s, i) => (
                <li key={i}>
                  <Field label={<span class="sr-only">{`${t('program_starts')} ${i + 1}`}</span>} error={errors[`starts[${i}]`]}>
                    {({ id: fid, describedBy, invalid }) => (
                      <input
                        id={fid}
                        class="input time-input"
                        type="time"
                        required
                        value={s}
                        disabled={ro}
                        aria-describedby={describedBy}
                        aria-invalid={invalid || undefined}
                        onInput={(e) => {
                          const v = [...draft.starts];
                          v[i] = (e.currentTarget as HTMLInputElement).value;
                          set({ starts: v });
                        }}
                      />
                    )}
                  </Field>
                  {draft.starts.length > 1 && (
                    <button type="button" class="icon-btn" aria-label={t('program_remove_start', { time: s })} disabled={ro} onClick={() => set({ starts: draft.starts.filter((_, j) => j !== i) })}>
                      <Icon name="trash" size={20} />
                    </button>
                  )}
                </li>
              ))}
            </ul>
            {draft.starts.length < 4 && (
              <Button icon="plus" variant="ghost" disabled={ro} onClick={() => set({ starts: [...draft.starts, '18:00'] })}>
                {t('program_add_start')}
              </Button>
            )}
          </Card>

          <Card title={t('program_days')}>
            <div class="stack">
              <Choice
                kind="chips"
                label={t('program_days')}
                hideLabel
                value={draft.dayType}
                onChange={(v) => set({ dayType: v })}
                options={[
                  { value: 'weekdays', label: t('days_weekdays') },
                  { value: 'interval', label: t('days_interval') },
                  { value: 'odd', label: t('days_odd') },
                  { value: 'even', label: t('days_even') },
                ]}
              />
              {draft.dayType === 'weekdays' && (
                <fieldset class="weekdays">
                  <legend class="sr-only">{t('days_weekdays')}</legend>
                  {DISPLAY_ORDER.map((i) => {
                    const key = WEEKDAY_KEYS[i] as string;
                    const on = draft.weekdays.includes(key);
                    return (
                      <label key={key} class={on ? 'is-checked' : undefined}>
                        <input
                          type="checkbox"
                          checked={on}
                          disabled={ro}
                          onChange={() => set({ weekdays: on ? draft.weekdays.filter((w) => w !== key) : [...draft.weekdays, key] })}
                        />
                        <span>{dayName(i)}</span>
                      </label>
                    );
                  })}
                </fieldset>
              )}
              {draft.dayType === 'weekdays' && draft.weekdays.length === 0 && <Banner severity="warning" title={t('days_none_selected')} />}
              {draft.dayType === 'interval' && (
                <div class="two-col">
                  <Stepper label={t('days_every')} value={draft.every} min={1} max={365} unit={tn('days_unit', draft.every)} onChange={(v) => set({ every: v })} error={errors['days.every']} />
                  <TextField label={t('days_anchor')} type="date" value={draft.anchor} onInput={(v) => set({ anchor: v })} error={errors['days.anchor']} disabled={ro} />
                </div>
              )}
              {draft.dayType === 'odd' && <p class="field-help">{t('days_odd_help')}</p>}
              {draft.dayType === 'even' && <p class="field-help">{t('days_even_help')}</p>}
            </div>
          </Card>

          <Card title={t('program_durations')}>
            <p class="field-help">{t('program_durations_help')}</p>
            <ul class="durations">
              {zones.value.map((z, i) => (
                <li key={z.id} class={(draft.durations[i] ?? 0) > 0 ? 'is-on' : undefined}>
                  <span class="zone-num">{z.id}</span>
                  <span class="dur-name">
                    {zoneLabel(z.id)}
                    {!z.enabled && <span class="badge tone-muted">{t('zone_state_disabled')}</span>}
                    {(draft.durations[i] ?? 0) > z.maxRunSec && <small class="tone-warning">{t('run_capped', { time: formatDuration(z.maxRunSec) })}</small>}
                  </span>
                  <Stepper
                    label={zoneLabel(z.id)}
                    hideLabel
                    value={Math.round((draft.durations[i] ?? 0) / 60)}
                    min={0}
                    max={240}
                    unit={t('minutes_short')}
                    error={errors[`durations_s[${i}]`]}
                    onChange={(m) => {
                      const d = [...draft.durations];
                      d[i] = m * 60;
                      set({ durations: d });
                    }}
                  />
                </li>
              ))}
            </ul>
            <p class="total">
              {draft.seasonal && pct !== 100
                ? t('program_total_adjusted', { time: formatDuration(total), pct })
                : t('program_total', { time: formatDuration(total) })}
            </p>
          </Card>

          <Card>
            <div class="stack">
              <Switch label={t('program_seasonal')} help={t('program_seasonal_help', { pct })} checked={draft.seasonal} onChange={(v) => set({ seasonal: v })} disabled={ro} />
              <Switch label={t('program_obey_sensor')} checked={draft.obeySensor} onChange={(v) => set({ obeySensor: v })} disabled={ro} />
            </div>
          </Card>

          {id !== 'new' && (
            <Button variant="ghost" icon="trash" class="danger-text" onClick={remove} disabled={ro}>
              {t('program_delete')}
            </Button>
          )}
        </div>
        <div class="editor-side">
          <Preview draft={program} />
        </div>
        <div class="save-bar">
          <Button onClick={() => navigate('/programs')}>{t('cancel')}</Button>
          <Button variant="primary" type="submit" loading={busy} disabled={!dirty && id !== 'new'}>
            {t('save')}
          </Button>
        </div>
      </form>
    </div>
  );
}

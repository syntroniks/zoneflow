// Settings sections. Fields with their own route (time, security, zones,
// programs) use it; the rest edit the configuration document (PUT /config),
// which the controller validates as a whole.
import { useEffect, useState } from 'preact/hooks';
import { ApiError, call, restoreBackup, setCsrf, upload, type Config, type Schemas } from '../api/client.ts';
import { clockFormat, localeUses24h, setClockFormat } from '../lib/clock.ts';
import { Icon } from '../components/icons.tsx';
import { Banner, Button, Card, Choice, Field, KeyValue, PageTitle, PasswordField, Progress, Spinner, Stepper, Switch, TextField, confirmAction } from '../components/ui.tsx';
import { t, tk } from '../i18n/index.ts';
import { formatDate, formatOffset, formatTime } from '../lib/format.ts';
import { TIME_ZONES, zoneForPosix, zoneLabel as tzLabel } from '../lib/timezones.ts';
import { TimeZone } from '../lib/tz.ts';
import { navigate } from '../router.ts';
import { ensureSignedIn, loadConfig, updateConfig } from '../state/actions.ts';
import { errorText } from '../state/errors.ts';
import { config, deviceNow, ota, signedIn, status, toast } from '../state/store.ts';
import { SETTINGS_SECTIONS } from './more.tsx';
import { MqttSection } from './mqtt.tsx';


function useLoad<T>(fn: () => Promise<T>, deps: unknown[] = []): [T | null, () => void, string | null] {
  const [v, setV] = useState<T | null>(null);
  const [err, setErr] = useState<string | null>(null);
  const [n, setN] = useState(0);
  useEffect(() => {
    let live = true;
    fn().then(
      (x) => live && setV(x),
      (e) => live && setErr(errorText(e)),
    );
    return () => {
      live = false;
    };
  }, [n, ...deps]);
  return [v, () => setN(n + 1), err];
}

function SaveRow({ busy, disabled }: { busy: boolean; disabled?: boolean }) {
  return (
    <div class="form-actions">
      <Button variant="primary" type="submit" loading={busy} disabled={disabled || !signedIn.value}>
        {t('save')}
      </Button>
    </div>
  );
}

/** A form over a few configuration fields; saves with PUT /config. */
function ConfigForm<T extends object>({ read, write, children }: { read: (c: Config) => T; write: (c: Config, v: T) => void; children: (v: T, set: (p: Partial<T>) => void) => preact.ComponentChildren }) {
  const c = config.value;
  const [v, setV] = useState<T | null>(c ? read(c) : null);
  const [dirty, setDirty] = useState(false);
  const [busy, setBusy] = useState(false);
  useEffect(() => {
    if (c && !dirty) setV(read(c));
  }, [c]);
  if (!v) return <Spinner label={t('loading')} />;
  return (
    <form
      class="stack"
      onSubmit={async (e) => {
        e.preventDefault();
        setBusy(true);
        if (await updateConfig((doc) => write(doc, v))) setDirty(false);
        setBusy(false);
      }}
    >
      {children(v, (p) => {
        setV({ ...v, ...p });
        setDirty(true);
      })}
      <SaveRow busy={busy} disabled={!dirty} />
    </form>
  );
}

function DeviceSection() {
  return (
    <>
    <Card>
      <ConfigForm read={(c) => ({ name: c.device_name })} write={(c, v) => (c.device_name = v.name.trim())}>
        {(v, set) => <TextField label={t('device_name')} help={t('device_name_help')} value={v.name} maxLength={31} onInput={(x) => set({ name: x })} disabled={!signedIn.value} />}
      </ConfigForm>
    </Card>
    <Card>
      <Choice
        kind="chips"
        label={t('clock_format')}
        value={clockFormat.value}
        onChange={setClockFormat}
        options={[
          { value: 'auto', label: t('clock_auto', { example: localeUses24h(navigator.language) ? '18:30' : '6:30 PM' }) },
          { value: '24', label: t('clock_24') },
          { value: '12', label: t('clock_12') },
        ]}
      />
      <p class="field-help">{t('clock_format_help')}</p>
    </Card>
    </>
  );
}

function TimeSection() {
  const [time, reload] = useLoad(() => call('get', '/time'));
  const [posix, setPosix] = useState<string | null>(null);
  const [choice, setChoice] = useState<string>('');
  const [busy, setBusy] = useState(false);
  useEffect(() => {
    if (time && posix === null) {
      const p = time.timezone ?? 'UTC0';
      setPosix(p);
      setChoice(zoneForPosix(p) ?? 'custom');
    }
  }, [time]);
  if (!time || posix === null) return <Spinner label={t('loading')} />;
  const parsed = TimeZone.parse(posix);
  const now = deviceNow();
  const save = async (e: Event) => {
    e.preventDefault();
    if (!ensureSignedIn() || !parsed) return;
    setBusy(true);
    try {
      await call('put', '/time', { body: { timezone: posix } });
      await loadConfig();
      reload();
      toast(t('saved'), 'success');
    } catch (x) {
      toast(errorText(x), 'error');
    }
    setBusy(false);
  };
  return (
    <>
      <Card>
        <KeyValue
          items={[
            [t('time_now'), parsed ? `${formatDate(now, parsed)} ${formatTime(now, parsed)}` : '—'],
            [t('time_status'), tk(`time_status_${time.status ?? 'unset'}`)],
          ]}
        />
        {time.rtc_present === false && <Banner severity="warning" title={t('time_rtc_missing')} />}
        <Button
          icon="clock"
          onClick={async () => {
            if (!ensureSignedIn()) return;
            try {
              await call('put', '/time', { body: { utc: Math.floor(Date.now() / 1000) } });
              reload();
              toast(t('saved'), 'success');
            } catch (x) {
              toast(errorText(x), 'error');
            }
          }}
        >
          {t('time_set_from_phone')}
        </Button>
        <p class="field-help">{t('time_set_from_phone_help')}</p>
      </Card>
      <Card title={t('time_zone')}>
        <TimeZoneForm posix={posix} choice={choice} setPosix={setPosix} setChoice={setChoice} onSubmit={save} busy={busy} />
      </Card>
    </>
  );
}

export function TimeZoneForm({ posix, choice, setPosix, setChoice, onSubmit, busy, submitLabel }: { posix: string; choice: string; setPosix: (p: string) => void; setChoice: (c: string) => void; onSubmit: (e: Event) => void; busy: boolean; submitLabel?: string }) {
  const parsed = TimeZone.parse(posix);
  const now = deviceNow();
  return (
    <form class="stack" onSubmit={onSubmit}>
      <Field label={t('time_zone')}>
        {({ id }) => (
          <select
            id={id}
            class="input"
            value={choice}
            onChange={(e) => {
              const v = (e.currentTarget as HTMLSelectElement).value;
              setChoice(v);
              const z = TIME_ZONES.find(([n]) => n === v);
              if (z) setPosix(z[1]);
            }}
          >
            {TIME_ZONES.map(([n]) => (
              <option key={n} value={n}>
                {tzLabel(n)}
              </option>
            ))}
            <option value="custom">{t('time_zone_custom')}</option>
          </select>
        )}
      </Field>
      {choice === 'custom' && (
        <TextField label={t('time_zone_posix')} help={t('time_zone_posix_help')} value={posix} maxLength={63} spellcheck={false} autocapitalize="off" onInput={setPosix} error={parsed ? null : t('time_zone_invalid')} />
      )}
      {parsed && (
        <p class="muted small">
          {t('time_offset', { offset: formatOffset(parsed.offsetAt(now)), dst: parsed.isDstAt(now) ? t('time_dst') : '' })} · {formatTime(now, parsed)}
        </p>
      )}
      <div class="form-actions">
        <Button variant="primary" type="submit" loading={busy} disabled={!parsed}>
          {submitLabel ?? t('save')}
        </Button>
      </div>
    </form>
  );
}

function WateringSection() {
  return (
    <Card>
      <ConfigForm
        read={(c) => ({
          zones: c.zone_count,
          pct: c.schedule.seasonal_percent,
          grace: c.schedule.missed_start_grace_min,
          pause: c.schedule.pause_timeout_min,
          rest: Math.round(c.driver.min_rest_s / 60),
          master: c.master.enabled,
        })}
        write={(c, v) => {
          c.zone_count = v.zones;
          c.schedule.seasonal_percent = v.pct;
          c.schedule.missed_start_grace_min = v.grace;
          c.schedule.pause_timeout_min = v.pause;
          c.driver.min_rest_s = v.rest * 60;
          c.master.enabled = v.master;
        }}
      >
        {(v, set) => (
          <>
            <Stepper label={t('seasonal_percent')} help={t('seasonal_help')} value={v.pct} min={0} max={250} step={5} unit="%" onChange={(x) => set({ pct: x })} />
            <Stepper label={t('zone_count')} value={v.zones} min={1} max={8} onChange={(x) => set({ zones: x })} />
            <Stepper label={t('grace')} help={t('grace_help')} value={v.grace} min={0} max={720} step={15} unit={t('minutes_short')} onChange={(x) => set({ grace: x })} />
            <Stepper label={t('pause_timeout')} help={t('pause_timeout_help')} value={v.pause} min={1} max={1440} step={15} unit={t('minutes_short')} onChange={(x) => set({ pause: x })} />
            <Stepper label={t('min_rest')} help={t('min_rest_help')} value={v.rest} min={1} max={1440} step={5} unit={t('minutes_short')} onChange={(x) => set({ rest: x })} />
            <Switch label={t('master_enabled')} help={t('master_enabled_help')} checked={v.master} onChange={(x) => set({ master: x })} disabled={!signedIn.value} />
          </>
        )}
      </ConfigForm>
    </Card>
  );
}

function SensorsSection() {
  return (
    <Card>
      <ConfigForm
        read={(c) => ({
          rain: c.sensors.rain.enabled,
          contact: c.sensors.rain.contact,
          flow: c.sensors.flow.enabled,
          ppl: c.sensors.flow.pulses_per_litre,
          leak: c.sensors.flow.leak_threshold_lpm,
          current: c.diagnostics.current_enabled,
        })}
        write={(c, v) => {
          c.sensors.rain.enabled = v.rain;
          c.sensors.rain.contact = v.contact;
          c.sensors.flow.enabled = v.flow;
          c.sensors.flow.pulses_per_litre = v.ppl;
          c.sensors.flow.leak_threshold_lpm = v.leak;
          c.diagnostics.current_enabled = v.current;
        }}
      >
        {(v, set) => (
          <>
            <Switch label={t('rain_enabled')} checked={v.rain} onChange={(x) => set({ rain: x })} disabled={!signedIn.value} />
            {v.rain && (
              <Choice
                label={t('rain_contact')}
                value={v.contact}
                onChange={(x) => set({ contact: x })}
                options={[
                  { value: 'nc', label: t('contact_nc') },
                  { value: 'no', label: t('contact_no') },
                ]}
              />
            )}
            <Switch label={t('flow_enabled')} checked={v.flow} onChange={(x) => set({ flow: x })} disabled={!signedIn.value} />
            {v.flow && (
              <div class="two-col">
                <TextField label={t('flow_ppl')} help={t('flow_ppl_help')} inputMode="decimal" value={String(v.ppl)} onInput={(x) => set({ ppl: Number(x) || 0 })} />
                <TextField label={t('leak_threshold')} inputMode="decimal" value={String(v.leak)} onInput={(x) => set({ leak: Number(x) || 0 })} />
              </div>
            )}
            <Switch label={t('current_enabled')} help={t('current_enabled_help')} checked={v.current} onChange={(x) => set({ current: x })} disabled={!signedIn.value} />
          </>
        )}
      </ConfigForm>
    </Card>
  );
}

function signalText(rssi: number): string {
  const key = rssi >= -55 ? 'signal_excellent' : rssi >= -67 ? 'signal_good' : rssi >= -75 ? 'signal_fair' : 'signal_weak';
  return t(key, { rssi });
}

function NetworkSection() {
  const [fetched] = useLoad(() => call('get', '/network'));
  const n = status.value?.network ?? fetched;
  if (!n) return <Spinner label={t('loading')} />;
  const reprov = async (transport: 'ble' | 'softap') => {
    if (!ensureSignedIn()) return;
    if (!(await confirmAction({ title: t('net_reprovision'), body: t('net_reprovision_confirm'), confirmLabel: t('confirm') }))) return;
    try {
      await call('post', '/network/reprovision', { body: { transport } });
    } catch (x) {
      toast(errorText(x), 'error');
    }
  };
  return (
    <>
      <Card>
        <KeyValue
          items={[
            [t('net_state'), tk(`net_state_${n.state ?? 'off'}`)],
            [t('net_ssid'), n.ssid || '—'],
            [t('net_signal'), n.rssi ? signalText(n.rssi) : '—'],
            [t('net_ip'), n.ip || '—'],
            [t('net_hostname'), n.hostname ? `${n.hostname}.local` : '—'],
            [t('net_gateway'), n.gateway || '—'],
            [t('net_mac'), n.mac || '—'],
          ]}
        />
      </Card>
      <Card title={t('net_reprovision')}>
        <p class="muted">{t('net_reprovision_help')}</p>
        <div class="row-actions wrap">
          <Button onClick={() => void reprov('ble')}>{t('net_reprovision_ble')}</Button>
          <Button onClick={() => void reprov('softap')}>{t('net_reprovision_softap')}</Button>
        </div>
      </Card>
    </>
  );
}

function SecuritySection() {
  const [sec, reload] = useLoad(() => call('get', '/security'));
  const [cur, setCur] = useState('');
  const [pw, setPw] = useState('');
  const [pw2, setPw2] = useState('');
  const [err, setErr] = useState<Record<string, string>>({});
  const [busy, setBusy] = useState(false);
  const put = async (body: Schemas['SecurityUpdate']) => {
    if (!ensureSignedIn()) return;
    try {
      await call('put', '/security', { body });
      reload();
    } catch (x) {
      toast(errorText(x), 'error');
    }
  };
  const change = async (e: Event) => {
    e.preventDefault();
    const errs: Record<string, string> = {};
    if (pw.length < 8) errs['new'] = t('password_short');
    else if (pw.length > 64) errs['new'] = t('password_long');
    if (pw !== pw2) errs['again'] = t('password_mismatch');
    setErr(errs);
    if (Object.keys(errs).length || !ensureSignedIn()) return;
    setBusy(true);
    try {
      const s = await call('post', '/auth/password', { body: { current_password: cur, new_password: pw } });
      setCsrf(s.csrf);
      setCur('');
      setPw('');
      setPw2('');
      toast(t('pw_changed'), 'success');
    } catch (x) {
      if (x instanceof ApiError && x.code === 'bad_credentials') setErr({ cur: t('pw_wrong') });
      else toast(errorText(x), 'error');
    }
    setBusy(false);
  };
  return (
    <>
      <Card title={t('pw_change')}>
        <form class="stack" onSubmit={change}>
          <input type="text" name="username" autocomplete="username" value="admin" hidden readOnly />
          <PasswordField label={t('pw_current')} value={cur} onInput={setCur} error={err['cur']} autocomplete="current-password" />
          <PasswordField label={t('pw_new')} help={t('new_password_help')} value={pw} onInput={setPw} error={err['new']} autocomplete="new-password" />
          <PasswordField label={t('confirm_password')} value={pw2} onInput={setPw2} error={err['again']} autocomplete="new-password" />
          <div class="form-actions">
            <Button variant="primary" type="submit" loading={busy} disabled={!cur || !pw || !signedIn.value}>
              {t('pw_change')}
            </Button>
          </div>
        </form>
      </Card>
      {!sec ? (
        <Spinner label={t('loading')} />
      ) : (
        <>
          <Card>
            <div class="stack">
              <Switch label={t('protect_reads')} help={t('protect_reads_help')} checked={!!sec.protect_reads} onChange={(v) => void put({ protect_reads: v })} disabled={!signedIn.value} />
              <Switch label={t('check_host')} help={t('check_host_help')} checked={sec.check_host !== false} onChange={(v) => void put({ check_host: v })} disabled={!signedIn.value} />
              <p class="muted small">{t('sessions_open', { n: sec.sessions ?? 0 })}</p>
            </div>
          </Card>
          <Card title={t('https_title')}>
            <div class="stack">
              <p class="muted">{t('https_help')}</p>
              <Switch label={t('https_enable')} checked={!!sec.https} onChange={(v) => void put({ https: v })} disabled={!signedIn.value} />
              {sec.tls_fingerprint_sha256 && <KeyValue items={[[t('https_fingerprint'), <code class="fingerprint">{sec.tls_fingerprint_sha256}</code>]]} />}
              {sec.restart_required && <Banner severity="warning" title={t('https_restart')} action={<Button size="sm" onClick={() => navigate('/settings/system')}>{t('reboot')}</Button>} />}
            </div>
          </Card>
        </>
      )}
    </>
  );
}

function BackupSection() {
  const [busy, setBusy] = useState(false);
  const download = async () => {
    try {
      const b = await call('get', '/config/backup');
      const date = new Date((b.created_utc ?? Date.now() / 1000) * 1000).toISOString().slice(0, 10);
      const blob = new Blob([JSON.stringify(b, null, 2)], { type: 'application/json' });
      const a = document.createElement('a');
      a.href = URL.createObjectURL(blob);
      a.download = `zoneflow-backup-${b.hostname ?? 'device'}-${date}.json`;
      a.click();
      setTimeout(() => URL.revokeObjectURL(a.href), 1000);
    } catch (x) {
      toast(errorText(x), 'error');
    }
  };
  const restore = async (file: File) => {
    if (!ensureSignedIn()) return;
    let doc: Schemas['Backup'];
    try {
      doc = JSON.parse(await file.text()) as Schemas['Backup'];
      if (doc.format !== 'zoneflow-backup' || !doc.config) throw new Error('format');
    } catch {
      toast(t('restore_bad_file'), 'error');
      return;
    }
    const date = doc.created_utc ? new Date(doc.created_utc * 1000).toLocaleString() : '?';
    if (!(await confirmAction({ title: t('restore_choose'), body: t('restore_confirm', { date, host: doc.hostname ?? '?' }), confirmLabel: t('confirm'), danger: true }))) return;
    setBusy(true);
    try {
      await restoreBackup(doc);
      await loadConfig();
      toast(t('restore_done'), 'success');
    } catch (x) {
      toast(errorText(x), 'error');
    }
    setBusy(false);
  };
  return (
    <Card>
      <div class="stack">
        <p class="muted">{t('backup_help')}</p>
        <Button icon="download" onClick={download}>
          {t('backup_download')}
        </Button>
        <FileButton label={t('restore_choose')} accept="application/json,.json" busy={busy} onFile={restore} />
      </div>
    </Card>
  );
}

function FileButton({ label, accept, onFile, busy, disabled }: { label: string; accept: string; onFile: (f: File) => void; busy?: boolean; disabled?: boolean }) {
  return (
    <label class={`btn btn-secondary file-btn ${disabled ? 'is-disabled' : ''}`} aria-busy={busy || undefined}>
      <Icon name="upload" size={20} />
      <span>{label}</span>
      <input
        type="file"
        accept={accept}
        class="sr-only"
        disabled={disabled || busy}
        onChange={(e) => {
          const f = (e.currentTarget as HTMLInputElement).files?.[0];
          (e.currentTarget as HTMLInputElement).value = '';
          if (f) onFile(f);
        }}
      />
    </label>
  );
}

function otaText(o: Schemas['OtaStatus'], pct: number): string {
  switch (o.phase) {
    case 'preparing':
      return t('ota_preparing');
    case 'receiving':
      return t('ota_receiving', { pct });
    case 'verifying':
      return t('ota_verifying');
    case 'rebooting':
      return t('ota_rebooting', { version: o.new_version ?? '' });
    case 'failed':
      return t('ota_failed', { error: tk(`ota_err_${o.error ?? 'aborted'}`) });
    default:
      return t('ota_idle');
  }
}

function FirmwareSection() {
  const [fw, reload] = useLoad(() => call('get', '/firmware'));
  const [sent, setSent] = useState<{ sent: number; total: number } | null>(null);
  const [url, setUrl] = useState('');
  const [uiDone, setUiDone] = useState(false);
  const o = ota.value ?? fw?.ota ?? null;
  const watering = (status.value?.queue?.state ?? 'idle') !== 'idle' || (status.value?.zones ?? []).some((z) => z.state !== 'off');
  const active = !!o && o.phase !== 'idle' && o.phase !== 'failed';
  const pct = o?.total_bytes ? Math.round(((o.received_bytes ?? 0) / o.total_bytes) * 100) : 0;

  /** Confirms, and with watering running asks again with the forced-update warning. */
  const confirmUpdate = async (body: string): Promise<boolean | 'force'> => {
    if (!ensureSignedIn()) return false;
    if (!(await confirmAction({ title: t('set_firmware'), body: <><p>{body}</p><p class="muted">{t('fw_rollback_note')}</p></>, confirmLabel: t('confirm') }))) return false;
    if (!watering) return true;
    return (await confirmAction({ title: t('fw_force_title'), body: t('fw_force_warning'), confirmLabel: t('fw_force_go'), danger: true })) ? 'force' : false;
  };

  const sendFile = async (file: File, kind: 'fw' | 'ui') => {
    const ok = await confirmUpdate(kind === 'fw' ? t('fw_confirm', { file: file.name }) : t('fw_confirm_ui', { file: file.name }));
    if (!ok) return;
    setUiDone(false);
    setSent({ sent: 0, total: file.size });
    try {
      await upload(kind === 'fw' ? '/firmware/upload' : '/firmware/assets', file, {
        query: ok === 'force' ? { force: 'true' } : {},
        onProgress: (s, total) => setSent({ sent: s, total }),
      });
      if (kind === 'ui') setUiDone(true);
    } catch (x) {
      toast(x instanceof ApiError && x.code === 'zones_active' ? t('err_zones_active') : errorText(x), 'error');
    } finally {
      setSent(null);
      reload();
    }
  };

  const pull = async (e: Event) => {
    e.preventDefault();
    const ok = await confirmUpdate(t('fw_confirm', { file: url }));
    if (!ok) return;
    try {
      await call('post', '/firmware/pull', { body: { url, force: ok === 'force' } });
    } catch (x) {
      toast(errorText(x), 'error');
    }
  };

  return (
    <>
      <Card>
        {fw ? (
          <KeyValue
            items={[
              [t('fw_version'), fw.version ?? '—'],
              [t('fw_build'), fw.build_date ?? '—'],
              [t('fw_slot'), fw.running_slot ?? '—'],
              [t('fw_state'), fw.running_state ?? '—'],
              [t('fw_other'), fw.other_slot_version || '—'],
            ]}
          />
        ) : (
          <Spinner label={t('loading')} />
        )}
      </Card>
      {(sent || (o && o.phase !== 'idle')) && (
        <Card>
          <div class="stack" aria-live="polite">
            {sent && !active ? (
              <>
                <p>{t('ota_uploading', { pct: sent.total ? Math.round((sent.sent / sent.total) * 100) : 0 })}</p>
                <Progress value={sent.sent} max={sent.total} label={t('ota_uploading', { pct: '' })} />
              </>
            ) : (
              o && (
                <>
                  <p class={o.phase === 'failed' ? 'tone-error' : undefined}>{otaText(o, pct)}</p>
                  {o.phase === 'receiving' && <Progress value={o.received_bytes ?? 0} max={o.total_bytes ?? 1} label={otaText(o, pct)} />}
                  {o.phase === 'rebooting' && <Button onClick={() => location.reload()}>{t('ota_reload')}</Button>}
                </>
              )
            )}
          </div>
        </Card>
      )}
      {uiDone && <Banner severity="success" title={t('ota_done_ui')} action={<Button size="sm" onClick={() => location.reload()}>{t('ota_reload')}</Button>} />}
      {watering && <Banner severity="warning" title={t('fw_force_title')}>{t('fw_force_warning')}</Banner>}
      <Card title={t('set_firmware')}>
        <div class="stack">
          <FileButton label={t('fw_upload')} accept=".bin,application/octet-stream" disabled={active || !!sent} onFile={(f) => void sendFile(f, 'fw')} />
          <p class="field-help">{t('fw_upload_help')}</p>
          <form class="stack" onSubmit={pull}>
            <TextField label={t('fw_pull_url')} type="url" inputMode="url" value={url} onInput={setUrl} placeholder="https://github.com/…/zoneflow-fw.bin" pattern="https://.*" />
            <Button type="submit" disabled={!url.startsWith('https://') || active}>
              {t('fw_pull_go')}
            </Button>
          </form>
        </div>
      </Card>
      <Card title={t('about_ui_version')}>
        <FileButton label={t('fw_ui_upload')} accept=".bin,application/octet-stream" disabled={active || !!sent} onFile={(f) => void sendFile(f, 'ui')} />
        <p class="field-help">{t('fw_ui_upload_help')}</p>
      </Card>
    </>
  );
}

function SystemSection() {
  const [rebooting, setRebooting] = useState(false);
  return (
    <>
      <Card title={t('reboot')}>
        <p class="muted">{t('reboot_help')}</p>
        {rebooting && <Banner severity="info" title={t('rebooting')} />}
        <Button
          icon="power"
          onClick={async () => {
            if (!ensureSignedIn()) return;
            if (!(await confirmAction({ title: t('reboot'), body: t('reboot_confirm'), confirmLabel: t('reboot'), danger: true }))) return;
            try {
              await call('post', '/system/reboot');
              setRebooting(true);
            } catch (x) {
              toast(errorText(x), 'error');
            }
          }}
        >
          {t('reboot')}
        </Button>
      </Card>
      <Card title={t('factory_reset')} class="danger-zone">
        <p class="muted">{t('factory_reset_help')}</p>
        <Button
          variant="danger"
          icon="trash"
          onClick={async () => {
            if (!ensureSignedIn()) return;
            const ok = await confirmAction({ title: t('factory_reset'), body: t('factory_reset_confirm'), confirmLabel: t('factory_reset_go'), danger: true, typed: t('factory_reset_word') });
            if (!ok) return;
            try {
              await call('post', '/system/factory-reset', { body: { confirm: 'factory-reset' } });
              toast(t('factory_reset_done'));
            } catch (x) {
              toast(errorText(x), 'error');
            }
          }}
        >
          {t('factory_reset')}
        </Button>
      </Card>
    </>
  );
}

export function SettingsScreen({ section }: { section: string }) {
  const s = SETTINGS_SECTIONS.find((x) => x.slug === section);
  if (!s) {
    navigate('/more', { replace: true });
    return null;
  }
  const body: Record<string, () => preact.JSX.Element> = {
    device: DeviceSection,
    time: TimeSection,
    watering: WateringSection,
    sensors: SensorsSection,
    network: NetworkSection,
    mqtt: MqttSection,
    security: SecuritySection,
    backup: BackupSection,
    firmware: FirmwareSection,
    system: SystemSection,
  };
  const Body = body[section]!;
  return (
    <div class="page narrow">
      <PageTitle title={t(s.key)} back="/more" />
      {!signedIn.value && <Banner severity="info" icon="lock" title={t('read_only_note')} />}
      <Body />
    </div>
  );
}


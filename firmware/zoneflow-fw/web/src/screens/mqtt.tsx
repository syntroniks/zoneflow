// MQTT and Home Assistant settings (GET/PUT /api/v1/mqtt). The password and
// the CA certificate are write-only: the controller only says whether they
// are stored. The connection state is polled while the page is open.
import { useEffect, useRef, useState } from 'preact/hooks';
import { ApiError, call, type Mqtt, type MqttUpdate } from '../api/client.ts';
import { Banner, Button, Card, Choice, KeyValue, PasswordField, Spinner, Switch, TextField, confirmAction } from '../components/ui.tsx';
import { Icon } from '../components/icons.tsx';
import { t, tk } from '../i18n/index.ts';
import { formatDuration } from '../lib/format.ts';
import { setNavigationGuard } from '../router.ts';
import { ensureSignedIn } from '../state/actions.ts';
import { errorText } from '../state/errors.ts';
import { signedIn, toast } from '../state/store.ts';

const POLL_MS = 5000;
const MAX_PEM = 6144;

type Form = Pick<Mqtt, 'enabled' | 'host' | 'port' | 'username' | 'tls' | 'ca' | 'client_id' | 'base_topic' | 'discovery' | 'discovery_prefix' | 'ha_zone_entity'>;

function toForm(m: Mqtt): Form {
  const { enabled, host, port, username, tls, ca, client_id, base_topic, discovery, discovery_prefix, ha_zone_entity } = m;
  return { enabled, host, port, username, tls, ca, client_id, base_topic, discovery, discovery_prefix, ha_zone_entity };
}

const STATE_TONE: Record<string, string> = { connected: 'success', connecting: 'info', waiting_retry: 'warning', waiting_network: 'warning', disabled: 'muted' };

function StatusCard({ m }: { m: Mqtt }) {
  const s = m.status;
  const tone = STATE_TONE[s.state] ?? 'muted';
  return (
    <Card title={t('mqtt_status')}>
      <p class={`mqtt-state tone-${tone}`} role="status">
        <Icon name={s.state === 'connected' ? 'check' : s.state === 'disabled' ? 'info' : 'wifi'} size={20} />
        {tk(`mqtt_state_${s.state}`, { time: formatDuration((s.retry_in_ms ?? 0) / 1000) }, s.state)}
      </p>
      {s.state !== 'disabled' && (
        <KeyValue
          items={[
            [t('mqtt_broker'), s.broker || '—'],
            [t('mqtt_root_topic'), <code>{s.root_topic || '—'}</code>],
            [t('mqtt_connected_for'), s.connected_for_s != null ? formatDuration(s.connected_for_s) : '—'],
            [t('mqtt_reconnects'), `${s.connects} / ${s.reconnect_attempt}`],
            [t('mqtt_last_error'), s.last_error || '—'],
            [t('mqtt_messages'), t('mqtt_messages_val', { sent: s.published, failed: s.publish_failed, received: s.commands_received, rejected: s.commands_rejected })],
            [t('mqtt_discovery_sent'), s.discovery_published ? t('yes') : t('no')],
          ]}
        />
      )}
    </Card>
  );
}

export function MqttSection() {
  const [m, setM] = useState<Mqtt | null>(null);
  const [form, setForm] = useState<Form | null>(null);
  const [dirty, setDirty] = useState(false);
  const [password, setPassword] = useState('');
  const [pem, setPem] = useState<{ name: string; text: string } | null>(null);
  const [errors, setErrors] = useState<Record<string, string>>({});
  const [busy, setBusy] = useState(false);
  const [loadError, setLoadError] = useState<string | null>(null);
  // The poll refreshes the status but never overwrites fields being edited.
  const dirtyRef = useRef(false);
  dirtyRef.current = dirty || password !== '' || pem !== null;

  useEffect(() => {
    let alive = true;
    const load = async () => {
      try {
        const x = await call('get', '/mqtt');
        if (!alive) return;
        setM(x);
        setLoadError(null);
        setForm((f) => (f && dirtyRef.current ? f : toForm(x)));
      } catch (e) {
        if (alive) setLoadError(errorText(e));
      }
    };
    void load();
    const timer = setInterval(load, POLL_MS);
    return () => {
      alive = false;
      clearInterval(timer);
    };
  }, []);

  useEffect(() => {
    const unsaved = dirty || password !== '' || pem !== null;
    setNavigationGuard(unsaved ? () => window.confirm(t('program_unsaved')) : null);
    return () => setNavigationGuard(null);
  }, [dirty, password, pem]);

  if (loadError && !m) return <Banner severity="error" title={loadError} />;
  if (!m || !form) return <Spinner label={t('loading')} />;
  const ro = !signedIn.value;
  const set = (p: Partial<Form>) => {
    setForm({ ...form, ...p });
    setDirty(true);
  };

  const put = async (body: MqttUpdate, done?: string) => {
    if (!ensureSignedIn()) return false;
    setBusy(true);
    setErrors({});
    try {
      const x = await call('put', '/mqtt', { body });
      setM(x);
      setForm(toForm(x));
      setDirty(false);
      setPassword('');
      setPem(null);
      toast(done ?? t('saved'), 'success');
      return true;
    } catch (e) {
      if (e instanceof ApiError && e.path) setErrors({ [e.path]: e.message });
      toast(errorText(e), 'error');
      return false;
    } finally {
      setBusy(false);
    }
  };

  const save = async (e: Event) => {
    e.preventDefault();
    const body: MqttUpdate = { ...form };
    if (password !== '') body.password = password;
    if (pem) body.ca_cert = pem.text;
    await put(body);
  };

  return (
    <>
      <StatusCard m={m} />
      <form class="stack" onSubmit={save}>
        <Card title={t('mqtt_broker_title')}>
          <div class="stack">
            <Switch label={t('mqtt_enabled')} help={t('mqtt_enabled_help')} checked={form.enabled} onChange={(v) => set({ enabled: v })} disabled={ro} />
            <div class="two-col host-port">
              <TextField label={t('mqtt_host')} help={t('mqtt_host_help')} value={form.host} maxLength={63} spellcheck={false} autocapitalize="off" inputMode="url" onInput={(v) => set({ host: v.trim() })} error={errors['host']} disabled={ro} placeholder="192.168.1.10" />
              <TextField label={t('mqtt_port')} value={String(form.port)} inputMode="numeric" onInput={(v) => set({ port: Number(v.replace(/\D/g, '')) || 0 })} error={errors['port']} disabled={ro} />
            </div>
            <Switch
              label={t('mqtt_tls')}
              help={t('mqtt_tls_help')}
              checked={form.tls}
              onChange={(v) => set({ tls: v, port: v && form.port === 1883 ? 8883 : !v && form.port === 8883 ? 1883 : form.port })}
              disabled={ro}
            />
            {form.tls && (
              <>
                <Choice
                  label={t('mqtt_ca')}
                  value={form.ca}
                  onChange={(v) => set({ ca: v })}
                  options={[
                    { value: 'bundle', label: t('mqtt_ca_bundle') },
                    { value: 'custom', label: t('mqtt_ca_custom') },
                  ]}
                />
                {form.ca === 'custom' && (
                  <div class="stack-sm">
                    <p class="field-help">
                      {pem ? t('mqtt_ca_chosen', { name: pem.name }) : m.ca_cert_set ? t('mqtt_ca_stored') : t('mqtt_ca_missing')}
                    </p>
                    {errors['ca_cert'] && <p class="field-error">{errors['ca_cert']}</p>}
                    <div class="row-actions wrap">
                      <label class={`btn btn-secondary file-btn ${ro ? 'is-disabled' : ''}`}>
                        <Icon name="upload" size={20} />
                        <span>{t('mqtt_ca_upload')}</span>
                        <input
                          type="file"
                          accept=".pem,.crt,.cer,application/x-pem-file"
                          class="sr-only"
                          disabled={ro}
                          onChange={async (e) => {
                            const f = (e.currentTarget as HTMLInputElement).files?.[0];
                            (e.currentTarget as HTMLInputElement).value = '';
                            if (!f) return;
                            const text = await f.text();
                            if (f.size > MAX_PEM || !text.includes('-----BEGIN CERTIFICATE-----')) {
                              toast(t('mqtt_ca_bad'), 'error');
                              return;
                            }
                            setPem({ name: f.name, text });
                          }}
                        />
                      </label>
                      {m.ca_cert_set && !pem && (
                        <Button
                          variant="ghost"
                          disabled={ro || busy}
                          onClick={async () => {
                            if (await confirmAction({ title: t('mqtt_ca_remove'), body: t('mqtt_ca_remove_confirm'), confirmLabel: t('mqtt_ca_remove'), danger: true }))
                              await put({ ca_cert: null }, t('mqtt_ca_removed'));
                          }}
                        >
                          {t('mqtt_ca_remove')}
                        </Button>
                      )}
                    </div>
                  </div>
                )}
              </>
            )}
          </div>
        </Card>
        <Card title={t('mqtt_login')}>
          <div class="stack">
            <TextField label={t('mqtt_username')} value={form.username} maxLength={63} autoComplete="off" spellcheck={false} autocapitalize="off" onInput={(v) => set({ username: v })} error={errors['username']} disabled={ro} />
            <PasswordField
              label={t('mqtt_password')}
              help={m.password_set ? t('mqtt_password_stored') : t('mqtt_password_none')}
              value={password}
              onInput={setPassword}
              autocomplete="new-password"
              error={errors['password']}
            />
            {m.password_set && (
              <Button
                variant="ghost"
                disabled={ro || busy}
                onClick={async () => {
                  if (await confirmAction({ title: t('mqtt_password_remove'), body: t('mqtt_password_remove_confirm'), confirmLabel: t('mqtt_password_remove'), danger: true }))
                    await put({ password: null }, t('mqtt_password_removed'));
                }}
              >
                {t('mqtt_password_remove')}
              </Button>
            )}
          </div>
        </Card>
        <Card title={t('mqtt_topics')}>
          <div class="stack">
            <TextField label={t('mqtt_base_topic')} help={t('mqtt_base_topic_help', { topic: m.status.root_topic || 'zoneflow/…' })} value={form.base_topic} maxLength={63} spellcheck={false} autocapitalize="off" placeholder={m.status.root_topic} onInput={(v) => set({ base_topic: v.trim() })} error={errors['base_topic']} disabled={ro} />
            <TextField label={t('mqtt_client_id')} help={t('mqtt_client_id_help')} value={form.client_id} maxLength={63} spellcheck={false} autocapitalize="off" onInput={(v) => set({ client_id: v.trim() })} error={errors['client_id']} disabled={ro} />
          </div>
        </Card>
        <Card title={t('mqtt_ha')}>
          <div class="stack">
            <Switch label={t('mqtt_discovery')} help={t('mqtt_discovery_help')} checked={form.discovery} onChange={(v) => set({ discovery: v })} disabled={ro} />
            {form.discovery && (
              <>
                <Choice
                  label={t('mqtt_entity')}
                  value={form.ha_zone_entity}
                  onChange={(v) => set({ ha_zone_entity: v })}
                  options={[
                    { value: 'valve', label: t('mqtt_entity_valve') },
                    { value: 'switch', label: t('mqtt_entity_switch') },
                  ]}
                />
                <p class="field-help">{form.ha_zone_entity === 'valve' ? t('mqtt_entity_valve_help') : t('mqtt_entity_switch_help')}</p>
                <TextField label={t('mqtt_discovery_prefix')} value={form.discovery_prefix} maxLength={31} spellcheck={false} autocapitalize="off" onInput={(v) => set({ discovery_prefix: v.trim() })} error={errors['discovery_prefix']} disabled={ro} />
              </>
            )}
          </div>
        </Card>
        <div class="form-actions">
          <Button variant="primary" type="submit" loading={busy} disabled={ro || !(dirty || password !== '' || pem !== null)}>
            {t('save')}
          </Button>
        </div>
      </form>
    </>
  );
}

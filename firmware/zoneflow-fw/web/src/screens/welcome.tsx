// First run: admin password with the setup code from the display, then time
// zone, then zone count and names. Also the sign-in page when reads are
// protected.
import { useState } from 'preact/hooks';
import { ApiError, call, setCsrf } from '../api/client.ts';
import { Icon } from '../components/icons.tsx';
import { LoginForm } from '../components/shell.tsx';
import { Banner, Button, Card, PasswordField, Stepper, TextField } from '../components/ui.tsx';
import { t } from '../i18n/index.ts';
import { guessZone, zoneForPosix } from '../lib/timezones.ts';
import { navigate } from '../router.ts';
import { loadConfig } from '../state/actions.ts';
import { errorText } from '../state/errors.ts';
import { auth, config, onboarding, toast } from '../state/store.ts';
import { TimeZoneForm } from './settings.tsx';

const CODE_RE = /^[2-9A-HJ-NP-Z]{10}$/;

function Frame({ step, total, title, intro, children }: { step?: number; total?: number; title: string; intro?: string; children: preact.ComponentChildren }) {
  return (
    <div class="gate">
      <div class="gate-inner">
        <p class="gate-brand">
          <Icon name="drop" class="brand-icon" /> {t('app_name')}
        </p>
        {step && <p class="muted small">{t('step_of', { n: step, total: total ?? 3 })}</p>}
        <h1 tabIndex={-1}>{title}</h1>
        {intro && <p class="muted">{intro}</p>}
        <Card>{children}</Card>
        {step && step > 1 && <SkipSetup />}
      </div>
    </div>
  );
}

export function LoginGate() {
  return (
    <Frame title={t('login_title')} intro={t('login_intro')}>
      <LoginForm autoFocus />
    </Frame>
  );
}

function PasswordStep() {
  const [code, setCode] = useState('');
  const [pw, setPw] = useState('');
  const [pw2, setPw2] = useState('');
  const [err, setErr] = useState<Record<string, string>>({});
  const [busy, setBusy] = useState(false);
  const submit = async (e: Event) => {
    e.preventDefault();
    const c = code.replace(/[\s-]/g, '').toUpperCase();
    const errs: Record<string, string> = {};
    if (!CODE_RE.test(c)) errs['code'] = t('setup_code_bad');
    if (pw.length < 8) errs['pw'] = t('password_short');
    else if (pw.length > 64) errs['pw'] = t('password_long');
    if (pw !== pw2) errs['pw2'] = t('password_mismatch');
    setErr(errs);
    if (Object.keys(errs).length) return;
    setBusy(true);
    try {
      const s = await call('post', '/auth/setup', { body: { setup_code: c, password: pw } });
      setCsrf(s.csrf);
      onboarding.value = true;
      auth.value = await call('get', '/auth');
      await loadConfig();
    } catch (x) {
      if (x instanceof ApiError && x.code === 'bad_credentials') setErr({ code: t('setup_code_wrong') });
      else if (x instanceof ApiError && x.code === 'password_already_set') {
        toast(t('setup_already'), 'error');
        auth.value = await call('get', '/auth');
      } else setErr({ code: errorText(x) });
    }
    setBusy(false);
  };
  return (
    <Frame step={1} title={t('welcome_title')} intro={t('welcome_intro')}>
      <form class="stack" onSubmit={submit} noValidate>
        <TextField
          label={t('setup_code')}
          help={t('setup_code_help')}
          value={code}
          onInput={setCode}
          error={err['code']}
          autoComplete="off"
          autocapitalize="characters"
          spellcheck={false}
          maxLength={12}
          class="input code-input"
          autoFocus
        />
        <input type="text" name="username" autocomplete="username" value="admin" hidden readOnly />
        <PasswordField label={t('new_password')} help={t('new_password_help')} value={pw} onInput={setPw} error={err['pw']} autocomplete="new-password" />
        <PasswordField label={t('confirm_password')} value={pw2} onInput={setPw2} error={err['pw2']} autocomplete="new-password" />
        <Button variant="primary" type="submit" size="lg" block loading={busy}>
          {t('setup_submit')}
        </Button>
      </form>
    </Frame>
  );
}

function TimeStep({ onNext }: { onNext: () => void }) {
  const current = config.value?.timezone;
  const guess = guessZone();
  const initial = current && current !== 'UTC0' ? current : (guess?.[1] ?? 'UTC0');
  const [posix, setPosix] = useState(initial);
  const [choice, setChoice] = useState(zoneForPosix(initial) ?? 'custom');
  const [busy, setBusy] = useState(false);
  return (
    <Frame step={2} title={t('tz_step_title')} intro={t('tz_step_intro')}>
      <TimeZoneForm
        posix={posix}
        choice={choice}
        setPosix={setPosix}
        setChoice={setChoice}
        busy={busy}
        submitLabel={t('next')}
        onSubmit={async (e) => {
          e.preventDefault();
          setBusy(true);
          try {
            await call('put', '/time', { body: { timezone: posix } });
            await loadConfig();
            onNext();
          } catch (x) {
            toast(errorText(x), 'error');
          }
          setBusy(false);
        }}
      />
    </Frame>
  );
}

function ZonesStep({ onNext }: { onNext: () => void }) {
  const cfg = config.value;
  const [count, setCount] = useState(cfg?.zone_count ?? 8);
  const [names, setNames] = useState<string[]>(() =>
    Array.from({ length: 8 }, (_, i) => cfg?.zones[i]?.name ?? ''),
  );
  const [busy, setBusy] = useState(false);
  return (
    <Frame step={3} title={t('zones_step_title')} intro={t('zones_step_intro')}>
      <form
        class="stack"
        onSubmit={async (e) => {
          e.preventDefault();
          setBusy(true);
          try {
            const doc = await call('get', '/config');
            doc.zone_count = count;
            doc.zones.forEach((z, i) => (z.name = (names[i] ?? '').trim()));
            await call('put', '/config', { body: doc });
            await loadConfig();
            await markSetupComplete();
            onNext();
          } catch (x) {
            toast(errorText(x), 'error');
          }
          setBusy(false);
        }}
      >
        <Stepper label={t('zone_count')} value={count} min={1} max={8} onChange={setCount} />
        {Array.from({ length: count }, (_, i) => (
          <TextField
            key={i}
            label={t('zone_n', { n: i + 1 })}
            value={names[i] ?? ''}
            maxLength={23}
            placeholder={t('zone_n', { n: i + 1 })}
            onInput={(v) => {
              const n = [...names];
              n[i] = v;
              setNames(n);
            }}
          />
        ))}
        <Button variant="primary" type="submit" size="lg" block loading={busy}>
          {t('setup_finish')}
        </Button>
      </form>
    </Frame>
  );
}

/** Stores that the first-run steps are done, so they do not come back on the next visit. */
async function markSetupComplete(): Promise<void> {
  try {
    await call('put', '/setup', { body: { complete: true } });
    if (auth.value) auth.value = { ...auth.value, setup_complete: true };
  } catch (x) {
    toast(errorText(x), 'error');
  }
}

function SkipSetup() {
  return (
    <button
      type="button"
      class="btn btn-ghost skip-setup"
      onClick={async () => {
        await markSetupComplete();
        onboarding.value = false;
        navigate('/', { replace: true });
      }}
    >
      {t('setup_skip')}
    </button>
  );
}

function DoneStep() {
  const finish = (to: string) => {
    onboarding.value = false;
    navigate(to, { replace: true });
  };
  return (
    <Frame title={t('setup_done_title')} intro={t('setup_done_intro')}>
      <div class="stack">
        <Banner severity="success" title={t('setup_done_title')} />
        <Button variant="primary" size="lg" block icon="calendar" onClick={() => finish('/programs/new')}>
          {t('setup_create_program')}
        </Button>
        <Button block onClick={() => finish('/')}>
          {t('setup_go_home')}
        </Button>
      </div>
    </Frame>
  );
}

/**
 * Password first; then time zone and zones, until the controller records
 * setup_complete (finished or skipped), also on a later visit.
 */
export function Welcome() {
  const [step, setStep] = useState(2);
  if (!auth.value?.password_set) return <PasswordStep />;
  if (step === 2) return <TimeStep onNext={() => setStep(3)} />;
  if (step === 3) return <ZonesStep onNext={() => setStep(4)} />;
  return <DoneStep />;
}

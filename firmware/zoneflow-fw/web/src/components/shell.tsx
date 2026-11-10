// Page frame: header with connection state, navigation (bottom bar on
// phones, side rail on wide screens), the "watering now" bar with its stop
// button on every page, toasts, and the sign-in sheet.
import type { ComponentChildren } from 'preact';
import { useEffect, useState } from 'preact/hooks';
import { call } from '../api/client.ts';
import { t } from '../i18n/index.ts';
import { formatCountdown, formatTime } from '../lib/format.ts';
import { path, onLinkClick } from '../router.ts';
import { stopAll, loadConfig } from '../state/actions.ts';
import { errorText } from '../state/errors.ts';
import { reconnectNow } from '../state/live.ts';
import { auth, config, conn, dismissToast, elapsedSec, loginOpen, signedIn, status, timeZone, toasts, zoneLabel } from '../state/store.ts';
import { setCsrf } from '../api/client.ts';
import { Icon, type IconName } from './icons.tsx';
import { Banner, Button, ConfirmHost, PasswordField, Sheet } from './ui.tsx';

const NAV: { href: string; label: () => string; icon: IconName; match: (p: string) => boolean }[] = [
  { href: '/', label: () => t('nav_home'), icon: 'home', match: (p) => p === '/' },
  { href: '/run', label: () => t('nav_run'), icon: 'play', match: (p) => p.startsWith('/run') },
  { href: '/programs', label: () => t('nav_programs'), icon: 'calendar', match: (p) => p.startsWith('/programs') },
  { href: '/zones', label: () => t('nav_zones'), icon: 'zones', match: (p) => p.startsWith('/zones') },
  {
    href: '/more',
    label: () => t('nav_more'),
    icon: 'settings',
    match: (p) => ['/more', '/settings', '/rain-delay', '/activity', '/about'].some((x) => p.startsWith(x)),
  },
];

function ConnPill() {
  const c = conn.value;
  const label =
    c === 'live' ? t('conn_live') : c === 'connecting' ? t('conn_connecting') : c === 'paused' ? t('conn_paused') : t('conn_lost');
  return (
    <span class={`conn conn-${c}`} role="status">
      <span class="conn-dot" aria-hidden="true" />
      {label}
    </span>
  );
}

export function ConnectionBanner() {
  const c = conn.value;
  if (c === 'live' || c === 'connecting' || c === 'paused') return null;
  const s = status.value;
  const when = s?.utc ? formatTime(s.utc, timeZone.value) : '';
  return (
    <div class="conn-banner">
      <Banner
        severity="error"
        icon="wifi"
        title={t('conn_lost')}
        action={
          <Button size="sm" onClick={reconnectNow}>
            {t('conn_retry')}
          </Button>
        }
      >
        {c === 'busy' ? t('conn_busy') : s ? t('conn_lost_detail', { time: when }) : t('conn_lost_never')}
      </Banner>
    </div>
  );
}

/** Fixed bar with what is watering and a Stop button, on every page. */
function RunningBar() {
  const s = status.value;
  const [homeCardVisible, setHomeCardVisible] = useState(false);
  useEffect(() => {
    const el = document.getElementById('now-card');
    if (!el || typeof IntersectionObserver === 'undefined') {
      setHomeCardVisible(false);
      return;
    }
    const io = new IntersectionObserver(([e]) => setHomeCardVisible(!!e?.isIntersecting), { threshold: 0.3 });
    io.observe(el);
    return () => io.disconnect();
  }, [path.value, s?.queue?.state]);
  const q = s?.queue;
  if (!s || !q || q.state === 'idle') return null;
  if (path.value === '/' && homeCardVisible) return null;
  const open = s.zones?.find((z) => z.state === 'open' || z.state === 'waiting');
  const head = q.items?.[0];
  const zoneId = open?.id ?? head?.zone ?? null;
  const left = Math.max(0, (open?.remaining_s ?? q.current_remaining_s ?? 0) - elapsedSec());
  return (
    <div class={`running-bar ${conn.value !== 'live' ? 'is-stale' : ''}`} role="region" aria-label={t('now_running')}>
      <Icon name="drop" class="running-icon" />
      <div class="running-text">
        <strong>{q.state === 'paused' ? t('now_paused') : zoneLabel(zoneId)}</strong>
        {q.state !== 'paused' && <span class="tabular">{t('clock_left', { time: formatCountdown(left) })}</span>}
      </div>
      <Button variant="danger" icon="stop" onClick={() => void stopAll()}>
        {t('stop_all')}
      </Button>
    </div>
  );
}

function Toasts() {
  return (
    <div class="toasts" aria-live="polite" aria-atomic="false">
      {toasts.value.map((x) => (
        <div key={x.id} class={`toast toast-${x.kind}`} role={x.kind === 'error' ? 'alert' : 'status'}>
          <span>{x.text}</span>
          <button type="button" class="icon-btn" aria-label={t('close')} onClick={() => dismissToast(x.id)}>
            <Icon name="close" size={18} />
          </button>
        </div>
      ))}
    </div>
  );
}

/** Sign-in form, used as the full-page gate and in the sheet. */
export function LoginForm({ onDone, autoFocus }: { onDone?: () => void; autoFocus?: boolean }) {
  const [pw, setPw] = useState('');
  const [err, setErr] = useState<string | null>(null);
  const [busy, setBusy] = useState(false);
  const submit = async (e: Event) => {
    e.preventDefault();
    setBusy(true);
    setErr(null);
    try {
      const s = await call('post', '/auth/login', { body: { password: pw } });
      setCsrf(s.csrf);
      auth.value = await call('get', '/auth');
      if (!config.value) void loadConfig();
      setPw('');
      onDone?.();
    } catch (x) {
      const code = (x as { code?: string }).code;
      setErr(code === 'bad_credentials' ? t('login_bad') : errorText(x));
    } finally {
      setBusy(false);
    }
  };
  return (
    <form onSubmit={submit} class="stack">
      <input type="text" name="username" autocomplete="username" value="admin" hidden readOnly />
      <PasswordField label={t('login_password')} value={pw} onInput={setPw} error={err} autocomplete="current-password" autoFocus={autoFocus} name="password" />
      <Button variant="primary" type="submit" block size="lg" loading={busy} disabled={!pw}>
        {t('login_submit')}
      </Button>
    </form>
  );
}

function LoginSheet() {
  return (
    <Sheet open={loginOpen.value} onClose={() => (loginOpen.value = false)} title={t('login_title')}>
      <p class="muted">{t('login_intro')}</p>
      <LoginForm autoFocus onDone={() => (loginOpen.value = false)} />
    </Sheet>
  );
}

export function Shell({ children }: { children: ComponentChildren }) {
  const p = path.value;
  const name = config.value?.device_name || t('app_name');
  const stale = conn.value !== 'live' && conn.value !== 'connecting';
  return (
    <div class="shell">
      <a class="skip-link" href="#main">
        {t('skip_to_content')}
      </a>
      <header class="topbar">
        <a href="/" onClick={onLinkClick} class="brand">
          <Icon name="drop" class="brand-icon" />
          <span class="brand-name">{name}</span>
        </a>
        <ConnPill />
        {!signedIn.value && auth.value?.password_set && (
          <Button size="sm" variant="tonal" icon="lock" onClick={() => (loginOpen.value = true)}>
            {t('login_submit')}
          </Button>
        )}
      </header>
      <nav class="nav" aria-label={t('nav_main')}>
        {NAV.map((n) => {
          const active = n.match(p);
          return (
            <a key={n.href} href={n.href} onClick={onLinkClick} class={active ? 'nav-item is-active' : 'nav-item'} aria-current={active ? 'page' : undefined}>
              <span class="nav-icon">
                <Icon name={n.icon} />
              </span>
              <span class="nav-label">{n.label()}</span>
            </a>
          );
        })}
      </nav>
      <main id="main" class={stale ? 'main is-stale' : 'main'}>
        <ConnectionBanner />
        {children}
      </main>
      <RunningBar />
      <Toasts />
      <LoginSheet />
      <ConfirmHost />
    </div>
  );
}

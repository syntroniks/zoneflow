import { useEffect } from 'preact/hooks';
import { call, setCsrf, setUnauthorizedHandler } from './api/client.ts';
import { Shell } from './components/shell.tsx';
import { Spinner } from './components/ui.tsx';
import { t } from './i18n/index.ts';
import { match, path } from './router.ts';
import { loadConfig, refreshAuth } from './state/actions.ts';
import { startLive } from './state/live.ts';
import { auth, onboarding } from './state/store.ts';
import { AboutScreen } from './screens/about.tsx';
import { ActivityScreen } from './screens/activity.tsx';
import { HomeScreen } from './screens/home.tsx';
import { MoreScreen } from './screens/more.tsx';
import { ProgramEditScreen, ProgramsScreen } from './screens/programs.tsx';
import { RainScreen } from './screens/rain.tsx';
import { RunScreen } from './screens/run.tsx';
import { SettingsScreen } from './screens/settings.tsx';
import { LoginGate, Welcome } from './screens/welcome.tsx';
import { ZoneDetailScreen, ZonesScreen } from './screens/zones.tsx';

function Routes() {
  const p = path.value;
  let m: Record<string, string> | null;
  if (p === '/') return <HomeScreen />;
  if (p === '/run') return <RunScreen />;
  if (p === '/zones') return <ZonesScreen />;
  if ((m = match('/zones/:id', p))) return <ZoneDetailScreen key={m['id']} id={Number(m['id'])} />;
  if (p === '/programs') return <ProgramsScreen />;
  if (p === '/programs/new') return <ProgramEditScreen key="new" id="new" />;
  if ((m = match('/programs/:id', p))) return <ProgramEditScreen key={m['id']} id={Number(m['id'])} />;
  if (p === '/rain-delay') return <RainScreen />;
  if (p === '/activity') return <ActivityScreen />;
  if (p === '/more' || p === '/settings') return <MoreScreen />;
  if ((m = match('/settings/:section', p))) return <SettingsScreen key={m['section']} section={m['section'] ?? ''} />;
  if (p === '/about') return <AboutScreen />;
  return <HomeScreen />;
}

export function App() {
  useEffect(() => {
    setUnauthorizedHandler(() => {
      setCsrf(null);
      void refreshAuth();
    });
    void (async () => {
      try {
        const a = await call('get', '/auth');
        setCsrf(a.csrf);
        auth.value = a;
      } catch {
        auth.value = { password_set: true, authenticated: false };
      }
      void loadConfig();
      startLive();
    })();
  }, []);

  const a = auth.value;
  if (!a) return <div class="gate"><Spinner label={t('loading')} /></div>;
  if (!a.password_set || onboarding.value) return <Welcome />;
  if (a.protect_reads && !a.authenticated) return <LoginGate />;
  return (
    <Shell>
      <Routes />
    </Shell>
  );
}

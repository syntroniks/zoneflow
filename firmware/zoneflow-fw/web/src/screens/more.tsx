// "More" menu: the less frequent screens and the settings sections.
import { Card, PageTitle, Row } from '../components/ui.tsx';
import { Icon, type IconName } from '../components/icons.tsx';
import { t } from '../i18n/index.ts';
import type { StringKey } from '../i18n/en.ts';
import { call } from '../api/client.ts';
import { auth, signedIn, status } from '../state/store.ts';
import { navigate } from '../router.ts';
import { setCsrf } from '../api/client.ts';
import { Button } from '../components/ui.tsx';

export const SETTINGS_SECTIONS: { slug: string; key: StringKey; icon: IconName }[] = [
  { slug: 'device', key: 'set_device', icon: 'pin' },
  { slug: 'time', key: 'set_time', icon: 'clock' },
  { slug: 'watering', key: 'set_watering', icon: 'drop' },
  { slug: 'sensors', key: 'set_sensors', icon: 'rain' },
  { slug: 'network', key: 'set_network', icon: 'wifi' },
  { slug: 'mqtt', key: 'set_mqtt', icon: 'flow' },
  { slug: 'security', key: 'set_security', icon: 'lock' },
  { slug: 'backup', key: 'set_backup', icon: 'download' },
  { slug: 'firmware', key: 'set_firmware', icon: 'upload' },
  { slug: 'system', key: 'set_system', icon: 'power' },
];

function MenuRow({ href, icon, label, value }: { href: string; icon: IconName; label: string; value?: string }) {
  return (
    <Row href={href} trailing={value ? <span class="row-value">{value}</span> : undefined}>
      <Icon name={icon} class="row-icon" />
      <span class="row-title">{label}</span>
    </Row>
  );
}

export function MoreScreen() {
  const rd = status.value?.rain_delay;
  return (
    <div class="page narrow">
      <PageTitle title={t('nav_more')} />
      <Card>
        <div class="rows">
          <MenuRow href="/rain-delay" icon="rain" label={t('nav_rain_delay')} value={rd?.active ? t('on') : t('off')} />
          <MenuRow href="/history" icon="activity" label={t('nav_activity')} />
        </div>
      </Card>
      <Card title={t('settings_title')}>
        <div class="rows">
          {SETTINGS_SECTIONS.map((s) => (
            <MenuRow key={s.slug} href={`/settings/${s.slug}`} icon={s.icon} label={t(s.key)} />
          ))}
        </div>
      </Card>
      <Card>
        <div class="rows">
          <MenuRow href="/about" icon="info" label={t('nav_about')} />
        </div>
      </Card>
      {signedIn.value && (
        <Button
          variant="ghost"
          icon="lock"
          onClick={async () => {
            try {
              await call('post', '/auth/logout');
            } catch {
              // signed out locally anyway
            }
            setCsrf(null);
            auth.value = await call('get', '/auth').catch(() => null);
            navigate('/');
          }}
        >
          {t('logout')}
        </Button>
      )}
    </div>
  );
}

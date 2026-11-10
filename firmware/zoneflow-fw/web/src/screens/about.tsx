// Device and firmware information.
import { useEffect, useState } from 'preact/hooks';
import { call, type Schemas } from '../api/client.ts';
import { Banner, Card, KeyValue, PageTitle, Spinner } from '../components/ui.tsx';
import { t } from '../i18n/index.ts';
import { formatDuration } from '../lib/format.ts';

declare const __UI_VERSION__: string;

export function AboutScreen() {
  const [info, setInfo] = useState<Schemas['Info'] | null>(null);
  const [failed, setFailed] = useState(false);
  useEffect(() => {
    call('get', '/info').then(setInfo, () => setFailed(true));
  }, []);
  return (
    <div class="page narrow">
      <PageTitle title={t('about_title')} back="/more" />
      <Banner severity="warning" title={t('about_untested')} />
      <Card>
        {failed ? (
          <p class="muted">{t('error_network')}</p>
        ) : !info ? (
          <Spinner label={t('loading')} />
        ) : (
          <KeyValue
            items={[
              [t('about_device'), info.device_name ?? '—'],
              [t('net_hostname'), info.hostname ? `${info.hostname}.local` : '—'],
              [t('about_board'), info.board ?? '—'],
              [t('fw_version'), info.firmware?.version ?? '—'],
              [t('fw_build'), info.firmware?.build_date ?? '—'],
              [t('about_idf'), info.firmware?.idf_version ?? '—'],
              [t('about_api'), info.api_version ?? '—'],
              [t('about_ui_version'), typeof __UI_VERSION__ === 'string' ? __UI_VERSION__ : 'dev'],
              [t('about_uptime'), info.uptime_s != null ? formatDuration(info.uptime_s) : '—'],
              [t('about_reset_reason'), info.reset_reason ?? '—'],
              [
                t('about_memory'),
                info.heap?.free != null ? t('about_memory_val', { free: Math.round(info.heap.free / 1024), min: Math.round((info.heap.min_free ?? 0) / 1024) }) : '—',
              ],
              [t('net_mac'), info.mac ?? '—'],
            ]}
          />
        )}
      </Card>
      <Card>
        <p>{t('about_licence')}</p>
        <p>
          <a class="link" href="https://syntroniks.github.io/zoneflow/" target="_blank" rel="noopener">
            {t('about_project')}
          </a>
        </p>
      </Card>
    </div>
  );
}

// Plain-language text for API errors and controller refusals.
import { ApiError } from '../api/client.ts';
import { t, tk } from '../i18n/index.ts';
import { formatDuration } from '../lib/format.ts';

export interface ErrorContext {
  zone?: string;
}

/** Text for a controller reason code (409 codes, command_rejected, ...). */
export function reasonText(code: string, ctx: ErrorContext = {}, retryAfter?: number): string {
  return tk(`err_${code}`, { zone: ctx.zone ?? '', time: formatDuration(retryAfter ?? 0) }, code.replace(/_/g, ' '));
}

export function errorText(e: unknown, ctx: ErrorContext = {}): string {
  if (!(e instanceof ApiError)) return t('error_generic', { message: String((e as Error)?.message ?? e) });
  if (e.status === 0) return t('error_network');
  if (e.status === 429) return t('error_rate_limited', { time: formatDuration(e.retryAfter ?? 1) });
  if (e.status === 503) return t('error_busy');
  if (e.code === 'csrf' || e.code === 'unauthorized') return t('error_csrf');
  const known = tk(`err_${e.code}`, { zone: ctx.zone ?? '', time: formatDuration(e.retryAfter ?? 0) }, '');
  if (known) return known;
  if (e.path) return t('error_field', { field: e.path, message: e.message });
  return t('error_generic', { message: e.message });
}

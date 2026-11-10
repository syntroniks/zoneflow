import { expect, type APIRequestContext, type Page } from '@playwright/test';

/** 2026-06-15 05:12 UTC is a Monday, 07:12 in Berlin (the demo time zone). */
export const DEMO_UTC = 1781500320;

export async function mock(request: APIRequestContext, hook: string, body: object = {}): Promise<void> {
  const r = await request.post(`/__mock/${hook}`, { data: body });
  expect(r.ok(), `mock hook ${hook}`).toBeTruthy();
}

export async function reset(request: APIRequestContext, opts: { scenario?: 'demo' | 'fresh'; utc?: number; frozen?: boolean } = {}) {
  await mock(request, 'reset', { scenario: 'demo', utc: DEMO_UTC, ...opts });
}

/** Signs in through the UI's sheet (demo password). */
export async function signIn(page: Page): Promise<void> {
  await page.getByRole('banner').getByRole('button', { name: 'Sign in' }).click();
  await page.getByLabel('Admin password').fill('zoneflow');
  await page.getByRole('dialog').getByRole('button', { name: 'Sign in' }).click();
  await expect(page.getByRole('dialog')).toBeHidden();
}

export async function openSignedIn(page: Page, path = '/'): Promise<void> {
  await page.goto(path);
  await expect(page.locator('.conn-live')).toBeVisible();
  await signIn(page);
}

/** Collects CSP violations and page errors; call check() at the end. */
export function watchErrors(page: Page): { check: () => void } {
  const errors: string[] = [];
  page.on('pageerror', (e) => errors.push(`pageerror: ${e.message}`));
  page.on('console', (m) => {
    if (m.type() === 'error' && /Content Security Policy|Refused to/.test(m.text())) errors.push(m.text());
  });
  return { check: () => expect(errors).toEqual([]) };
}

/** In-app navigation (no reload), so the session's event log survives. */
export async function go(page: Page, path: string): Promise<void> {
  await page.evaluate((p) => {
    history.pushState(null, '', p);
    dispatchEvent(new PopStateEvent('popstate'));
  }, path);
  await expect(page).toHaveURL(new RegExp(`${path.replace(/[/]/g, '\\/')}$`));
}

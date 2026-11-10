// Automated accessibility check (axe-core, WCAG 2.1 A and AA rules,
// colour contrast included) of every screen, in light and dark.
import AxeBuilder from '@axe-core/playwright';
import { expect, test } from '@playwright/test';
import { go, mock, openSignedIn, reset } from './helpers.ts';

const PATHS = ['/', '/run', '/zones', '/zones/4', '/programs', '/programs/2', '/rain-delay', '/history', '/more',
  '/settings/time', '/settings/watering', '/settings/sensors', '/settings/network', '/settings/mqtt', '/settings/device', '/settings/security',
  '/settings/backup', '/settings/firmware', '/settings/system', '/about'];

for (const scheme of ['light', 'dark'] as const) {
  test(`axe: every screen, ${scheme}`, async ({ page, request }) => {
    test.setTimeout(90_000);
    await page.emulateMedia({ colorScheme: scheme, reducedMotion: 'reduce' });
    await reset(request);
    await openSignedIn(page);
    await mock(request, 'run', { program: 2 });
    await mock(request, 'fault', { code: 'zone_open_circuit', zone: 4 });
    await mock(request, 'set', { rest_zone: 1, rest_s: 600, rain: true });
    const problems: string[] = [];
    for (const p of PATHS) {
      await go(page, p);
      await page.waitForTimeout(200);
      const r = await new AxeBuilder({ page }).withTags(['wcag2a', 'wcag2aa', 'wcag21a', 'wcag21aa']).analyze();
      for (const v of r.violations) problems.push(`${p}: ${v.id} (${v.nodes.length}) ${v.nodes[0]?.target.join(' ')} ${v.nodes[0]?.failureSummary?.split('\n')[1] ?? ''}`);
    }
    expect(problems).toEqual([]);
  });
}

test('axe: first-run page', async ({ page, request }) => {
  await reset(request, { scenario: 'fresh' });
  await page.goto('/');
  await expect(page.getByRole('heading', { name: 'Set up your controller' })).toBeVisible();
  const r = await new AxeBuilder({ page }).withTags(['wcag2a', 'wcag2aa', 'wcag21aa']).analyze();
  expect(r.violations.map((v) => v.id)).toEqual([]);
});

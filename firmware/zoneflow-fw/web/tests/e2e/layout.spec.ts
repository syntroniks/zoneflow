// Every screen at 320 px wide: nothing scrolls sideways.
import { expect, test } from '@playwright/test';
import { go, mock, openSignedIn, reset } from './helpers.ts';

const PATHS = ['/', '/run', '/zones', '/zones/4', '/programs', '/programs/new', '/programs/2', '/rain-delay', '/activity', '/more',
  '/settings/device', '/settings/time', '/settings/watering', '/settings/sensors', '/settings/network', '/settings/security',
  '/settings/backup', '/settings/firmware', '/settings/system', '/about'];

test('no horizontal scrolling on any screen', async ({ page, request }) => {
  test.setTimeout(60_000);
  await reset(request);
  await openSignedIn(page);
  await mock(request, 'run', { program: 2 });
  await mock(request, 'fault', { code: 'zone_open_circuit', zone: 4 });
  for (const p of PATHS) {
    await go(page, p);
    await page.waitForTimeout(150);
    const over = await page.evaluate(() => document.documentElement.scrollWidth - document.documentElement.clientWidth);
    expect(over, `${p} overflows by ${over}px`).toBeLessThanOrEqual(0);
  }
});

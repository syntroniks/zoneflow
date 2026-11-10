// Documentation screenshots of every screen, light and dark, phone and
// desktop: docs/firmware/img/webui/<theme>-<size>-<name>.png.
//   npm run build && npx playwright test --project=screenshots
import { expect, test, type Page } from '@playwright/test';
import { DEMO_UTC, go, mock, reset, signIn } from './helpers.ts';

const OUT = '../../../docs/firmware/img/webui';
const SIZES = { phone: { width: 390, height: 844 }, desktop: { width: 1280, height: 860 } } as const;
const ONLY = process.env['SHOTS'];

for (const scheme of ['light', 'dark'] as const) {
  for (const [size, viewport] of Object.entries(SIZES)) {
    test.describe(`${scheme} ${size}`, () => {
      test.use({ colorScheme: scheme, viewport, deviceScaleFactor: 1, isMobile: size === 'phone', hasTouch: size === 'phone' });

      const shot = async (page: Page, name: string, full = false) => {
        if (ONLY && !name.includes(ONLY)) return;
        await page.waitForTimeout(250);
        // Full-page captures only on desktop, by growing the viewport to the
        // page so fixed and sticky bars sit where they end up when scrolled.
        const path = `${OUT}/${scheme}-${size}-${name}.png`;
        if (full && size === 'desktop') {
          const h = await page.evaluate(() => document.documentElement.scrollHeight);
          await page.setViewportSize({ width: viewport.width, height: Math.max(viewport.height, h) });
          await page.waitForTimeout(150);
          await page.screenshot({ path, animations: 'disabled', caret: 'hide' });
          await page.setViewportSize(viewport);
        } else {
          await page.screenshot({ path, animations: 'disabled', caret: 'hide' });
        }
      };

      test('first run', async ({ page, request }) => {
        await reset(request, { scenario: 'fresh', frozen: true });
        await page.goto('/');
        await expect(page.getByRole('heading', { name: 'Set up your controller' })).toBeVisible();
        await shot(page, 'setup');
      });

      test('screens', async ({ page, request }) => {
        test.setTimeout(120_000);
        await reset(request, { frozen: true, utc: DEMO_UTC });
        await page.goto('/');
        await expect(page.locator('.conn-live')).toBeVisible();
        await shot(page, 'home-idle');
        await signIn(page);

        await mock(request, 'run', { program: 2, elapsed_s: 260 });
        await mock(request, 'set', { rest_zone: 1, rest_s: 1260 });
        await page.waitForTimeout(700);
        await shot(page, 'home-running', true);

        await mock(request, 'fault', { code: 'zone_open_circuit', zone: 4 });
        await mock(request, 'set', { rain: true });
        await page.waitForTimeout(700);
        await shot(page, 'home-faults', true);
        await go(page, '/activity');
        await shot(page, 'activity');

        await go(page, '/run');
        await page.getByRole('radio', { name: /Back lawn/ }).check();
        await shot(page, 'run');

        await go(page, '/');
        await page.locator('.tile').first().click();
        await expect(page.getByRole('dialog')).toBeVisible();
        await shot(page, 'quick-run');
        await page.keyboard.press('Escape');

        await go(page, '/zones');
        await shot(page, 'zones');
        await go(page, '/zones/4');
        await shot(page, 'zone-detail', true);

        await go(page, '/programs');
        await shot(page, 'programs');
        await go(page, '/programs/2');
        await expect(page.getByRole('heading', { name: 'Next 7 days' })).toBeVisible();
        await shot(page, 'program-edit', true);

        await go(page, '/rain-delay');
        await shot(page, 'rain-delay');
        await go(page, '/more');
        await shot(page, 'more', true);
        for (const s of ['time', 'watering', 'network', 'security', 'backup', 'firmware', 'system']) {
          await go(page, `/settings/${s}`);
          await page.waitForTimeout(200);
          await shot(page, `settings-${s}`, true);
        }
        await page.getByRole('button', { name: 'Factory reset' }).click();
        await shot(page, 'confirm-reset');
        await page.keyboard.press('Escape');
        await go(page, '/about');
        await shot(page, 'about');

        await go(page, '/');
        await mock(request, 'set', { mute: true });
        await mock(request, 'drop-streams');
        await expect(page.locator('.conn-lost')).toBeVisible({ timeout: 15_000 });
        await shot(page, 'connection-lost');
      });
    });
  }
}

// Main flows against the mock controller, on a phone and a desktop viewport.
import { expect, test } from '@playwright/test';
import { DEMO_UTC, go, mock, openSignedIn, reset, signIn, watchErrors } from './helpers.ts';

test.beforeEach(async ({ request }) => {
  await reset(request);
});

test('first run: setup code, password, time zone, zones', async ({ page, request }) => {
  const errs = watchErrors(page);
  await reset(request, { scenario: 'fresh' });
  await page.goto('/');
  await expect(page.getByRole('heading', { name: 'Set up your controller' })).toBeVisible();
  await page.getByLabel('Setup code').fill('ABC');
  await page.getByLabel('New admin password', { exact: true }).fill('short');
  await page.getByRole('button', { name: 'Set password' }).click();
  await expect(page.getByText('The setup code has 10 characters')).toBeVisible();
  await expect(page.getByText('Use at least 8 characters.')).toBeVisible();

  await page.getByLabel('Setup code').fill('K7Q2-M9XW-4R'.toLowerCase().replace(/-/g, ' '));
  await page.getByLabel('New admin password', { exact: true }).fill('garden hose 42');
  await page.getByLabel('Repeat the password').fill('garden hose 42');
  await page.getByRole('button', { name: 'Set password' }).click();

  await expect(page.getByRole('heading', { name: 'Where is the controller?' })).toBeVisible();
  // The browser runs in Europe/Berlin, which is suggested.
  await expect(page.getByLabel('Time zone')).toHaveValue('Europe/Berlin');
  await page.getByRole('button', { name: 'Next' }).click();

  await expect(page.getByRole('heading', { name: 'Your zones' })).toBeVisible();
  await page.getByRole('button', { name: 'Decrease Zones in use' }).click();
  for (let i = 0; i < 3; i++) await page.getByRole('button', { name: 'Decrease Zones in use' }).click();
  await expect(page.getByLabel('Zones in use', { exact: true })).toHaveValue('4');
  await page.getByLabel('Zone 1').fill('Lawn');
  await page.getByLabel('Zone 2').fill('Beds');
  await page.getByRole('button', { name: 'Finish' }).click();
  await page.getByRole('button', { name: 'Go to the dashboard' }).click();

  await expect(page.locator('.tile')).toHaveCount(4);
  await expect(page.locator('.tile').first()).toContainText('Lawn');
  const cfg = await (await request.get('/api/v1/config')).json();
  expect(cfg.timezone).toBe('CET-1CEST,M3.5.0,M10.5.0/3');
  expect(cfg.zone_count).toBe(4);
  errs.check();
});

test('read-only visitor sees status and is asked to sign in before a change', async ({ page }) => {
  const errs = watchErrors(page);
  await page.goto('/');
  await expect(page.locator('.conn-live')).toBeVisible();
  await expect(page.getByText('Nothing is watering')).toBeVisible();
  await page.locator('.tile', { hasText: 'Back lawn' }).click();
  await page.getByRole('button', { name: /Start Back lawn/ }).click();
  await expect(page.getByRole('heading', { name: 'Sign in' })).toBeVisible();
  await page.getByLabel('Admin password').fill('wrong password');
  await page.getByRole('dialog', { name: 'Sign in' }).getByRole('button', { name: 'Sign in' }).click();
  await expect(page.getByText('Wrong password.')).toBeVisible();
  await page.getByLabel('Admin password').fill('zoneflow');
  await page.getByRole('dialog', { name: 'Sign in' }).getByRole('button', { name: 'Sign in' }).click();
  await expect(page.getByRole('dialog', { name: 'Sign in' })).toBeHidden();
  errs.check();
});

test('manual run in three taps, then stop from another page', async ({ page }) => {
  const errs = watchErrors(page);
  await openSignedIn(page);
  // 1: Run tab, 2: zone, 3: start (the zone's default run time is preselected: 20 min in the demo).
  await page.getByRole('navigation').getByRole('link', { name: 'Run' }).click();
  await page.getByRole('radio', { name: /Back lawn/ }).check();
  await page.getByRole('button', { name: /Start Back lawn for 20 min/ }).click();
  await expect(page).toHaveURL(/\/$/);
  const now = page.locator('#now-card');
  await expect(now).toContainText('Back lawn');
  await expect(now).toContainText('Watering');
  await expect(now.locator('.now-clock')).toContainText(/^(20:00|19:5\d)/);

  // The stop button follows to every page.
  await page.getByRole('navigation').getByRole('link', { name: 'Programs' }).click();
  const bar = page.getByRole('region', { name: 'Watering' });
  await expect(bar).toContainText('Back lawn');
  await bar.getByRole('button', { name: 'Stop all' }).click();
  await expect(bar).toBeHidden();
  await page.getByRole('navigation').getByRole('link', { name: 'Home' }).click();
  await expect(page.getByText('Nothing is watering')).toBeVisible();
  errs.check();
});

test('pause, resume and skip the queue', async ({ page, request }) => {
  await openSignedIn(page);
  await mock(request, 'run', { program: 2 });
  const now = page.locator('#now-card');
  await expect(now).toContainText('Vegetable beds');
  await now.getByRole('button', { name: 'Pause' }).click();
  await expect(now).toContainText('Paused');
  await expect(now).toContainText(/stops by itself in (2 h|1 h 59 min)/);
  await now.getByRole('button', { name: 'Resume' }).click();
  await expect(now).toContainText('Watering');
  await now.getByRole('button', { name: 'Skip to next' }).click();
  await expect(now).toContainText('Roses along north');
});

test('a resting zone is refused with a clear reason', async ({ page, request }) => {
  await openSignedIn(page);
  await mock(request, 'set', { rest_zone: 2, rest_s: 600 });
  await go(page, '/zones/2');
  await expect(page.getByText('Back lawn is resting')).toBeVisible();
  // The test buttons are disabled; the run screen still offers the zone,
  // and the controller's 409 is explained.
  await go(page, '/run');
  await page.getByRole('radio', { name: /Back lawn/ }).check();
  await page.getByRole('button', { name: /Start Back lawn/ }).click();
  await expect(page.getByRole('alert').filter({ hasText: 'Back lawn is resting after its maximum run time. Try again in 10 min.' })).toBeVisible();
});

test('a late refusal arrives on the stream as command_rejected', async ({ page, request }) => {
  await openSignedIn(page);
  await mock(request, 'set', { verdict: 'reject:queue_full' });
  await page.locator('.tile', { hasText: 'Back lawn' }).click();
  await page.getByRole('button', { name: /Start Back lawn/ }).click();
  await expect(page.getByText('Sent. Waiting for the controller…')).toBeVisible();
  await expect(page.getByRole('alert').filter({ hasText: 'The controller refused: The queue is full.' })).toBeVisible();
});

test('faults are explained and cleared after confirmation', async ({ page, request }) => {
  await openSignedIn(page);
  await mock(request, 'fault', { code: 'zone_open_circuit', zone: 4 });
  const alert = page.locator('.banner', { hasText: 'Roses along north fence: no current' });
  await expect(alert).toContainText('Check the wire');
  await page.getByRole('button', { name: 'Clear faults' }).click();
  const dialog = page.getByRole('dialog', { name: 'Clear faults' });
  await dialog.getByRole('button', { name: 'Cancel' }).click();
  await expect(alert).toBeVisible();
  await page.getByRole('button', { name: 'Clear faults' }).click();
  await dialog.getByRole('button', { name: 'Clear faults' }).click();
  await expect(alert).toBeHidden();
});

test('program editor: create, preview, save, delete with confirmation', async ({ page, request }) => {
  const errs = watchErrors(page);
  await openSignedIn(page, '/programs');
  await page.getByRole('link', { name: 'New program' }).click();
  await page.getByLabel('Name').fill('Evening');
  // The start-time control follows the clock-format setting (24-hour for en-GB).
  await page.getByLabel('Start times 1: hour').selectOption('20');
  await page.getByLabel('Start times 1: minute').selectOption('15');
  // Weekdays: only Tuesday and Thursday.
  for (const d of ['Mon', 'Wed', 'Fri']) await page.locator('.weekdays label', { hasText: d }).click();
  await page.locator('.weekdays label', { hasText: 'Tue' }).click();
  await page.locator('.weekdays label', { hasText: 'Thu' }).click();
  const preview = page.locator('.preview');
  // Demo time is Monday 15 June 07:12 in Berlin: next runs Tue 16 and Thu 18.
  await expect(preview.locator('.run-chip.is-mine')).toHaveCount(2);
  await expect(preview.locator('li', { hasText: 'Tomorrow' }).locator('.is-mine')).toContainText('20:15');
  await page.getByRole('button', { name: 'Increase Front lawn' }).click();
  await page.getByRole('button', { name: 'Save' }).click();
  await expect(page.getByText('Program saved')).toBeVisible();
  const card = page.locator('.program', { hasText: 'Evening' });
  await expect(card).toContainText('Tue, Thu · 20:15');

  const progs = await (await request.get('/api/v1/programs')).json();
  const ev = progs.find((p: { name: string }) => p.name === 'Evening');
  expect(ev.days).toEqual({ type: 'weekdays', weekdays: ['tue', 'thu'] });
  expect(ev.durations_s.slice(0, 3)).toEqual([660, 600, 600]);

  await card.locator('.program-main').click();
  await page.getByRole('button', { name: 'Delete program' }).click();
  await page.getByRole('dialog').getByRole('button', { name: 'Cancel' }).click();
  await expect(page.getByLabel('Name')).toHaveValue('Evening');
  await page.getByRole('button', { name: 'Delete program' }).click();
  await page.getByRole('dialog').getByRole('button', { name: 'Delete' }).click();
  await expect(page.getByText('Program deleted')).toBeVisible();
  await expect(page.locator('.program', { hasText: 'Evening' })).toHaveCount(0);
  errs.check();
});

test('rain delay: set for a day, then end it after confirming', async ({ page }) => {
  await openSignedIn(page, '/rain-delay');
  await page.getByRole('radio', { name: '2 days' }).check();
  await page.getByRole('button', { name: 'Start delay' }).click();
  await expect(page.getByText(/Until Wed 17 Jun/)).toBeVisible();
  await page.getByRole('button', { name: 'End delay now' }).click();
  await page.getByRole('dialog').getByRole('button', { name: 'End delay now' }).click();
  await expect(page.getByText('No rain delay')).toBeVisible();
});

test('connection loss is shown and recovers', async ({ page, request }) => {
  await page.goto('/');
  await expect(page.locator('.conn-live')).toBeVisible();
  await mock(request, 'set', { mute: true });
  await mock(request, 'drop-streams');
  await expect(page.getByText('Connection lost').first()).toBeVisible();
  await expect(page.getByText(/Showing the state from/)).toBeVisible();
  await expect(page.locator('.conn-live')).toHaveCount(0);
  await mock(request, 'set', { mute: false });
  await page.getByRole('button', { name: 'Retry now' }).click();
  await expect(page.locator('.conn-live')).toBeVisible();
});

test('a restart signs the browser out; the next change asks to sign in', async ({ page, request }) => {
  await openSignedIn(page, '/settings/system');
  await page.getByRole('button', { name: 'Restart controller' }).click();
  await page.getByRole('dialog').getByRole('button', { name: 'Restart controller' }).click();
  await expect(page.getByText('Restarting… this page reconnects by itself.')).toBeVisible();
  await expect(page.locator('.conn-live')).toBeVisible({ timeout: 20_000 });
  await expect(page.getByRole('banner').getByRole('button', { name: 'Sign in' })).toBeVisible({ timeout: 10_000 });
  void request;
});

test('factory reset needs the typed word', async ({ page }) => {
  await openSignedIn(page, '/settings/system');
  await page.getByRole('button', { name: 'Factory reset' }).click();
  const dialog = page.getByRole('dialog', { name: 'Factory reset' });
  const go = dialog.getByRole('button', { name: 'Erase everything' });
  await expect(go).toBeDisabled();
  await dialog.getByLabel('Type RESET').fill('reset');
  await expect(go).toBeEnabled();
  await dialog.getByRole('button', { name: 'Cancel' }).click();
  await expect(dialog).toBeHidden();
});

test('firmware upload warns about running watering and shows progress', async ({ page, request }) => {
  await openSignedIn(page, '/settings/firmware');
  await mock(request, 'run', { zone: 1, seconds: 600 });
  await expect(page.getByText('Watering is running').first()).toBeVisible();
  const image = Buffer.alloc(300_000, 1);
  image[0] = 0xe9;
  await page.locator('input[type=file]').first().setInputFiles({ name: 'zoneflow-fw.bin', mimeType: 'application/octet-stream', buffer: image });
  await page.getByRole('dialog').getByRole('button', { name: 'Confirm' }).click();
  const force = page.getByRole('dialog', { name: 'Watering is running' });
  await expect(force).toContainText('stops the current watering');
  await force.getByRole('button', { name: 'Stop watering and update' }).click();
  await expect(page.getByText(/Writing update… \d+ %|Uploading… \d+ %/)).toBeVisible();
  await expect(page.getByText(/Restarting into 0\.3\.1/)).toBeVisible({ timeout: 20_000 });
});

test('backup downloads a JSON file and restore asks first', async ({ page }) => {
  await openSignedIn(page, '/settings/backup');
  const [dl] = await Promise.all([page.waitForEvent('download'), page.getByRole('button', { name: 'Download backup' }).click()]);
  expect(dl.suggestedFilename()).toMatch(/^zoneflow-backup-zoneflow-ab12-2026-06-15\.json$/);
  const body = JSON.parse(await (await dl.createReadStream()).toArray().then((c) => Buffer.concat(c).toString()));
  expect(body.format).toBe('zoneflow-backup');
  body.config.device_name = 'Restored';
  await page.locator('input[type=file]').setInputFiles({ name: 'b.json', mimeType: 'application/json', buffer: Buffer.from(JSON.stringify(body)) });
  await expect(page.getByRole('dialog')).toContainText('zoneflow-ab12');
  await page.getByRole('dialog').getByRole('button', { name: 'Confirm' }).click();
  await expect(page.getByText('Backup restored')).toBeVisible();
  await expect(page.getByRole('banner')).toContainText('Restored');
});

test('signed-in session survives a reload (CSRF recovered from GET /auth)', async ({ page }) => {
  await openSignedIn(page);
  await page.reload();
  await expect(page.locator('.conn-live')).toBeVisible();
  await expect(page.getByRole('banner').getByRole('button', { name: 'Sign in' })).toHaveCount(0);
  await page.locator('.tile', { hasText: 'Back lawn' }).click();
  await page.getByRole('button', { name: /Start Back lawn/ }).click();
  await expect(page.locator('#now-card')).toContainText('Back lawn');
});

test('protected reads show the sign-in page first', async ({ page, request }) => {
  await mock(request, 'set', { protect_reads: true });
  await page.goto('/');
  await expect(page.getByRole('heading', { name: 'Sign in' })).toBeVisible();
  await page.getByLabel('Admin password').fill('zoneflow');
  await page.getByRole('button', { name: 'Sign in' }).click();
  await expect(page.locator('.conn-live')).toBeVisible();
  expect(DEMO_UTC).toBeGreaterThan(0);
});

test('keyboard: skip link, navigation and dialogs', async ({ page, isMobile }) => {
  test.skip(isMobile, 'keyboard checks run on the desktop viewport');
  await page.goto('/');
  await expect(page.locator('.conn-live')).toBeVisible();
  await page.keyboard.press('Tab');
  await expect(page.getByRole('link', { name: 'Skip to content' })).toBeFocused();
  await page.keyboard.press('Enter');
  await page.keyboard.press('Tab');
  // Focus moves into main content.
  const inMain = await page.evaluate(() => !!document.activeElement?.closest('main'));
  expect(inMain).toBe(true);
  await page.locator('.tile', { hasText: 'Back lawn' }).focus();
  await page.keyboard.press('Enter');
  await expect(page.getByRole('dialog')).toBeVisible();
  await page.keyboard.press('Escape');
  await expect(page.getByRole('dialog')).toBeHidden();
  await signIn(page);
});

test('first-run steps resume on a later visit until finished or skipped', async ({ page, request }) => {
  await reset(request, { scenario: 'fresh' });
  await page.goto('/');
  await page.getByLabel('Setup code').fill('K7Q2M9XW4R');
  await page.getByLabel('New admin password', { exact: true }).fill('garden hose 42');
  await page.getByLabel('Repeat the password').fill('garden hose 42');
  await page.getByRole('button', { name: 'Set password' }).click();
  await expect(page.getByRole('heading', { name: 'Where is the controller?' })).toBeVisible();
  // Leave and come back: the controller says setup is not complete.
  await page.reload();
  await expect(page.getByRole('heading', { name: 'Where is the controller?' })).toBeVisible();
  await page.getByRole('button', { name: 'Skip setup for now' }).click();
  await expect(page.locator('.conn-live')).toBeVisible();
  await page.reload();
  await expect(page.locator('.conn-live')).toBeVisible();
  await expect(page.getByRole('heading', { name: 'Where is the controller?' })).toHaveCount(0);
  const auth = await (await request.get('/api/v1/auth')).json();
  expect(auth.setup_complete).toBe(true);
});

test('history: grouped by day, summarised per zone, and live', async ({ page }) => {
  const errs = watchErrors(page);
  await openSignedIn(page, '/history');
  await expect(page.getByRole('heading', { name: 'History', level: 1 })).toBeVisible();
  await expect(page.getByRole('region', { name: 'Yesterday' })).toContainText('Vegetable beds watered');
  await expect(page.getByText('Update installed; restarting')).toBeVisible();
  const summary = page.locator('.usage');
  await expect(summary).toContainText('Front lawn');
  await expect(summary).toContainText(' L');
  // Only one zone.
  await page.getByLabel('Show').selectOption({ label: 'Drip: pots' });
  await expect(page.locator('.hist-day li').first()).toContainText('Drip: pots');
  await expect(page.locator('.usage li')).toHaveCount(1);
  // A run that ends now appears without reloading.
  await page.getByLabel('Show').selectOption({ label: 'All zones' });
  await go(page, '/');
  await page.locator('.tile', { hasText: 'Back lawn' }).click();
  await page.getByRole('button', { name: /Start Back lawn/ }).click();
  await page.getByRole('button', { name: 'Stop all' }).first().click();
  await go(page, '/history');
  await expect(page.getByRole('region', { name: 'Today' })).toContainText('Back lawn watered (stopped)');
  errs.check();
});

test('sensors: the spare input can hold programs like the rain sensor', async ({ page, request }) => {
  const errs = watchErrors(page);
  await openSignedIn(page, '/settings/sensors');
  await page.getByRole('switch', { name: /Spare input connected/ }).check();
  await expect(page.getByRole('radio', { name: 'Show only' })).toBeChecked();
  await page.getByRole('radio', { name: 'Hold programs' }).check();
  await page.getByRole('button', { name: 'Save' }).click();
  await expect(page.getByText('Saved')).toBeVisible();
  const cfg = await (await request.get('/api/v1/config')).json();
  expect(cfg.sensors.spare).toMatchObject({ enabled: true, function: 'inhibit' });
  await mock(request, 'set', { spare: true });
  await go(page, '/');
  await expect(page.locator('.banner', { hasText: 'Spare input is active' })).toBeVisible();
  errs.check();
});

test('MQTT: connection state, write-only password, Home Assistant entity type', async ({ page, request }) => {
  const errs = watchErrors(page);
  await openSignedIn(page, '/settings/mqtt');
  await expect(page.getByRole('status').filter({ hasText: 'Connected' })).toBeVisible();
  await expect(page.getByText('A password is stored and never shown.')).toBeVisible();
  await expect(page.getByLabel('Password', { exact: true })).toHaveValue('');
  await page.getByLabel('Broker address').fill('mqtt-fail.local');
  await page.getByLabel('Password', { exact: true }).fill('new secret');
  await page.getByRole('radio', { name: 'Switches' }).check();
  await page.getByRole('button', { name: 'Save' }).click();
  await expect(page.getByText('Saved')).toBeVisible();
  await expect(page.getByRole('status').filter({ hasText: 'Not connected; retrying' })).toBeVisible();
  await expect(page.getByLabel('Password', { exact: true })).toHaveValue('');
  const m = await (await request.get('/api/v1/mqtt')).json();
  expect(m.host).toBe('mqtt-fail.local');
  expect(m.ha_zone_entity).toBe('switch');
  expect(m.password_set).toBe(true);
  expect(JSON.stringify(m)).not.toContain('new secret');
  // TLS with a custom CA: no CA stored yet is said plainly.
  await page.getByRole('switch', { name: /Encrypt/ }).check();
  await expect(page.getByRole('textbox', { name: 'Port' })).toHaveValue('8883');
  await page.getByRole('radio', { name: 'My own CA certificate' }).check();
  await expect(page.getByText('No CA certificate stored yet')).toBeVisible();
  errs.check();
});

test('a zone default run time is preselected for manual runs', async ({ page, request }) => {
  await openSignedIn(page, '/zones/2');
  await expect(page.getByText(/Measured: 255 mA/)).toBeVisible();
  await page.getByRole('textbox', { name: 'Default run time' }).fill('7');
  await page.getByRole('button', { name: 'Save' }).click();
  await expect(page.getByText('Saved')).toBeVisible();
  const z = await (await request.get('/api/v1/zones/2')).json();
  expect(z.default_run_s).toBe(420);
  await go(page, '/run');
  await page.getByRole('radio', { name: /Back lawn/ }).check();
  await expect(page.getByRole('button', { name: 'Start Back lawn for 7 min' })).toBeVisible();
});

test('one clock-format setting changes shown and entered times', async ({ page }) => {
  await openSignedIn(page, '/settings/device');
  await page.getByRole('radio', { name: '12-hour' }).check();
  await go(page, '/programs');
  await expect(page.locator('.program', { hasText: 'Lawns, early' })).toContainText(/5:30\sAM/i);
  await go(page, '/programs/1');
  await expect(page.getByLabel('Start times 1: AM or PM')).toHaveValue('am');
  await expect(page.getByLabel('Start times 1: hour')).toHaveValue('5');
  await go(page, '/settings/device');
  await page.getByRole('radio', { name: '24-hour' }).check();
  await go(page, '/programs/1');
  await expect(page.getByLabel('Start times 1: AM or PM')).toHaveCount(0);
  await expect(page.getByLabel('Start times 1: hour')).toHaveValue('5');
});

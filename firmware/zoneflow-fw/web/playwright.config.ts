import { defineConfig, devices } from '@playwright/test';

// End-to-end tests and documentation screenshots run against the mock
// controller serving the packed UI (../www), with the device's CSP. They
// share one mock, so they run one at a time; each test resets it.
const PORT = 8788;

export default defineConfig({
  testDir: 'tests/e2e',
  fullyParallel: false,
  workers: 1,
  retries: process.env['CI'] ? 1 : 0,
  reporter: process.env['CI'] ? [['list'], ['html', { open: 'never' }]] : 'list',
  timeout: 30_000,
  use: {
    baseURL: `http://127.0.0.1:${PORT}`,
    locale: 'en-GB',
    timezoneId: 'Europe/Berlin',
    trace: 'retain-on-failure',
  },
  webServer: {
    command: `node mock/server.ts --port ${PORT} --static ../www --quiet`,
    url: `http://127.0.0.1:${PORT}/api/v1/auth`,
    reuseExistingServer: !process.env['CI'],
  },
  projects: [
    { name: 'phone', testIgnore: /screenshots/, use: { ...devices['Pixel 7'], browserName: 'chromium' } },
    { name: 'phone-320', testMatch: /layout\.spec/, use: { browserName: 'chromium', viewport: { width: 320, height: 640 }, isMobile: true, hasTouch: true } },
    { name: 'desktop', testIgnore: /screenshots/, use: { browserName: 'chromium', viewport: { width: 1280, height: 860 } } },
    { name: 'screenshots', testMatch: /screenshots\.spec/, use: { browserName: 'chromium' } },
  ],
});

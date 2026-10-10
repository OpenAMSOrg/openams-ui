import { defineConfig, devices } from '@playwright/test';

// The gallery is the page under test: the panel, the real logic behind it and
// every catalog state (docs/design/UNIFIED_UI.md 9). `@playwright/test` is
// pinned to 1.63.0 because that browser is preinstalled, so nothing here may
// ask for a download.

// Two checkouts on one machine would both want 5179, and --strictPort (in the
// gallery script) makes the second one fail loudly rather than drift, so the
// port is the caller's to move: PW_PORT in, the default out.
const port = process.env.PW_PORT ?? '5179';

export default defineConfig({
    testDir: './tests',
    timeout: 30_000,
    expect: { timeout: 5_000 },
    fullyParallel: true,
    forbidOnly: !!process.env.CI,
    // The HTML report is CI's to have: it is what the failure artifact uploads,
    // and a local run would only leave an untracked report behind.
    reporter: process.env.CI ? [['list'], ['html', { open: 'never' }]] : [['list']],
    use: {
        baseURL: `http://127.0.0.1:${port}`,
        trace: 'off',
        screenshot: 'off',
    },
    projects: [{ name: 'chromium', use: { ...devices['Desktop Chrome'] } }],
    webServer: {
        command: 'npm run gallery',
        url: `http://127.0.0.1:${port}/gallery.html`,
        reuseExistingServer: !process.env.CI,
        timeout: 120_000,
    },
});

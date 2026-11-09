import { expect, test } from '@playwright/test';
import { action, card, expectRequests, listedActions, open, requests } from './panel';

// A failed toolchange or unload (the host's `toolchange_fault`) and the Retry it
// advertises (`commands.retry`): the message row says what failed, and the FPS
// alert holds it with Retry, Unload and Clear errors. A host without the
// command (the 'load-failed' card) gets no Retry.
test.describe('retry a failed toolchange', () => {
    test.beforeEach(async ({ page }) => {
        await open(page, { state: 'sheet', width: 1200 });
    });

    test('the message row says what failed and why; the stepper marks where', async ({ page }) => {
        const failed = card(page, 'toolchange-failed');
        await expect(failed.locator('.status-row .message')).toHaveText('Loading T1 failed: Spool loading failed (NO_PROGRESS)');
        await expect(failed.locator('.status-row .message')).toHaveClass(/tone-error/);
        // The step it failed at is the stepper's, in red; the row does not say it again.
        await expect(failed.locator('.stepper')).toHaveClass(/kind-error/);
    });

    test('the FPS alert holds the failure with Retry, Unload and Clear errors', async ({ page }) => {
        const failed = card(page, 'toolchange-failed');
        await failed.locator('[data-popover-invoker="alerts"]').click();
        const alerts = failed.locator('[data-popover="alerts"]:popover-open');
        await expect(alerts).toBeVisible();
        await expect(alerts.locator('.al-title')).toHaveText('Loading T1 failed (Push new filament into extruder)');
        await expect(alerts.locator('.al-text')).toHaveText('Spool loading failed (NO_PROGRESS)');
        expect(await listedActions(alerts)).toEqual(['Retry', 'Unload', 'Clear errors']);
        // Nothing was fed, so there is nothing to back out: Unload says why.
        await expect(action(alerts, 'unload')).toHaveAttribute('data-enabled', 'false');
        await expect(action(alerts, 'unload')).toHaveAttribute('title', 'No filament is loaded');
    });

    test('Retry sends the advertised command, with the lane', async ({ page }) => {
        const failed = card(page, 'toolchange-failed');
        await failed.locator('[data-popover-invoker="alerts"]').click();
        await action(failed.locator('[data-popover="alerts"]:popover-open'), 'retry').click();
        await expectRequests(page, 1);
        expect(await requests(page)).toEqual([{ kind: 'gcode', script: 'OAMSM_RETRY FPS=fps1' }]);
    });

    test('Clear errors sends the reset', async ({ page }) => {
        const failed = card(page, 'toolchange-failed');
        await failed.locator('[data-popover-invoker="alerts"]').click();
        await action(failed.locator('[data-popover="alerts"]:popover-open'), 'clear_errors').click();
        expect(await requests(page)).toEqual([{ kind: 'gcode', script: 'OAMSM_CLEAR_ERRORS' }]);
    });

    test('a host without the command offers no Retry', async ({ page }) => {
        const failed = card(page, 'load-failed');
        await failed.locator('[data-popover-invoker="alerts"]').click();
        const alerts = failed.locator('[data-popover="alerts"]:popover-open');
        await expect(alerts.locator('.action[data-action="retry"]')).toHaveCount(0);
    });
});

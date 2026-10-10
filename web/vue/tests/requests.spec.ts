import { expect, test } from '@playwright/test';
import { action, card, clickTile, dialog, expectRequests, message, open, openPopover, requests, target } from './panel';

// What an action asks the host for, and what happens while it waits
// (UNIFIED_UI.md 8: the panel emits `request` and the host sends it;
// PRINCIPLES.md 9: a press is shown at once and never sent twice).
test.describe('requests', () => {
    test.beforeEach(async ({ page }) => {
        await open(page, { state: 'sheet', width: 1200 });
    });

    test('a gcode action is emitted as `request`, with the core\'s script', async ({ page }) => {
        const states = card(page, 'tile-states');
        await clickTile(states, 'oams12');
        await action(openPopover(page, 'tile'), 'load').click();
        // The payload is the ActionResult itself, nothing wrapped around it.
        expect(await requests(page)).toEqual([{ kind: 'gcode', script: 'OPENAMS_LOAD GROUP=T3 SLOT=5' }]);
    });

    test('an rpc action is emitted as `request`, with its method and params', async ({ page }) => {
        const states = card(page, 'tile-states');
        await states.locator('[data-target="ring"]').first().click({ position: { x: 6, y: 20 } });
        await action(openPopover(page, 'tile'), 'spool_list_refresh').click();
        expect(await requests(page)).toEqual([
            { kind: 'rpc', method: 'server.spoolman.proxy', params: { request_method: 'GET', path: '/v1/spool' } },
        ]);
    });

    test('a toolhead button works directly, with no popover', async ({ page }) => {
        const idle = card(page, 'idle');
        await idle.locator('.th-actions [data-action="unload"]').click();
        expect(await requests(page)).toEqual([{ kind: 'gcode', script: 'OPENAMS_UNLOAD FPS=fps1' }]);
    });

    test('a double click on Load is one request, not two', async ({ page }) => {
        const states = card(page, 'tile-states');
        await clickTile(states, 'oams12');
        const load = action(openPopover(page, 'tile'), 'load');
        // The fastest a hand can manage: the first click sends and closes the
        // menu, so the second finds nothing to press.
        await load.dblclick({ force: true });
        expect(await requests(page)).toHaveLength(1);
        // Opened again while the printer has not answered, the menu still
        // refuses a second Load.
        await clickTile(states, 'oams12');
        const again = action(openPopover(page, 'tile'), 'load');
        await expect(again).toHaveAttribute('data-pending', 'true');
        await again.click();
        expect(await requests(page)).toHaveLength(1);
    });

    test('choosing an action closes the menu; a dimmed one leaves it open', async ({ page }) => {
        const states = card(page, 'tile-states');
        await clickTile(states, 'oams12');
        const menu = openPopover(page, 'tile');
        await action(menu, 'load').click();
        await expect(page.locator('[data-popover]:popover-open')).toHaveCount(0);

        await clickTile(states, 'oams12');
        const dimmed = openPopover(page, 'tile').locator('[data-enabled="false"]').first();
        await dimmed.click({ force: true });
        await expect(page.locator('[data-popover]:popover-open')).toHaveCount(1);
    });

    test('the press is shown at once, and the panel is quiet until then', async ({ page }) => {
        const states = card(page, 'tile-states');
        await clickTile(states, 'oams12');
        const load = action(openPopover(page, 'tile'), 'load');
        await expect(load).not.toHaveAttribute('data-pending', 'true');
        await expectRequests(page, 0);
        await load.click();
        // The request went out on the same click, and the menu closed; opened
        // again, the action shows its pressed state.
        await expectRequests(page, 1);
        await clickTile(states, 'oams12');
        const again = action(openPopover(page, 'tile'), 'load');
        await expect(again).toHaveAttribute('data-pending', 'true');
        // And the reason: the second click is refused while it waits.
        await again.click();
        expect(await requests(page)).toHaveLength(1);
    });

    test('the wait ends when the printer\'s next view differs', async ({ page }) => {
        await open(page, { state: 'player', fixture: 'two_lanes', width: 1200 });
        const panel = page.locator('.oams-panel');
        // One of the two lanes of the last fixture step has filament, so its
        // Unload is pressable.
        const unload = panel.locator('.th-actions [data-action="unload"][data-enabled="true"]');
        await unload.click();
        await expect(unload).toHaveAttribute('data-pending', 'true');
        await expect(panel.locator('[data-pending="true"]')).toHaveCount(1);

        // The host feeds the panel a different state (the gallery's own event
        // slider). The wait is over at once, and nothing is said about it: the
        // press was answered, not abandoned.
        await page.locator('#step').fill('0');
        await expect(panel.locator('[data-pending="true"]')).toHaveCount(0);
        await expect(message(panel)).toHaveText('No units connected');
    });

    test('a stalled press says the printer did not answer', async ({ page }) => {
        test.setTimeout(30_000);
        const states = card(page, 'tile-states');
        await clickTile(states, 'oams12');
        const load = action(openPopover(page, 'tile'), 'load');
        await load.click();
        await expectRequests(page, 1);
        // Nothing answers for the full wait.
        await expect(message(states)).toHaveText('No response from the printer', { timeout: 12_000 });
        await expect(message(states)).toHaveClass(/tone-error/);
        // And the press is released, so the action can be tried again.
        await clickTile(states, 'oams12');
        await expect(action(openPopover(page, 'tile'), 'load')).not.toHaveAttribute('data-pending', 'true');
    });

    test('a dimmed action sends nothing at all', async ({ page }) => {
        const idle = card(page, 'idle-unloaded');
        // The toolhead of a card with no filament cannot unload.
        const unload = idle.locator('.th-actions [data-action="unload"]');
        await expect(unload).toHaveAttribute('aria-disabled', 'true');
        await unload.click({ force: true });
        expect(await requests(page)).toEqual([]);
        await expect(message(idle)).toHaveCount(0);
    });

    test('a card the panel cannot act on never asks for anything', async ({ page }) => {
        // Nothing is pressed: a page of panels must not send a request.
        expect(await requests(page)).toEqual([]);
    });

    test('a confirm is asked before the request, never after', async ({ page }) => {
        const states = card(page, 'tile-states');
        await target(states, 'oams23', 'tag').click();
        await action(openPopover(page, 'tag'), 'clear_fault').click();
        await expect(dialog(page)).toBeVisible();
        expect(await requests(page)).toEqual([]);
        await dialog(page).locator('[data-role="cancel"]').click();
        expect(await requests(page)).toEqual([]);
    });
});

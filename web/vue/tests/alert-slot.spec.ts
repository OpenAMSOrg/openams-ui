import { expect, test } from '@playwright/test';
import { card, open } from './panel';

// PRINCIPLES.md 2, "A reserved slot has a resting state": the alert slots of a
// toolhead and of a unit rest on a muted check named "No alerts" when there is
// nothing to report, instead of sitting blank. The check is not a control: it
// does nothing on the panel, so it takes no focus.

test.describe('the alert slots at rest', () => {
    test('the toolhead\'s and the unit\'s slots show a check named "No alerts"', async ({ page }) => {
        await open(page, { state: 'sheet', width: 1200 });
        const fps = card(page, 'idle');

        const toolhead = fps.locator('.fps-alert .alert-slot.none');
        await expect(toolhead).toHaveCount(1);
        await expect(toolhead.locator('svg')).toBeVisible();
        await expect(toolhead).toHaveAttribute('role', 'img');
        await expect(toolhead).toHaveAccessibleName('No alerts');
        await expect(toolhead).toHaveAttribute('title', 'No alerts');

        // Both of the card's units have nothing to report, so both rest.
        const units = fps.locator('.mmu-alert .alert-slot.none');
        await expect(units).toHaveCount(2);
        for (const slot of await units.all()) {
            await expect(slot).toHaveAccessibleName('No alerts');
            await expect(slot.locator('svg')).toBeVisible();
        }
    });

    test('it keeps the size of the slot, and is neither a button nor focusable', async ({ page }) => {
        await open(page, { state: 'sheet', width: 1200 });
        const idle = card(page, 'idle').locator('.fps-alert .alert-slot');
        const faulted = card(page, 'load-failed').locator('.fps-alert .alert-slot');
        const [a, b] = await Promise.all([idle.boundingBox(), faulted.boundingBox()]);
        expect(Math.round(a!.width)).toBe(Math.round(b!.width));
        expect(Math.round(a!.height)).toBe(Math.round(b!.height));

        await expect(card(page, 'idle').locator('button.alert-slot')).toHaveCount(0);
        const focusable = await idle.evaluate((el) => (el as HTMLElement).tabIndex);
        expect(focusable).toBe(-1);
    });

    test('a slot with something to report is the button it was', async ({ page }) => {
        await open(page, { state: 'sheet', width: 1200 });
        const faulted = card(page, 'load-failed');
        await expect(faulted.locator('.fps-alert button.alert-slot')).toHaveCount(1);
        await expect(faulted.locator('.fps-alert .alert-slot.none')).toHaveCount(0);
    });

    // "No alerts" is a claim about the whole scope. A slot with no badge is
    // empty for one of two reasons: nothing alerts (the check), or what alerts
    // is shown right beside it (the message row, a tile's tag), where a check
    // would contradict it.
    test('a runout in the message row leaves the toolhead\'s slot empty, not "No alerts"', async ({ page }) => {
        await open(page, { state: 'sheet', width: 1200 });
        const runout = card(page, 'runout-empty');

        await expect(runout.locator('.message')).toBeVisible();
        const slot = runout.locator('.fps-alert .alert-slot');
        await expect(slot).toHaveCount(1);
        await expect(slot).not.toHaveClass(/tone-/);
        await expect(slot.locator('svg')).toHaveCount(0);
        await expect(runout.locator('.fps-alert [aria-label="No alerts"]')).toHaveCount(0);
    });

    test('a fault on a tile: the lane and the unit that has it are not "clear", the others are', async ({ page }) => {
        await open(page, { state: 'sheet', width: 1200 });
        const states = card(page, 'tile-states');
        const fps1 = states.locator('section.fps[data-toolhead="fps1"]');
        const fps2 = states.locator('section.fps[data-toolhead="fps2"]');

        // The fault names oams2's bay 3: the lane's row says it, and so does the tile.
        await expect(fps1.locator('.fps-alert [aria-label="No alerts"]')).toHaveCount(0);
        await expect(fps1.locator('.mmu[data-unit="oams2"] .mmu-alert [aria-label="No alerts"]')).toHaveCount(0);
        // oams1 is on the same lane and has no alert of its own.
        await expect(fps1.locator('.mmu[data-unit="oams1"] .mmu-alert [aria-label="No alerts"]')).toHaveCount(1);
        // The other lane has nothing to report anywhere.
        await expect(fps2.locator('.fps-alert [aria-label="No alerts"]')).toHaveCount(1);
    });
});

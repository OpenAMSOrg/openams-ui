import { expect, test } from '@playwright/test';
import { card, dialog, open, requests } from './panel';

// UNIFIED_UI 4g, drawn by the panel from the core's words: a dark filament
// gets a light outline where its color sits on the dark surface (the resting
// segment and the hotend icon's frame), and Stop stops the whole sequence.

test.describe('dark filament outline', () => {
    test('a black filament outlines the resting segment and the hotend icon', async ({ page }) => {
        await open(page, { state: 'sheet', width: 1200 });
        const fps = card(page, 'idle');   // the sheet's loaded spool is #1c1c1c

        const seg = fps.locator('.step.rest i');
        await expect(seg).toHaveClass(/outline/);
        const shadow = await seg.evaluate((el) => getComputedStyle(el).boxShadow);
        expect(shadow).not.toBe('none');
        // 1 px, spread, in the faint text color
        expect(shadow).toMatch(/0px 0px 0px 1px/);

        const icon = fps.locator('.th-icon');
        await expect(icon).toHaveClass(/outline/);
        await expect(icon).toHaveCSS('outline-style', 'solid');
        await expect(icon).toHaveCSS('outline-width', '1px');
    });

    test('a mid color has none', async ({ page }) => {
        await open(page, { state: 'sheet', width: 1200 });
        const fps = card(page, 'idle-red');

        await expect(fps.locator('.step.rest i')).toHaveCSS('background-color', 'rgb(211, 47, 47)');
        await expect(fps.locator('.step.rest i')).not.toHaveClass(/outline/);
        await expect(fps.locator('.step.rest i')).toHaveCSS('box-shadow', 'none');
        await expect(fps.locator('.th-icon')).not.toHaveClass(/outline/);
    });
});

test.describe('Stop stops the whole sequence', () => {
    test('it is enabled in a heat step and sends the host\'s own stop', async ({ page }) => {
        await open(page, { state: 'sheet', width: 1200 });
        const fps = card(page, 'heating');
        const stop = fps.locator('.th-actions [data-action="stop"]');

        await expect(stop).toBeEnabled();
        await stop.click();
        // the confirmation says what stops, not only "the load"
        const dlg = dialog(page);
        await expect(dlg).toContainText('Stop now?');
        await expect(dlg).toContainText('What is running stops after the current move.');
        expect(await requests(page)).toEqual([]);
        await dlg.getByRole('button', { name: 'Stop' }).click();
        await expect.poll(async () => (await requests(page)).length).toBe(1);
        expect(await requests(page)).toEqual([{ kind: 'rpc', method: 'server.openams.stop', params: { lane: 'fps1' } }]);
    });

    test('it is dimmed, with its reason, when nothing runs', async ({ page }) => {
        await open(page, { state: 'sheet', width: 1200 });
        const stop = card(page, 'idle').locator('.th-actions [data-action="stop"]');

        await expect(stop).toBeDisabled();
    });
});

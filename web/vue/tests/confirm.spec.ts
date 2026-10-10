import { expect, test } from '@playwright/test';
import {
    action, card, clickTile, dialog, listedActions, open, openPopover, requests, target, tile,
} from './panel';

// A spool waiting for its confirmation (UNIFIED_UI.md 7, SpoolConfirm): the
// tile wears the "Confirm" tag, and the tag's details carry the answer - a
// dialog asking how much is left, starting from a full spool, that sends the
// core's `spool confirm <slot> <grams>`. The state sheet's oams33 is such a
// spool: 770 g at 77 %, so a full one is 1000 g.
test.describe('spool confirmation', () => {
    test.beforeEach(async ({ page }) => {
        await open(page, { state: 'sheet', width: 1200 });
    });

    test('the Confirm tag offers the confirmation', async ({ page }) => {
        const states = card(page, 'tile-states');
        await expect(tile(states, 'oams33')).toHaveClass(/pending/);
        await target(states, 'oams33', 'tag').click();
        const tag = openPopover(page, 'tag');
        await expect(tag).toBeVisible();
        await expect(tag.locator('.pop-head')).toHaveText('Confirm');
        await expect(tag.locator('.tag-detail')).toHaveText('Confirm the linked Spoolman spool.');
        expect(await listedActions(tag)).toEqual(['Confirm spool']);
        await expect(action(tag, 'confirm_spool')).toHaveAttribute('data-enabled', 'true');
        // The tile's own menu is unchanged: the answer is the tag's.
        await page.keyboard.press('Escape');
        await clickTile(states, 'oams33');
        expect(await listedActions(openPopover(page, 'tile'))).not.toContain('Confirm spool');
    });

    test('a full spool is confirmed as it opens', async ({ page }) => {
        const states = card(page, 'tile-states');
        await target(states, 'oams33', 'tag').click();
        await action(openPopover(page, 'tag'), 'confirm_spool').click();
        const dlg = dialog(page);
        await expect(dlg).toBeVisible();
        await expect(page.locator('[data-popover]:popover-open')).toHaveCount(0);
        await expect(dlg).toHaveAccessibleName('Confirm spool');
        await expect(dlg.locator('label').first()).toHaveText(/Remaining\s*g/);
        await expect(dlg.locator('input[data-field="grams"]')).toHaveValue('1000');
        const ok = dlg.locator('button[data-action="confirm_spool"]');
        await expect(ok).toHaveText('Confirm');
        await expect(ok).toBeEnabled();
        await ok.click();
        await expect(dlg).toBeHidden();
        expect(await requests(page)).toEqual([
            { kind: 'rpc', method: 'server.openams_spoolman.confirm', params: { bay: 'oams3-2', remaining_g: 1000 } },
        ]);
    });

    test('a spool that is not full is confirmed with what is left', async ({ page }) => {
        const states = card(page, 'tile-states');
        await target(states, 'oams33', 'tag').click();
        await action(openPopover(page, 'tag'), 'confirm_spool').click();
        const dlg = dialog(page);
        await dlg.locator('input[data-field="grams"]').fill('640');
        await dlg.locator('button[data-action="confirm_spool"]').click();
        expect(await requests(page)).toEqual([
            { kind: 'rpc', method: 'server.openams_spoolman.confirm', params: { bay: 'oams3-2', remaining_g: 640 } },
        ]);
    });

    test('Cancel sends nothing', async ({ page }) => {
        const states = card(page, 'tile-states');
        await target(states, 'oams33', 'tag').click();
        await action(openPopover(page, 'tag'), 'confirm_spool').click();
        const dlg = dialog(page);
        await dlg.locator('[data-role="cancel"]').click();
        await expect(dlg).toBeHidden();
        expect(await requests(page)).toEqual([]);
    });
});

import { expect, test } from '@playwright/test';
import { action, card, clickTile, dialog, open, openPopover, openSelect, options, requests, tile } from './panel';

// A bay's group is shown where the concept shows it, on its tile's tool pill
// (`T0 ∞n`), and is changed there: the pill opens the bay's own "Change
// group…". Groups belong to an FPS lane, not to a unit, so the panel has no
// per-unit groups list (owner: "we really don't need to show the 'groups'
// part, since it is now shown on the pill and we can change groups from
// there").
//
// The tile-states card (gallery/scenarios.ts, tileStates()) has T0 = oams1 bays
// 0 and 2 plus oams2 bay 1, T3 = oams1 bay 1, and oams1 bay 3 in no group; its
// lane fps1 has T0 loaded, so T0 cannot be edited and T3 can.
const pill = (scope: import('@playwright/test').Locator, slot: string) =>
    tile(scope, slot).locator('[data-target="group"]');

test.describe('changing a group from the tile', () => {
    test.beforeEach(async ({ page }) => {
        await open(page, { state: 'sheet', width: 1200 });
    });

    test('there is no separate groups list', async ({ page }) => {
        await expect(page.locator('.oams-panel .groups')).toHaveCount(0);
    });

    test('the pill names the change and the group it shows', async ({ page }) => {
        const states = card(page, 'tile-states');
        await expect(pill(states, 'oams12')).toHaveAttribute('aria-label', 'Change group… (T3)');
        await expect(pill(states, 'oams11')).toHaveAttribute('aria-label', 'Change group… (T0)');
    });

    test('the pill opens the bay\'s change-group form and sends the core\'s script', async ({ page }) => {
        const states = card(page, 'tile-states');
        await pill(states, 'oams12').click();

        // The core's own form: the groups on the bay's lane, then "No group"
        // and "New group", with the bay's own group chosen and no way to send
        // it until another is picked (require_change).
        const dlg = dialog(page);
        await expect(dlg).toBeVisible();
        const save = dlg.locator('[data-action="change_group"]');
        await expect(save).toHaveText('Change group');
        await expect(save).toBeDisabled();
        const { list } = await openSelect(dlg, 'group');
        expect((await options(list).allInnerTexts()).map((t) => t.trim())).toEqual([
            'T0', 'T3', 'No group', 'New group',
        ]);
        await list.getByRole('option', { name: 'New group', exact: true }).click();
        await expect(save).toBeEnabled();

        // One request, the core's own script: create T1 (the first free name:
        // T0, T2, T3 and T6..T9 exist) and put this bay in it.
        await save.click();
        await expect(dlg).toBeHidden();
        expect(await requests(page)).toEqual([
            {
                kind: 'gcode',
                script: 'OAMSM_CREATE_GROUP GROUP=T1\nOAMSM_ASSIGN_BAY GROUP=T1 OAMS=oams1 BAY=1',
            },
        ]);
    });

    test('a pill whose group cannot change now says why and sends nothing', async ({ page }) => {
        const states = card(page, 'tile-states');
        // T0 is loaded on fps1, so its bays cannot change group.
        const loaded = pill(states, 'oams11');
        await expect(loaded).toHaveAttribute('aria-disabled', 'true');
        await expect(loaded).toHaveAttribute('title', 'This group is loaded in the toolhead');
        await loaded.click({ force: true });
        await expect(dialog(page)).toHaveCount(0);
        expect(await requests(page)).toEqual([]);
    });

    test('the rest of the tile still opens the tile\'s menu', async ({ page }) => {
        const states = card(page, 'tile-states');
        await clickTile(states, 'oams12');
        await expect(action(openPopover(page, 'tile'), 'load')).toBeVisible();
        await expect(dialog(page)).toHaveCount(0);
    });

    test('change group is in the tile\'s menu too, from the ring\'s row', async ({ page }) => {
        const states = card(page, 'tile-states');
        await states.locator('.lane[data-slot="oams12"] [data-target="ring"]').click({ position: { x: 6, y: 20 } });
        await expect(action(openPopover(page, 'tile'), 'change_group')).toBeVisible();
    });
});

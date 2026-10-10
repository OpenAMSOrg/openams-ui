import { expect, test } from '@playwright/test';
import { action, card, clickRing, clickTile, listedActions, open, openPopover, popover, settled, target, tile } from './panel';

// The three click targets of a tile, and the other popovers of the panel
// (docs/design/UNIFIED_UI.md 4b, 7; PRINCIPLES.md 10). The state sheet's first
// card is two FPS cards: oams11 is a loaded T0, oams14 an empty bay, oams23 the
// error tile the lane's fault is shown on.
test.describe('click targets', () => {
    test.beforeEach(async ({ page }) => {
        await open(page, { state: 'sheet', width: 1200 });
    });

    test('the ring and the tile open the same menu, with the same actions', async ({ page }) => {
        const states = card(page, 'tile-states');
        await expect(openPopover(page, 'tile')).toHaveCount(0);
        // Both targets name the one popover.
        const id = await target(states, 'oams11', 'tile').getAttribute('popovertarget');
        expect(id).toBeTruthy();
        await expect(target(states, 'oams11', 'ring')).toHaveAttribute('popovertarget', id!);
        // The ring's row opens it...
        await target(states, 'oams11', 'ring').click();
        const menu = openPopover(page, 'tile');
        await expect(menu).toBeVisible();
        const merged = [
            'Load',
            'Unload',
            'Edit spool',
            'Refresh spools',
            'Unlink spool',
            'Change group…',
            'Re-read tag',
            'Calibrate',
        ];
        const viaRing = await listedActions(menu);
        // ...and the tile's own target opens the very same one.
        await page.keyboard.press('Escape');
        await expect(menu).toBeHidden();
        await clickTile(states, 'oams11');
        await expect(menu).toBeVisible();
        expect(await listedActions(menu)).toEqual(viaRing);
        expect(viaRing).toEqual(merged);
        // There is no separate ring popover any more.
        await expect(tile(states, 'oams11').locator('[data-popover="ring"]')).toHaveCount(0);
    });

    test('the tag opens its details and the tag\'s actions', async ({ page }) => {
        const states = card(page, 'tile-states');
        await target(states, 'oams23', 'tag').click();
        const tag = openPopover(page, 'tag');
        await expect(tag).toBeVisible();
        // The core's own sentence, and the host's code under it, in mono.
        await expect(tag.locator('.tag-detail')).toHaveText('The feeder stalled in Spool 3');
        await expect(tag.locator('.tag-code')).toHaveText('feeder_stall');
        await expect(tag.locator('.tag-code')).toHaveCSS('font-family', /mono|Menlo|Consolas|Courier/i);
        // The fault's recovery, which the core marked for the tag.
        expect(await listedActions(tag)).toEqual(['Retry', 'Clear fault']);
    });

    test('the rest of the tile opens the tile\'s menu, load and unload included', async ({ page }) => {
        const states = card(page, 'tile-states');
        await clickTile(states, 'oams11');
        const tilePop = openPopover(page, 'tile');
        await expect(tilePop).toBeVisible();
        const listed = await listedActions(tilePop);
        expect(listed.slice(0, 2)).toEqual(['Load', 'Unload']);
        expect(listed).toContain('Edit spool');
    });

    test('a dimmed action is shown, and says why', async ({ page }) => {
        const states = card(page, 'tile-states');
        // An empty bay: nothing is linked, and there is no tag to re-read.
        await clickRing(states, 'oams14');
        const ring = openPopover(page, 'tile');
        const unlink = action(ring, 'unlink');
        // The menu is placed in the frame it opens in, so a click inside it has
        // to let that frame go by first (see settled() in panel.ts).
        await settled(page);
        await expect(unlink).toHaveAttribute('aria-disabled', 'true');
        // Focusable, so it can be read and reached by keyboard.
        await unlink.focus();
        await expect(unlink).toBeFocused();
        // Its reason is the tooltip, and a click on it does nothing.
        await expect(unlink).toHaveAttribute('title', 'No Spoolman spool is linked');
        await unlink.click({ force: true });
        await expect(ring).toBeVisible();
        await expect(page.locator('.oams-panel dialog[open]')).toHaveCount(0);
    });

    test('the alert badge opens its items and their actions', async ({ page }) => {
        // A load failure with no unit: the alert belongs to the toolhead.
        const failed = card(page, 'load-failed');
        const badge = failed.locator('[data-popover-invoker="alerts"]');
        await expect(badge).toBeVisible();
        await badge.click();
        const alerts = failed.locator('[data-popover="alerts"]:popover-open');
        await expect(alerts).toBeVisible();
        await expect(alerts.locator('.al-text')).toHaveText('Load failed: the filament did not reach the extruder');
        await expect(alerts.locator('.al-code')).toHaveText('load_failed');
        expect(await listedActions(alerts)).toEqual(['Resume', 'Clear errors']);
    });

    test('a unit alert lists its items in the same popover', async ({ page }) => {
        // A dryer fault and a missing adapter, both on the unit.
        const drying = card(page, 'drying');
        await drying.locator('[data-popover-invoker="alerts"]').click();
        const alerts = drying.locator('[data-popover="alerts"]:popover-open');
        await expect(alerts.locator('.al-text').first()).toHaveText('The dryer reported a fault.');
    });

    test('the env pill opens the unit\'s actions', async ({ page }) => {
        const states = card(page, 'tile-states');
        const pill = states.locator('[data-popover-invoker="unit"]').first();
        await expect(pill).toBeVisible();
        await pill.click();
        const unit = openPopover(page, 'unit');
        await expect(unit).toBeVisible();
        expect(await listedActions(unit)).toEqual(['Start drying', 'Stop drying']);
    });

    test('one popover at a time, and Escape closes it', async ({ page }) => {
        const states = card(page, 'tile-states');
        await target(states, 'oams11', 'ring').click();
        const first = tile(states, 'oams11').locator('[data-popover="tile"]');
        await expect(first).toBeVisible();
        // The next target takes over: never nested, never two.
        await clickTile(states, 'oams12');
        await expect(openPopover(page, 'tile')).toBeVisible();
        await expect(first).toBeHidden();
        await page.keyboard.press('Escape');
        await expect(openPopover(page, 'tile')).toBeHidden();
    });

    test('a click outside dismisses the popover', async ({ page }) => {
        const states = card(page, 'tile-states');
        await target(states, 'oams11', 'ring').click();
        await expect(openPopover(page, 'tile')).toBeVisible();
        await page.mouse.click(5, 5);
        await expect(openPopover(page, 'tile')).toBeHidden();
    });

    test('the popover is anchored under its own source', async ({ page }) => {
        const states = card(page, 'tile-states');
        const ring = target(states, 'oams11', 'ring');
        // Centred in the window, so there is room below it: the sheet is far
        // taller than the window, and scrolling only into view would leave the
        // tile at the bottom of it.
        await ring.evaluate((el) => el.scrollIntoView({ block: 'center' }));
        await ring.click();
        await settled(page);
        const a = await ring.boundingBox();
        const b = await openPopover(page, 'tile').boundingBox();
        const size = page.viewportSize();
        expect(a).not.toBeNull();
        expect(b).not.toBeNull();
        expect(size).not.toBeNull();
        // Aligned to the source's own left edge, and below it.
        expect(b!.x).toBeCloseTo(a!.x, 0);
        expect(b!.y).toBeGreaterThanOrEqual(a!.y + a!.height - 1);
        // And wholly on the screen.
        expect(b!.x).toBeGreaterThanOrEqual(0);
        expect(b!.y + b!.height).toBeLessThanOrEqual(size!.height);
    });

    test('a popover near the right edge is pulled back on to the screen', async ({ page }) => {
        // The toolhead's alert badge sits at the right end of its card, and its
        // popover is wide: it cannot be aligned and fit at once.
        const failed = card(page, 'load-failed');
        const badge = failed.locator('[data-popover-invoker="alerts"]');
        await badge.click();
        await settled(page);
        const a = await badge.boundingBox();
        const b = await failed.locator('[data-popover="alerts"]:popover-open').boundingBox();
        const size = page.viewportSize();
        expect(a).not.toBeNull();
        expect(b).not.toBeNull();
        expect(size).not.toBeNull();
        // The screen wins: wholly on it, and clear of the right edge.
        expect(b!.x).toBeGreaterThanOrEqual(0);
        expect(b!.x + b!.width).toBeLessThanOrEqual(size!.width);
        expect(b!.x).toBeLessThan(a!.x);
        expect(b!.y).toBeGreaterThanOrEqual(a!.y + a!.height - 1);
    });

    test('a popover with no room below opens above its source', async ({ page }) => {
        const bottom = card(page, 'offline');
        const ring = bottom.locator('[data-target="ring"]').first();
        await ring.scrollIntoViewIfNeeded();
        await ring.click({ position: { x: 6, y: 20 } });
        await settled(page);
        const a = await ring.boundingBox();
        const b = await openPopover(page, 'tile').boundingBox();
        const size = page.viewportSize();
        expect(a).not.toBeNull();
        expect(b).not.toBeNull();
        expect(size).not.toBeNull();
        // Flipped, aligned to the same edge, and wholly on the screen.
        expect(b!.x).toBeCloseTo(a!.x, 0);
        expect(b!.y).toBeGreaterThanOrEqual(0);
        expect(b!.y + b!.height).toBeLessThanOrEqual(a!.y + 1);
        expect(b!.y + b!.height).toBeLessThanOrEqual(size!.height);
    });

    test('a tile with no spool still offers the edit actions', async ({ page }) => {
        const states = card(page, 'tile-states');
        // An empty bay has no ring drawn, but the bay is still editable.
        const empty = tile(states, 'oams14');
        await expect(empty).toHaveAttribute('data-state', 'empty');
        await expect(empty.locator('svg')).toHaveCount(0);
        await clickRing(states, 'oams14');
        await expect(openPopover(page, 'tile')).toBeVisible();
    });

    test('every popover role the specs use exists', async ({ page }) => {
        for (const name of ['tag', 'tile', 'alerts', 'unit']) {
            expect(await popover(page, name).count()).toBeGreaterThan(0);
        }
    });

    test('a closed popover is nothing the panel lays out', async ({ page }) => {
        // Every tile carries its popovers in the DOM, so this is what keeps the
        // approved look (tools/shoot.mjs) exactly as it was: a popover that is
        // not open has no box, and the panel around it does not move.
        const states = card(page, 'tile-states');
        const closed = tile(states, 'oams11').locator('[data-popover="tile"]');
        expect((await closed.boundingBox())?.width ?? 0).toBe(0);
        const before = (await states.boundingBox())!.height;
        await target(states, 'oams11', 'ring').click();
        await expect(closed).toBeVisible();
        expect((await states.boundingBox())!.height).toBeCloseTo(before, 0);
    });
});

import { expect, test } from '@playwright/test';
import { action, card, clickRing, clickTile, message, open, openPopover, requests, settled } from './panel';

// A dimmed action says why where a touch can read it: a tooltip needs a
// pointer, so on a touchscreen the reason goes in the panel's one message row
// (PRINCIPLES.md 10). The taps are forced because Playwright reads
// aria-disabled as "not enabled" and would wait forever for one that is very
// deliberately still enabled: it is focusable and clickable, only inert.
test.use({ hasTouch: true });

test.describe('a touchscreen', () => {
    test.beforeEach(async ({ page }) => {
        await open(page, { state: 'sheet', width: 360 });
    });

    test('a tap on a dimmed action says its reason', async ({ page }) => {
        const states = card(page, 'tile-states');
        // The premise of the whole file: a touchscreen, so the panel's own
        // test for "there is no pointer to hover with" is true.
        expect(await page.evaluate(() => matchMedia('(pointer: coarse)').matches)).toBe(true);
        await clickRing(states, 'oams14');
        // An empty bay: nothing is linked, and there is no tag to re-read.
        const unlink = action(openPopover(page, 'tile'), 'unlink');
        await expect(unlink).toHaveAttribute('aria-disabled', 'true');
        await expect(message(states)).toHaveCount(0);
        // A popover is placed in the frame it opens in, and a tap carries a
        // point: let that frame go by, as a finger's would.
        await settled(page);

        await unlink.tap({ force: true });
        await expect(message(states)).toHaveText('No Spoolman spool is linked');
        // The tooltip's own reason is still there, for a mouse or a reader.
        await expect(unlink).toHaveAttribute('title', 'No Spoolman spool is linked');
        // And nothing was sent.
        expect(await requests(page)).toEqual([]);
    });

    test('a tap on a dimmed tile action says its reason too', async ({ page }) => {
        const states = card(page, 'tile-states');
        await clickTile(states, 'oams11');
        const load = action(openPopover(page, 'tile'), 'load');
        // oams11 holds the loaded T0, so its Load cannot run.
        await expect(load).toHaveAttribute('aria-disabled', 'true');
        await settled(page);
        await load.tap({ force: true });
        await expect(message(states)).toHaveText('This spool is already loaded');
        expect(await requests(page)).toEqual([]);
    });

    test('a tap on a dimmed action changes nothing but the message', async ({ page }) => {
        const states = card(page, 'tile-states');
        await clickRing(states, 'oams14');
        const ring = openPopover(page, 'tile');
        const reread = action(ring, 'reread');
        await settled(page);
        await reread.tap({ force: true });
        await expect(message(states)).toHaveText('No spool is inserted');
        // The popover stays: a dimmed action is not a way out of the menu.
        await expect(ring).toBeVisible();
        await expect(page.locator('.oams-panel dialog[open]')).toHaveCount(0);
    });
});

test.describe('the same click with a mouse', () => {
    test.use({ hasTouch: false });

    test('says nothing at all: the tooltip was enough', async ({ page }) => {
        await open(page, { state: 'sheet', width: 1200 });
        expect(await page.evaluate(() => matchMedia('(pointer: coarse)').matches)).toBe(false);
        const states = card(page, 'tile-states');
        await clickRing(states, 'oams14');
        const unlink = action(openPopover(page, 'tile'), 'unlink');
        // The menu is placed in the frame after it opens; click once it is there.
        await settled(page);
        await unlink.click({ force: true });
        // Nothing in the message row, and nothing sent: the reason was on the
        // tooltip the whole time.
        await expect(message(states)).toHaveCount(0);
        expect(await requests(page)).toEqual([]);
    });
});

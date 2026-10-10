import { expect, test } from '@playwright/test';
import { card, controls, message, open } from './panel';

// The states that are not states (principle 2: a state gets its place, and a
// place with nothing in it says so). The panel composes no text of its own:
// whatever it shows here is the core's `view.notice`, and a view that has no
// notice gets no row at all.
//
// There is no "not configured" state in the view: `view.notice` carries
// PANEL_NO_UNITS, PANEL_HOST_OFFLINE and PANEL_NOT_READY, and web/logic has no
// PANEL_NOT_CONFIGURED string to write one with. Nothing is composed for it
// here; the field that is missing is reported in the milestone report.
test.describe('states with nothing to show', () => {
    test('before the first view the frame is drawn, empty', async ({ page }) => {
        await open(page, { state: 'player', logic: 'none', width: 1200 });
        const panel = page.locator('.oams-panel');
        // The frame is there (the host's card is sized by the panel), with
        // nothing in it: an empty box, not a missing one.
        await expect(panel).toHaveCount(1);
        expect((await panel.innerText()).trim()).toBe('');
        // And nothing is faked: no card, no tile, no field, no dialog.
        await expect(panel.locator('.fps, .mmu, .lane, [role="status"], dialog[open]')).toHaveCount(0);
        // A dimmed control is not drawn either: there is nothing to act on.
        await expect(controls(panel)).toHaveCount(0);
        // The frame takes no room, because it holds nothing.
        expect((await panel.boundingBox())?.height ?? 0).toBe(0);
    });

    test('before the first view there is no spinner', async ({ page }) => {
        await open(page, { state: 'player', logic: 'none', width: 1200 });
        const panel = page.locator('.oams-panel');
        // No progress role, no busy state, and no element whose name says it
        // is loading anything.
        await expect(panel.locator('[role="progressbar"], [aria-busy="true"], .v-progress-linear, .spinner')).toHaveCount(0);
        expect(await panel.innerHTML()).not.toMatch(/spinner|loading|progress/i);
    });

    test('no units is the core\'s own sentence, in its own row', async ({ page }) => {
        await open(page, { state: 'player', fixture: 'two_lanes', width: 1200 });
        // The gallery's step slider replays the fixture; at step 0 the host has
        // fed the core nothing.
        await page.locator('#step').fill('0');
        const panel = page.locator('.oams-panel');
        await expect(message(panel)).toHaveText('No units connected');
        // Which is a sentence, not a spinner and not an empty frame.
        await expect(panel.locator('[role="progressbar"]')).toHaveCount(0);
    });

    test('an offline host is the core\'s own sentence too', async ({ page }) => {
        await open(page, { state: 'sheet', width: 1200 });
        const offline = card(page, 'offline');
        await expect(message(offline)).toHaveText('Host offline');
        await expect(offline.locator('[role="progressbar"]')).toHaveCount(0);
    });

    test('a view with no notice gets no row', async ({ page }) => {
        await open(page, { state: 'sheet', width: 1200 });
        // Every card but the offline one has something to draw and nothing to
        // say; the panel does not invent a row to fill the gap.
        for (const key of ['tile-states', 'idle', 'loading', 'tiles', 'drying']) {
            await expect(message(card(page, key)), key).toHaveCount(0);
        }
    });

    test('the panel\'s own text is only ever about an action it ran', async ({ page }) => {
        await open(page, { state: 'sheet', width: 1200 });
        // Every string the panel itself owns is a reason or a timeout, both of
        // which come from an action: nothing here is a state sentence.
        const panel = page.locator('.oams-panel').first();
        expect(await panel.innerText()).not.toMatch(/not configured|configure|no printer found/i);
    });
});

// PRINCIPLES.md 1: a tile's color is its spool's color, and chrome is neutral. A
// spool nobody told us the color of is drawn on the neutral surface; it is never
// a black fill, which would read as black filament.
test.describe('a spool with no known color', () => {
    test('is drawn on the neutral surface, not black', async ({ page }) => {
        // The recorded fixtures carry no spool summary, so a loaded bay has no color.
        await open(page, { state: 'player', fixture: 'loaded', width: 1200 });
        const lane = page.locator('.oams-panel .lane.state-loaded').first();
        await expect(lane).toHaveClass(/bare/);
        const [fill, surface] = await lane.evaluate((el) => {
            const probe = document.createElement('div');
            probe.style.background = 'var(--oams-surface-3)';
            el.appendChild(probe);
            const surface = getComputedStyle(probe).backgroundColor;
            probe.remove();
            return [getComputedStyle(el).backgroundColor, surface];
        });
        expect(fill).toBe(surface);
        expect(fill).not.toBe('rgb(0, 0, 0)');
    });

    test('is still black when it is known to be black', async ({ page }) => {
        await open(page, { state: 'sheet', width: 1200 });
        // oams21's neighbor in the state sheet is #1c1c1c ASA (the BLACK of the sheet).
        const lane = card(page, 'tile-states').locator('.lane[data-slot="oams11"]');
        await expect(lane).not.toHaveClass(/bare/);
        await expect(lane).toHaveClass(/filled/);
    });
});

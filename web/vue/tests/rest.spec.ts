import { expect, test } from '@playwright/test';
import { card, open } from './panel';

// PRINCIPLES.md 2, "A reserved slot has a resting state": the toolhead card's
// stepper slot is kept for something that comes and goes, and at rest it says
// something true and useful about the toolhead instead of sitting empty. At
// rest it shows the toolhead's own path in the stepper's form - one full-width
// segment in the loaded filament's color, labeled with what it is.
//
// The words and the color both come from the core (`activity.rest`); the panel
// draws them and composes nothing.

/** The slot of one card: the row the stepper lives in, whatever it is showing. */
function slot(scope: ReturnType<typeof card>) {
    return scope.locator('.status-steps');
}

/** The resting segment, the only child the slot holds while nothing runs. */
function resting(scope: ReturnType<typeof card>) {
    return scope.locator('.step.rest');
}

test.describe('the stepper slot at rest', () => {
    test('shows the loaded toolhead\'s path, in the filament\'s own color', async ({ page }) => {
        await open(page, { state: 'sheet', width: 1200 });
        const fps = card(page, 'idle');

        // The core's own words: the tool, the bay it came from and the
        // material on it.
        // The loaded path's words moved up to the header (panel-polish.spec.ts):
        // the segment keeps its color and does not repeat them.
        await expect(fps.locator('.fps-loaded')).toHaveText('T0 · oams1:1 · ASA');
        await expect(resting(fps).locator('span')).toHaveText('');
        // The segment is the filament's own color (the sheet's oams1-0 spool
        // is #1c1c1c), and it is the whole width: one segment, no more.
        const seg = resting(fps).locator('i');
        await expect(seg).toHaveCSS('background-color', 'rgb(28, 28, 28)');
        expect((await resting(fps).count())).toBe(1);
        const [segBox, slotBox] = await Promise.all([seg.boundingBox(), slot(fps).boundingBox()]);
        expect(Math.abs(segBox!.width - slotBox!.width)).toBeLessThanOrEqual(1);
    });

    test('with nothing loaded it is a gray segment that says what could be loaded', async ({ page }) => {
        await open(page, { state: 'sheet', width: 1200 });
        const fps = card(page, 'idle-unloaded');

        // The tools with a ready bay on this toolhead's units, in group order;
        // never "Nothing loaded", which the message row already says.
        await expect(resting(fps).locator('span')).toHaveText('Ready: T0 · T1 · T2');
        // Gray: the neutral track, the same color an empty step's segment is,
        // never a filament color.
        const [fill, track] = await resting(fps).locator('i').evaluate((el) => {
            const probe = document.createElement('div');
            probe.style.background = 'var(--oams-surface-3)';
            el.appendChild(probe);
            const track = getComputedStyle(probe).backgroundColor;
            probe.remove();
            return [getComputedStyle(el).backgroundColor, track];
        });
        expect(fill).toBe(track);
    });

    test('with nothing loaded and nothing ready it says the next step', async ({ page }) => {
        await open(page, { state: 'sheet', width: 1200 });
        const fps = card(page, 'idle-insert');

        await expect(resting(fps).locator('span')).toHaveText('Insert a spool');
    });

    test('a plan running replaces it, and the slot keeps its height', async ({ page }) => {
        await open(page, { state: 'sheet', width: 1200 });
        // The rule the owner cared about: the card's landmarks do not move.
        const heights = async (key: string) => (await slot(card(page, key)).boundingBox())!.height;
        expect(await heights('loading')).toBe(await heights('idle'));
        expect(await heights('loading')).toBe(await heights('idle-unloaded'));

        // And the resting segment is gone while the steps are drawn.
        const loading = card(page, 'loading');
        await expect(loading.locator('.step.rest')).toHaveCount(0);
        await expect(loading.locator('.step:not(.rest)')).toHaveCount(4);
    });

    test('the resting label survives a phone width, where the step labels do not', async ({ page }) => {
        await open(page, { state: 'sheet', width: 360 });
        const fps = card(page, 'idle');

        await expect(resting(fps).locator('span')).toBeVisible();
        // The loaded path's words moved up to the header (panel-polish.spec.ts):
        // the segment keeps its color and does not repeat them.
        await expect(fps.locator('.fps-loaded')).toHaveText('T0 · oams1:1 · ASA');
        await expect(resting(fps).locator('span')).toHaveText('');
        // The narrow-width current-step row has no step to name, so it adds no
        // height: the slot is still the row the resting path needs.
        const heights = async (key: string) => (await slot(card(page, key)).boundingBox())!.height;
        expect(await heights('idle')).toBe(await heights('loading'));
    });
});

// The toolhead header is three zones - the title, the pressure block, the alert
// slot - and each keeps its place when one of them has nothing to show. A
// missing pressure reading once removed the middle item from the grid and slid
// the alert slot into its column (principle 2).
test.describe('the toolhead header without a pressure reading', () => {
    for (const width of [1200, 700]) {
        test(`keeps the alert slot where it is (${width} px)`, async ({ page }) => {
            await open(page, { state: 'sheet', width });
            const withReading = card(page, 'idle');
            const without = card(page, 'no-pressure');
            await expect(withReading.locator('.pressure')).toHaveCount(1);
            await expect(without.locator('.pressure')).toHaveCount(0);

            const slot = (scope: ReturnType<typeof card>) => scope.locator('.fps-alert .alert-slot').boundingBox();
            const [a, b] = await Promise.all([slot(withReading), slot(without)]);
            expect(Math.round(b!.x - (await without.boundingBox())!.x))
                .toBe(Math.round(a!.x - (await withReading.boundingBox())!.x));
            expect(Math.round(b!.y - (await without.boundingBox())!.y))
                .toBe(Math.round(a!.y - (await withReading.boundingBox())!.y));
        });
    }
});

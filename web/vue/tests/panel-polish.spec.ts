import { expect, test } from '@playwright/test';
import { card, open } from './panel';

// The panel polish: the loaded spool's line, the actions beside their status,
// Stop's quiet outline, a tile's meta line, the empty bay's one quiet word and
// the regulator's pressure band. Every word comes from the core.

test.describe('the loaded filament in the toolhead header', () => {
    test('is a body-size line with a swatch, under the title', async ({ page }) => {
        await open(page, { state: 'sheet', width: 1200 });
        const fps = card(page, 'idle');
        const line = fps.locator('.fps-loaded');

        await expect(line).toHaveText('T0 · oams1:1 · ASA');
        await expect(line.locator('.fps-swatch')).toHaveCSS('background-color', 'rgb(28, 28, 28)');
        await expect(fps.locator('.fps-sub')).toHaveText('fps1');
        // the id is demoted, the loaded line is not
        const [sub, loaded] = await Promise.all([
            fps.locator('.fps-sub').evaluate((el) => parseFloat(getComputedStyle(el).fontSize)),
            line.evaluate((el) => parseFloat(getComputedStyle(el).fontSize)),
        ]);
        expect(loaded).toBeGreaterThan(sub);
        // the stepper's segment keeps its color and does not repeat the text
        await expect(fps.locator('.step.rest i')).toHaveCSS('background-color', 'rgb(28, 28, 28)');
        await expect(fps.locator('.step.rest span')).toHaveText('');
    });

    test('is absent when nothing is loaded, and the header height does not change', async ({ page }) => {
        await open(page, { state: 'sheet', width: 1200 });
        const loaded = card(page, 'idle');
        const unloaded = card(page, 'idle-unloaded');

        await expect(unloaded.locator('.fps-loaded')).toHaveAttribute('data-loaded', 'false');
        await expect(unloaded.locator('.fps-loaded .fps-swatch')).toHaveCount(0);
        await expect(unloaded.locator('.fps-loaded')).toHaveText('');
        // the stepper keeps saying what could be loaded
        await expect(unloaded.locator('.step.rest span')).toHaveText(/Ready/);
        const [a, b] = await Promise.all([
            loaded.locator('.fps-head').boundingBox(),
            unloaded.locator('.fps-head').boundingBox(),
        ]);
        expect(Math.abs(a!.height - b!.height)).toBeLessThanOrEqual(1);
    });

    test('keeps the pressure reading at its size', async ({ page }) => {
        await open(page, { state: 'sheet', width: 1200 });
        const size = await card(page, 'idle').locator('.pressure-val')
            .evaluate((el) => parseFloat(getComputedStyle(el).fontSize));
        expect(size).toBeGreaterThanOrEqual(20);
    });
});

test.describe('the toolhead actions', () => {
    test('sit in the status row, right of the message and above the units', async ({ page }) => {
        await open(page, { state: 'sheet', width: 1200 });
        const fps = card(page, 'idle');
        const row = fps.locator('.status-row');

        await expect(row.locator('.th-actions [data-action="unload"]')).toHaveCount(1);
        await expect(row.locator('.th-actions [data-action="stop"]')).toHaveCount(1);
        const [message, actions, units] = await Promise.all([
            row.locator('.message').boundingBox(),
            row.locator('.th-actions').boundingBox(),
            fps.locator('.mmus').boundingBox(),
        ]);
        // same row: the actions' middle is within the message's height
        const mid = actions!.y + actions!.height / 2;
        expect(mid).toBeGreaterThan(message!.y);
        expect(mid).toBeLessThan(message!.y + message!.height);
        expect(actions!.x).toBeGreaterThan(message!.x);
        expect(actions!.y + actions!.height).toBeLessThanOrEqual(units!.y);
    });

    test('wrap under the message on a narrow card, still above the units', async ({ page }) => {
        await open(page, { state: 'sheet', width: 420 });
        const fps = card(page, 'idle');
        const [message, actions, units] = await Promise.all([
            fps.locator('.status-row .message').boundingBox(),
            fps.locator('.th-actions').boundingBox(),
            fps.locator('.mmus').boundingBox(),
        ]);
        expect(actions!.y).toBeGreaterThanOrEqual(message!.y + message!.height - 1);
        expect(actions!.y + actions!.height).toBeLessThanOrEqual(units!.y);
    });
});

test.describe('Stop is identifiable before it is urgent', () => {
    const border = (el: Element): string => getComputedStyle(el).borderTopColor;

    test('Stop has an error-toned outline, Unload stays neutral, enabled is stronger', async ({ page }) => {
        await open(page, { state: 'sheet', width: 1200 });
        const dimmed = card(page, 'idle').locator('.th-actions [data-action="stop"]');
        const enabled = card(page, 'heating').locator('.th-actions [data-action="stop"]');
        const unload = card(page, 'idle').locator('.th-actions [data-action="unload"]');

        await expect(dimmed).toHaveAttribute('data-enabled', 'false');
        await expect(enabled).toHaveAttribute('data-enabled', 'true');
        const [d, e, u] = await Promise.all([dimmed.evaluate(border), enabled.evaluate(border), unload.evaluate(border)]);
        // computed colors come back as rgb(), rgba() or color(srgb 0-1 ... / a)
        const parse = (c: string): { r: number; g: number; b: number; a: number } => {
            const n = c.replace('/', ' ').match(/[\d.]+/g)!.map(Number);
            const k = c.startsWith('color(') ? 255 : 1;
            return { r: n[0] * k, g: n[1] * k, b: n[2] * k, a: n[3] ?? 1 };
        };
        const alpha = (c: string): number => parse(c).a;
        // error tone: red dominates; unload's border is the neutral line
        const red = (c: string): boolean => {
            const { r, g, b } = parse(c);
            return r > g + 40 && r > b + 40;
        };
        expect(red(d)).toBe(true);
        expect(red(e)).toBe(true);
        expect(red(u)).toBe(false);
        expect(alpha(e)).toBeGreaterThan(alpha(d));
        // the text is toned too, enabled or not
        const text = await dimmed.evaluate((el) => getComputedStyle(el).color);
        expect(red(text)).toBe(true);
    });
});

test.describe('the tile meta line', () => {
    test('says the color name, not the material twice', async ({ page }) => {
        await open(page, { state: 'sheet', width: 1200 });
        const states = card(page, 'tile-states');
        const red = states.locator('.lane[data-state="ready"]:has(.lane-meta .g:text("460"))').first();

        await expect(red.locator('.lane-name')).toHaveText('ASA');
        await expect(red.locator('.lane-meta .m-first')).toHaveText('red');
        await expect(red.locator('.lane-meta .g')).toHaveText('460 g');
        await expect(red.locator('.lane-meta')).not.toContainText('ASA');
        await expect(red.locator('.lane-meta .low')).toHaveCount(0);
    });

    test('a low spool says grams and the Low word, in the amber tone', async ({ page }) => {
        await open(page, { state: 'sheet', width: 1200 });
        const low = card(page, 'tile-states').locator('.lane:has(.ring.low)').first();

        await expect(low.locator('.lane-meta .g')).toHaveText('120 g');
        await expect(low.locator('.lane-meta .low')).toHaveText('Low');
        await expect(low.locator('.lane-meta .low')).toBeVisible();
        await expect(low.locator('.lane-meta .low')).toHaveCSS('color', 'rgb(255, 210, 122)');
        await expect(low.locator('.lane-meta')).not.toContainText('ASA');
    });

    test('has no color name when the color is unknown', async ({ page }) => {
        await open(page, { state: 'sheet', width: 1200 });
        const unknown = card(page, 'tile-states').locator('.lane.state-unknown .lane-meta');

        await expect(unknown.first().locator('.m-first')).toHaveCount(0);
    });

    test('drops the color name first, then Low, and keeps the grams', async ({ page }) => {
        await open(page, { state: 'sheet', width: 420 });
        const states = card(page, 'tile-states');
        const red = states.locator('.lane[data-state="ready"]:has(.lane-meta .g:text("460"))').first();
        const low = states.locator('.lane:has(.ring.low)').first();
        const width = await low.evaluate((el) => el.parentElement!.getBoundingClientRect().width);

        expect(width).toBeLessThan(100);
        // The meta line never overflows its tile: whatever does not fit has
        // been dropped (the color name first), and under 92 px the color name
        // is never drawn.
        const fits = await red.locator('.lane-meta').evaluate((el) => el.scrollWidth <= el.clientWidth);
        expect(fits).toBe(true);
        if (width <= 92) await expect(red.locator('.lane-meta .m-first')).toBeHidden();
        await expect(red.locator('.lane-meta .g')).toBeVisible();
        await expect(low.locator('.lane-meta .g')).toBeVisible();
        if (width <= 80) await expect(low.locator('.lane-meta .low')).toBeHidden();
    });
});

test.describe('the empty bay', () => {
    test('says Empty once, as quiet text, with no "?"', async ({ page }) => {
        await open(page, { state: 'sheet', width: 1200 });
        const empty = card(page, 'tile-states').locator('.lane.state-empty').first();
        const tag = empty.locator('.status-tag');

        await expect(tag).toHaveText('Empty');
        await expect(tag).toHaveClass(/quiet/);
        await expect(tag).toHaveCSS('background-color', 'rgba(0, 0, 0, 0)');
        await expect(tag).toHaveCSS('box-shadow', 'none');
        // the label keeps its box (heights align) but nothing is drawn
        const id = empty.locator('.lane-id');
        await expect(id).toHaveCSS('visibility', 'hidden');
        const box = await id.boundingBox();
        expect(box!.height).toBeGreaterThan(0);
        await expect(empty).toHaveClass(/hatched/);
    });

    test('other states keep their pill', async ({ page }) => {
        await open(page, { state: 'sheet', width: 1200 });
        const err = card(page, 'tile-states').locator('.lane.state-error .status-tag').first();

        await expect(err).not.toHaveClass(/quiet/);
        await expect(err).not.toHaveCSS('background-color', 'rgba(0, 0, 0, 0)');
    });
});

test.describe('the pressure band', () => {
    test('is a quiet segment of the track when the host reports one', async ({ page }) => {
        await open(page, { state: 'sheet', width: 1200 });
        const bar = card(page, 'idle').locator('.pressure-bar');
        const band = bar.locator('.band');

        await expect(band).toHaveCount(1);
        const [b, t] = await Promise.all([band.boundingBox(), bar.boundingBox()]);
        expect(Math.abs((b!.x - t!.x) / t!.width - 0.3)).toBeLessThan(0.02);
        expect(Math.abs(b!.width / t!.width - 0.4)).toBeLessThan(0.02);
    });

    test('is absent when the host reports none', async ({ page }) => {
        await open(page, { state: 'sheet', width: 1200 });
        await expect(card(page, 'no-band').locator('.pressure-bar')).toHaveCount(1);
        await expect(card(page, 'no-band').locator('.pressure-bar .band')).toHaveCount(0);
    });

    test('never changes the fill or the number color, whatever the reading', async ({ page }) => {
        await open(page, { state: 'sheet', width: 1200 });
        const colors = async (key: string): Promise<string[]> => {
            const fps = card(page, key);
            return fps.locator('.pressure').evaluate((el) => [
                getComputedStyle(el.querySelector('.pressure-bar > i')!).backgroundColor,
                getComputedStyle(el.querySelector('.pressure-val')!).color,
            ]);
        };
        // loading reads 0.74 (above the band), no-band 0.85, idle 0.50 (inside it), load-failed 0.93
        const inside = await colors('idle');
        for (const key of ['loading', 'no-band', 'load-failed']) expect(await colors(key)).toEqual(inside);
    });
});

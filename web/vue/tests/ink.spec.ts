import { expect, test, type Locator } from '@playwright/test';
import { card, clickTile, dialog, open, openPopover, settled, target, tile, type Theme } from './panel';

// A popover and a dialog read in their own ink, never in the ink of what they
// are anchored to (UNIFIED_UI.md 7; PRINCIPLES.md 12, the 4.5:1 floor). A
// tile's menu and its tag's details sit inside the tile in the document, and a
// light spool's tile draws its text dark: that ink must not reach the dark base
// of a popover. The state sheet's oams34 is a light spool and oams23 a light
// spool in error, both drawn with dark ink.

const themes: Theme[] = ['dark', 'light', 'host-gray', 'host-white'];

type Rgba = [number, number, number, number];

/** What the browser paints for `text` over `surface`'s background: both
 *  computed colors are drawn on a canvas, the text over the background, so
 *  color-mix(), alpha and every color syntax resolve exactly as painted. */
async function painted(text: Locator, surface: Locator): Promise<{ ink: Rgba; base: Rgba }> {
    const bg = await surface.evaluate((el) => getComputedStyle(el).backgroundColor);
    const fg = await text.evaluate((el) => getComputedStyle(el).color);
    return text.page().evaluate(([b, f]) => {
        const c = document.createElement('canvas');
        c.width = c.height = 1;
        const ctx = c.getContext('2d', { willReadFrequently: true })!;
        const read = (): [number, number, number, number] => {
            const d = ctx.getImageData(0, 0, 1, 1).data;
            return [d[0], d[1], d[2], d[3]];
        };
        ctx.fillStyle = '#808080';   // a popover's base is opaque; this never shows through
        ctx.fillRect(0, 0, 1, 1);
        ctx.fillStyle = b;
        ctx.fillRect(0, 0, 1, 1);
        const base = read();
        ctx.fillStyle = f;
        ctx.fillRect(0, 0, 1, 1);
        return { ink: read(), base };
    }, [bg, fg] as const);
}

const luminance = ([r, g, b]: Rgba): number => {
    const lin = (v: number) => {
        const s = v / 255;
        return s <= 0.04045 ? s / 12.92 : ((s + 0.055) / 1.055) ** 2.4;
    };
    return 0.2126 * lin(r) + 0.7152 * lin(g) + 0.0722 * lin(b);
};

async function contrast(text: Locator, surface: Locator): Promise<number> {
    const { ink, base } = await painted(text, surface);
    const [a, b] = [luminance(ink), luminance(base)].sort((x, y) => y - x);
    return (a + 0.05) / (b + 0.05);
}

test.describe('popover ink', () => {
    for (const theme of themes) {
        test(`a light spool's menu reads on the popover, in ${theme}`, async ({ page }) => {
            await open(page, { state: 'sheet', theme, width: 1200 });
            const states = card(page, 'tile-states');
            await expect(tile(states, 'oams34')).toHaveClass(/ink-dark/);
            await clickTile(states, 'oams34');
            const menu = openPopover(page, 'tile');
            await expect(menu).toBeVisible();
            await settled(page);
            // Every row, dimmed or not, holds the floor on the popover's own base.
            for (const label of await menu.locator('.action-label').all()) {
                expect(await contrast(label, menu), await label.textContent() ?? '').toBeGreaterThanOrEqual(4.5);
            }
            // The popover's own ink, whatever the tile: the same as a dark
            // spool's menu (oams12 draws light ink), and no tile's text shadow.
            const ink = await menu.evaluate((el) => getComputedStyle(el).color);
            expect(await menu.evaluate((el) => getComputedStyle(el).textShadow)).toBe('none');
            await page.keyboard.press('Escape');
            await expect(tile(states, 'oams12')).toHaveClass(/ink-light/);
            await clickTile(states, 'oams12');
            const other = openPopover(page, 'tile');
            await expect(other).toBeVisible();
            expect(await other.evaluate((el) => getComputedStyle(el).color)).toBe(ink);
        });

        test(`a light spool's tag details read on the popover, in ${theme}`, async ({ page }) => {
            await open(page, { state: 'sheet', theme, width: 1200 });
            const states = card(page, 'tile-states');
            await expect(tile(states, 'oams23')).toHaveClass(/ink-dark/);
            await target(states, 'oams23', 'tag').click();
            const tag = openPopover(page, 'tag');
            await expect(tag).toBeVisible();
            await settled(page);
            expect(await contrast(tag.locator('.tag-detail'), tag)).toBeGreaterThanOrEqual(4.5);
            for (const label of await tag.locator('.action-label').all()) {
                expect(await contrast(label, tag)).toBeGreaterThanOrEqual(4.5);
            }
        });

        test(`a dialog opened from a light spool reads on its own base, in ${theme}`, async ({ page }) => {
            await open(page, { state: 'sheet', theme, width: 1200 });
            const states = card(page, 'tile-states');
            await clickTile(states, 'oams34');
            await openPopover(page, 'tile').locator('.action[data-action="edit_spool"]').click();
            const dlg = dialog(page);
            await expect(dlg).toBeVisible();
            expect(await contrast(dlg.locator('.dlg-head'), dlg)).toBeGreaterThanOrEqual(4.5);
            expect(await contrast(dlg.locator('.dlg-field label').first(), dlg)).toBeGreaterThanOrEqual(4.5);
        });
    }
});

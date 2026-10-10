import AxeBuilder from '@axe-core/playwright';
import { expect, test, type Locator, type Page } from '@playwright/test';
import {
    action, activeOption, card, chosen, clickRing, dialog, open, openList, openPopover, openSelect,
    options, requests, select, type Theme,
} from './panel';

// A select field of our own, in place of the native <select> (PRINCIPLES.md 1,
// 12): the browser paints a native list in its own colors whatever the theme,
// and lights the title's option rather than the one under the pointer. What
// replaces it is an ARIA listbox in a popover, with the panel's own tokens.

/** The ring's "Edit spool" form: three selects, two numbers and a color list,
 *  which is every shape the control has to carry. */
const openForm = async (page: Page, slot = 'oams11'): Promise<Locator> => {
    const states = card(page, 'tile-states');
    await clickRing(states, slot);
    await action(openPopover(page, 'tile'), 'edit_spool').click();
    return dialog(page);
};

/** Pick an option by the name the core gave it, with the pointer, opening the
 *  list on the way. The button that opens a list is also what shuts it, so a
 *  list that is already open is chosen from where it is. */
const pick = async (dlg: Locator, field: string, label: string): Promise<void> => {
    const already = await openList(dlg, field).isVisible();
    const list = already ? openList(dlg, field) : (await openSelect(dlg, field)).list;
    await list.getByRole('option', { name: label, exact: true }).click();
    await expect(openList(dlg, field)).toBeHidden();
};

/** A form's own submit button. */
const submitOf = (page: Page): Locator => page.locator('.oams-panel dialog[open] [data-action="edit_spool"]');

test.describe('a select field', () => {
    /** The list's own painted background as three 0-255 channels. Chromium
     *  writes a resolved `color-mix()` as `color(srgb r g b)` in 0..1 and a
     *  plain color as `rgb(r, g, b)`, so both are read here. */
    const backdrop = async (list: Locator): Promise<number[]> =>
        list.evaluate((el) => {
            const value = getComputedStyle(el).backgroundColor;
            const parts = (value.match(/[\d.]+/g) ?? []).map(Number);
            return value.startsWith('color(') ? parts.map((c) => Math.round(c * 255)) : parts.slice(0, 3);
        });

    test.beforeEach(async ({ page }) => {
        await open(page, { state: 'sheet', width: 1200 });
    });

    test('the list is the panel\'s own card, and follows the theme', async ({ page }) => {
        for (const { theme, dark } of [{ theme: 'dark', dark: true }, { theme: 'light', dark: false }] as
            { theme: Theme; dark: boolean }[]) {
            await open(page, { state: 'sheet', theme, width: 1200 });
            const dlg = await openForm(page);
            const { list } = await openSelect(dlg, 'material');
            const [r, g, b] = await backdrop(list);
            // The theme's own base, not the browser's canvas: dark is dark in
            // the dark theme and the list is light in the light one.
            if (dark) expect(Math.max(r, g, b), `dark list in ${theme}`).toBeLessThan(64);
            else expect(Math.min(r, g, b), `light list in ${theme}`).toBeGreaterThan(200);
        }
    });

    test('hover lights the option under the pointer, not the first', async ({ page }) => {
        const dlg = await openForm(page);
        const { list } = await openSelect(dlg, 'color');
        // On open it is the chosen option that is lit; the pointer moves it.
        await expect(activeOption(list)).toHaveAttribute('data-index', '18');
        await options(list).nth(9).hover();
        await expect(activeOption(list)).toHaveAttribute('data-index', '9');
        // And it is painted, not only marked: the lit option has a background
        // of its own and the ones around it have none.
        const lit = await options(list).nth(9).evaluate((el) => getComputedStyle(el).backgroundColor);
        const unlit = await options(list).nth(10).evaluate((el) => getComputedStyle(el).backgroundColor);
        expect(lit).not.toBe(unlit);
        expect(lit).not.toBe('rgba(0, 0, 0, 0)');
    });

    test('choosing and reopening lights the option that was chosen', async ({ page }) => {
        const dlg = await openForm(page);
        const { list } = await openSelect(dlg, 'vendor');
        // Generic, Bambu Lab, Polymaker, Prusament, ...: the fourth is Prusament.
        await options(list).nth(3).click();
        await expect(openList(dlg, 'vendor')).toBeHidden();
        expect(await chosen(dlg, 'vendor')).toBe('Prusament');
        // The list opens on the choice, not on the first row.
        const again = await openSelect(dlg, 'vendor');
        await expect(activeOption(again.list)).toHaveAttribute('data-index', '3');
        await expect(options(again.list).nth(3)).toHaveAttribute('aria-selected', 'true');
        expect(await requests(page)).toEqual([]);
    });

    test('a color field draws a swatch on every option, and on the button', async ({ page }) => {
        const dlg = await openForm(page, 'oams21');
        const { list } = await openSelect(dlg, 'color');
        const rows = options(list);
        expect(await rows.count()).toBeGreaterThan(10);
        // Every option carries one: a color is content, not a word (principle 6).
        await expect(rows.locator('.swatch')).toHaveCount(await rows.count());
        // The core's own encoding, read back: 0xc12e1f is red.
        await expect(rows.filter({ hasText: /^red$/ }).locator('.swatch'))
            .toHaveCSS('background-color', 'rgb(193, 46, 31)');
        // And the button shows the chosen swatch, not only the list.
        await pick(dlg, 'color', 'red');
        await expect(select(dlg, 'color').locator('.swatch')).toHaveCSS('background-color', 'rgb(193, 46, 31)');
        // A field that is not a color field draws none at all: the core's
        // materials are PLA, PETG, ABS, ASA, TPU, ... and the fourth is ASA.
        const material = await openSelect(dlg, 'material');
        await expect(options(material.list).nth(3)).toHaveText('ASA');
        await expect(options(material.list).locator('.swatch')).toHaveCount(0);
    });

    test('"Not set" is the design\'s empty look, never a color', async ({ page }) => {
        // oams21 is the bay nobody identified, so its color is -1.
        const dlg = await openForm(page, 'oams21');
        expect(await chosen(dlg, 'color')).toBe('Not set');
        const { list } = await openSelect(dlg, 'color');
        const unset = options(list).filter({ hasText: /^Not set$/ }).locator('.swatch');
        // Hatched, like an empty bay, and no color of its own behind it.
        await expect(unset).toHaveClass(/swatch-unset/);
        await expect(unset).not.toHaveAttribute('style', /background/);
        await expect(unset).toHaveCSS('background-image', /repeating-linear-gradient/);
    });

    test('the keys choose: arrows, Home and End', async ({ page }) => {
        const dlg = await openForm(page);
        const button = select(dlg, 'material');
        await button.focus();
        // Closed, a down arrow opens the list on the option that is chosen: ASA, the
        // fourth of the core's materials.
        await page.keyboard.press('ArrowDown');
        await expect(openList(dlg, 'material')).toBeVisible();
        const list = openList(dlg, 'material');
        await expect(activeOption(list)).toHaveAttribute('data-index', '3');
        await page.keyboard.press('ArrowDown');
        await expect(activeOption(list)).toHaveAttribute('data-index', '4');
        await page.keyboard.press('End');
        await expect(activeOption(list)).toHaveAttribute('data-index', '9');
        await page.keyboard.press('Home');
        await expect(activeOption(list)).toHaveAttribute('data-index', '0');
        // And wraps, as the control it replaces does.
        await page.keyboard.press('ArrowUp');
        await expect(activeOption(list)).toHaveAttribute('data-index', '9');
    });

    test('Enter and Space choose, and the field says what it now holds', async ({ page }) => {
        const dlg = await openForm(page);
        const button = select(dlg, 'material');
        await button.focus();
        await page.keyboard.press('ArrowDown');
        await page.keyboard.press('ArrowDown');
        await page.keyboard.press('Enter');
        await expect(openList(dlg, 'material')).toBeHidden();
        expect(await chosen(dlg, 'material')).toBe('TPU');
        // Space opens the list the same way, and a space in the open list
        // chooses what the keys have walked to.
        await button.focus();
        await page.keyboard.press(' ');
        await expect(openList(dlg, 'material')).toBeVisible();
        await page.keyboard.press('ArrowDown');
        await page.keyboard.press(' ');
        await expect(openList(dlg, 'material')).toBeHidden();
        expect(await chosen(dlg, 'material')).toBe('PA');
        expect(await requests(page)).toEqual([]);
    });

    test('Escape closes the list and leaves the field alone', async ({ page }) => {
        const dlg = await openForm(page);
        const { list } = await openSelect(dlg, 'material');
        await options(list).nth(0).hover();
        await page.keyboard.press('Escape');
        await expect(openList(dlg, 'material')).toBeHidden();
        await expect(dialog(page)).toBeVisible();
        expect(await chosen(dlg, 'material')).toBe('ASA');
    });

    test('a letter jumps to the option that starts with it', async ({ page }) => {
        const dlg = await openForm(page, 'oams21');
        const button = select(dlg, 'color');
        await button.focus();
        // Closed, typing opens the list on the match; 'f' is "forest green".
        await page.keyboard.press('f');
        const list = openList(dlg, 'color');
        await expect(activeOption(list)).toHaveText(/forest green/);
        await page.keyboard.press('p');
        await expect(activeOption(list)).toHaveText(/purple/);
        await page.keyboard.press('Enter');
        expect(await chosen(dlg, 'color')).toBe('purple');
    });

    test('a click outside closes the list, and the dialog stays', async ({ page }) => {
        const dlg = await openForm(page);
        await openSelect(dlg, 'material');
        await dlg.locator('.dlg-head').click();
        await expect(openList(dlg, 'material')).toBeHidden();
        await expect(dialog(page)).toBeVisible();
        expect(await chosen(dlg, 'material')).toBe('ASA');
    });

    test('a long list scrolls inside itself and stays on the screen', async ({ page }) => {
        await page.setViewportSize({ width: 1200, height: 460 });
        const dlg = await openForm(page, 'oams21');
        const { list } = await openSelect(dlg, 'color');
        const box = await list.boundingBox();
        // Nineteen palette rows do not fit under the field, so the list takes
        // the room there is and scrolls the rest (see popover.ts, which caps it).
        expect(await list.evaluate((el) => el.scrollHeight > el.clientHeight)).toBe(true);
        expect(box!.y).toBeGreaterThanOrEqual(0);
        expect(box!.y + box!.height).toBeLessThanOrEqual(460);
        // And the option that is chosen is scrolled into view, not left below.
        const chosenAt = await list.evaluate((el) => {
            const option = el.querySelector('[aria-selected="true"]') as HTMLElement;
            const rows = el.getBoundingClientRect();
            const row = option.getBoundingClientRect();
            return row.top >= rows.top - 1 && row.bottom <= rows.bottom + 1;
        });
        expect(chosenAt).toBe(true);
    });

    test('the list is drawn over the dialog, not inside it', async ({ page }) => {
        const dlg = await openForm(page, 'oams21');
        const { list } = await openSelect(dlg, 'color');
        // It is in the top layer above the dialog: whatever is painted at an
        // option's own middle is that option, not the dialog behind it.
        const onTop = await options(list).nth(2).evaluate((el) => {
            const box = el.getBoundingClientRect();
            const hit = document.elementFromPoint(box.left + box.width / 2, box.top + box.height / 2);
            return !!hit?.closest('[data-list="color"]');
        });
        expect(onTop).toBe(true);
    });

    test('an open list meets every rule, contrast included', async ({ page }) => {
        await open(page, { state: 'sheet', theme: 'light', width: 1200 });
        const dlg = await openForm(page, 'oams21');
        const { list } = await openSelect(dlg, 'color');
        const results = await new AxeBuilder({ page }).include(`[data-list="color"]:popover-open`).analyze();
        const blocking = results.violations.filter((v) => v.impact === 'serious' || v.impact === 'critical');
        expect(blocking.map((v) => v.id)).toEqual([]);
        expect(await list.getAttribute('role')).toBe('listbox');
    });
});

// The submit button: the core's own words, and a form that cannot be sent
// without a change (PRINCIPLES.md 8: a disabled action says why; 11: the task
// happens where the user is). Both come from the core's view model, which the
// gallery can stamp (gallery/forms.ts) until the core writes them.
test.describe('the submit button', () => {
    test('the action\'s own label, and always live, when the core names none', async ({ page }) => {
        await open(page, { state: 'sheet', width: 1200, form: 'plain' });
        await openForm(page);
        await expect(submitOf(page)).toHaveText('Edit spool');
        await expect(submitOf(page)).toBeEnabled();
    });

    test('the edit form: the core\'s "Save edit", waiting for a change', async ({ page }) => {
        await open(page, { state: 'sheet', width: 1200 });
        await openForm(page);
        await expect(submitOf(page)).toHaveText('Save edit');
        await expect(submitOf(page)).toBeDisabled();
    });

    test('the form\'s own label, when the core gives one', async ({ page }) => {
        await open(page, { state: 'sheet', width: 1200, form: 'require-change' });
        await openForm(page);
        await expect(submitOf(page)).toHaveText('Save changes');
    });

    test('a form that asks for a change cannot be sent without one', async ({ page }) => {
        await open(page, { state: 'sheet', width: 1200, form: 'require-change' });
        const dlg = await openForm(page);
        await expect(submitOf(page)).toBeDisabled();
        // One field changed is enough.
        await pick(dlg, 'color', 'red');
        await expect(submitOf(page)).toBeEnabled();
    });

    test('putting the field back makes it dead again', async ({ page }) => {
        await open(page, { state: 'sheet', width: 1200, form: 'require-change' });
        const dlg = await openForm(page);
        await pick(dlg, 'color', 'red');
        await expect(submitOf(page)).toBeEnabled();
        await pick(dlg, 'color', '#1C1C1C');
        await expect(submitOf(page)).toBeDisabled();
        // A number counts as a change too.
        await dlg.locator('[data-field="remaining"]').fill('640');
        await expect(submitOf(page)).toBeEnabled();
        await dlg.locator('[data-field="remaining"]').fill('1000');
        await expect(submitOf(page)).toBeDisabled();
        expect(await requests(page)).toEqual([]);
    });
});
test.describe('a spool list draws the filament colors', () => {
    const openAssign = async (page: Page): Promise<Locator> => {
        await page.goto('/gallery.html?mode=sheet&theme=dark&width=1200&spools=1');
        await page.waitForSelector('#stage[data-ready="true"]');
        await clickRing(card(page, 'tile-states'), 'oams11');
        await action(openPopover(page, 'tile'), 'assign_spool').click();
        return dialog(page);
    };

    test('a known color is the fill, an unknown one the empty look', async ({ page }) => {
        const dlg = await openAssign(page);
        const { list } = await openSelect(dlg, 'spool');
        const rows = options(list);
        await expect(rows).toHaveCount(3);
        await expect(rows.locator('.swatch')).toHaveCount(3);
        await expect(rows.locator('.swatch').nth(0)).toHaveCSS('background-color', 'rgb(193, 46, 31)');
        await expect(rows.locator('.swatch').nth(1)).toHaveCSS('background-color', 'rgb(47, 140, 240)');
        const unknown = rows.locator('.swatch').nth(2);
        await expect(unknown).toHaveClass(/swatch-unset/);
        await expect(unknown).not.toHaveAttribute('style', /background/);
        await expect(unknown).toHaveAttribute('aria-hidden', 'true');
        // The closed button shows the chosen one's swatch.
        await rows.nth(1).click();
        await expect(select(dlg, 'spool').locator('.swatch')).toHaveCSS('background-color', 'rgb(47, 140, 240)');
    });
});

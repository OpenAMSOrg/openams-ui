import { expect, test } from '@playwright/test';
import { card, open, openPopover } from './panel';

// The Language setting: the one row of the settings popover that is a choice
// rather than a switch (UNIFIED_UI.md 7, 12). It sends no action - picking a
// language asks the core for another one and draws the view again, so the same
// set of translations serves every renderer (PRINCIPLES.md 5).

/** The languages the build carries, each by its own name. The names are never
 *  translated: the list has to be readable by the people who read it. */
const NAMES = [
    'English',
    'Deutsch',
    'Español',
    'Français',
    'Italiano',
    '日本語',
    '한국어',
    'Polski',
    'Português (Brasil)',
    'Русский',
    '简体中文',
];

/** The settings popover of the first card of the sheet, open. */
async function settings(page: import('@playwright/test').Page) {
    const bar = card(page, 'tile-states').locator('[data-popover-invoker="settings"]');
    await bar.click();
    const pop = card(page, 'tile-states').locator('[data-popover="settings"]:popover-open');
    await expect(pop).toBeVisible();
    return pop;
}

test.describe('the Language setting', () => {
    test.beforeEach(async ({ page }) => {
        await open(page, { state: 'sheet', width: 1200 });
    });

    test('the last row lists every language by its own name', async ({ page }) => {
        const row = (await settings(page)).locator('[data-setting="language"]');
        expect(await row.locator('.choice-label').allTextContents()).toEqual(NAMES);
        // A choice, not a switch: a radio group, and only one answer ticked.
        await expect(row.locator('[role="radiogroup"]')).toHaveCount(1);
        await expect(row.locator('[role="radio"][aria-checked="true"]')).toHaveCount(1);
        // English is what the image starts in, and it says so.
        await expect(row.locator('[data-code="en"]')).toHaveAttribute('aria-checked', 'true');
        await expect(row.locator('.row-state')).toHaveText('English');
    });

    test('picking a language redraws the panel in it', async ({ page }) => {
        const pop = await settings(page);
        const states = card(page, 'tile-states');
        const before = await states.innerText();
        await pop.locator('[data-code="de"]').click();
        // The words changed, and so did the page: the language is the gallery's
        // as much as the panel's, and the page's tag is what picks the font.
        expect(await states.innerText()).not.toBe(before);
        await expect(page.locator('html')).toHaveAttribute('lang', 'de');
        await expect(page.locator('#lang')).toHaveValue('de');
        // The core's own words, on the tile that reports an error.
        await expect(states.locator('.status-tag', { hasText: 'Fehler' })).toBeVisible();
        // The row itself is now in the language that was picked, and the choice
        // it holds is the German name, not the code.
        await expect(pop.locator('[data-setting="language"] .row-state')).toHaveText('Deutsch');
        await expect(pop.locator('[data-code="de"]')).toHaveAttribute('aria-checked', 'true');
    });

    test('the language is module state: one panel switches them all', async ({ page }) => {
        await (await settings(page)).locator('[data-code="fr"]').click();
        // Nothing the printer sent changed, so every other panel is still the
        // same panel - only the words it is drawn with are French now.
        await expect(card(page, 'idle')).toContainText('Charger');
    });

    test('the gallery can be opened in any language', async ({ page }) => {
        await open(page, { state: 'sheet', width: 1200, lang: 'ja' });
        await expect(page.locator('html')).toHaveAttribute('lang', 'ja');
        await expect(card(page, 'tile-states').getByText('位置調整中').first()).toBeVisible();
        // And the switch in the gallery's own bar moves with it.
        await expect(page.locator('#lang')).toHaveValue('ja');
        await page.locator('#lang').selectOption('ko');
        await expect(page.locator('html')).toHaveAttribute('lang', 'ko');
        await expect(card(page, 'tile-states').getByText('위치 조정 중').first()).toBeVisible();
    });

    test('a language this build does not carry is not offered, and not drawn', async ({ page }) => {
        // The pseudo-locale is in the image but not in the list: it is for
        // tests, not for a user.
        const row = (await settings(page)).locator('[data-setting="language"]');
        await expect(row.locator('[data-code="qps"]')).toHaveCount(0);
        // An unknown code in the query is ignored rather than half-applied.
        await open(page, { state: 'sheet', width: 1200, lang: 'xx' });
        await expect(page.locator('html')).toHaveAttribute('lang', 'en');
        await expect(page.locator('#lang')).toHaveValue('en');
    });
});

// The gallery tags its own page, so a review in any language is a review in
// that language's font. In a host the panel names its own language too: the
// page's lang describes the page, and the panel's words are in the panel's.
test.describe('the panel names its own language in a host', () => {
    test('a host card carries the language the panel draws in', async ({ page }) => {
        await page.goto('/gallery.html?mode=sheet&width=1200&theme=host-gray');
        await page.waitForSelector('#stage[data-ready="true"]');
        // Inside a v-app (what Mainsail gives the panel) the panel still says
        // which language its own text is in.
        await expect(page.locator('.oams-panel').first()).toHaveAttribute('lang', 'en');
    });

    test('and the settings popover is not drawn in a host card', async ({ page }) => {
        await page.goto('/gallery.html?mode=sheet&width=1200&theme=host-gray');
        await page.waitForSelector('#stage[data-ready="true"]');
        // The bar is the standalone page's; in a host the host's own bar shows
        // the settings, so the panel draws none (never two ways to do one thing).
        await expect(page.locator('[data-popover-invoker="settings"]')).toHaveCount(0);
        await expect(openPopover(page, 'settings')).toHaveCount(0);
    });
});

import { expect, test, type Page } from '@playwright/test';
import { card } from './panel';

// The panel inside a host speaks the host's language (PRINCIPLES.md 6: inside a
// host, the host's surfaces and fonts are used - we are a guest). A Mainsail
// page carries `lang="en"` whatever language the user picked, so a host hands
// the panel the language to draw in through its `language` prop. The gallery
// hands it over `?hostlang=`, which is the one seam here: a Mainsail wrapper
// reads the locale off its own vue-i18n instead.

/** The gallery in host mode, with the language a host panel is handed. The
 *  gallery's own `?lang=` is left at English, because that is the standalone
 *  page's own choice and a host panel never has one. */
async function host(page: Page, code: string): Promise<void> {
    await page.goto(`/gallery.html?mode=sheet&theme=host-gray&width=1200&hostlang=${code}`);
    await page.waitForSelector('#stage[data-ready="true"]');
}

/** The panel's own root: `lang` on it is what the browser and a font stack
 *  read, so the whole of "which language is this drawn in" is on it. */
function panelRoot(page: Page) {
    return page.locator('.oams-panel').first();
}

test.describe('a panel handed a language by its host', () => {
    test('draws in that language, and carries it', async ({ page }) => {
        await host(page, 'de');
        // The core's own words, on the tile that reports an error.
        await expect(card(page, 'tile-states').locator('.status-tag', { hasText: 'Fehler' })).toBeVisible();
        // The panel says which language it is in, and the page around it cannot
        // talk it out of that: a Mainsail page is `lang="en"` whatever the
        // user picked, and the panel's text is not the page's.
        await expect(panelRoot(page)).toHaveAttribute('lang', 'de');
    });

    test('the host changing the language redraws the panel in it', async ({ page }) => {
        await host(page, 'ja');
        const states = card(page, 'tile-states');
        await expect(states.getByText('位置調整中').first()).toBeVisible();
        await expect(panelRoot(page)).toHaveAttribute('lang', 'ja');

        // The host changes its language. A computed getter is all the wrapper
        // needs for this, so the new prop reaches the view and it redraws.
        await page.locator('#hostlang').selectOption('ko');

        await expect(states.getByText('위치 조정 중').first()).toBeVisible();
        await expect(panelRoot(page)).toHaveAttribute('lang', 'ko');
    });
});

test.describe('a panel handed no language', () => {
    test('in a host it keeps the language the core is already in, and still says it', async ({ page }) => {
        await page.goto('/gallery.html?mode=sheet&theme=host-gray&width=1200');
        await page.waitForSelector('#stage[data-ready="true"]');
        // English, the language the core starts in, with its own words.
        await expect(card(page, 'tile-states').locator('.status-tag', { hasText: 'Error' })).toBeVisible();
        await expect(panelRoot(page)).toHaveAttribute('lang', 'en');
    });

    test('on the standalone page it keeps its picker, which is the only way to change it', async ({ page }) => {
        await page.goto('/gallery.html?mode=sheet&theme=dark&width=1200');
        await page.waitForSelector('#stage[data-ready="true"]');
        const states = card(page, 'tile-states');
        // The panel's own bar and its Language row, exactly as before.
        await expect(states.locator('[data-popover-invoker="settings"]')).toHaveCount(1);
        await expect(panelRoot(page)).toHaveAttribute('lang', 'en');
        // And a host's language is not offered outside a host: the standalone
        // page is the one that chooses, and it chooses for itself.
        await expect(page.locator('#hostlang')).toHaveCount(0);
    });
});

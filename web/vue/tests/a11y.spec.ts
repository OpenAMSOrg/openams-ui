import AxeBuilder from '@axe-core/playwright';
import { expect, test } from '@playwright/test';
import { action, card, clickRing, controls, dialog, open, openPopover, target, type Theme } from './panel';

// The floors of principle 12: no serious or critical rule broken, in any of the
// places the panel is drawn (contrast included, in the dark and light sets and
// on both host cards), and every control named.

const themes: { theme: Theme; name: string }[] = [
    { theme: 'dark', name: 'dark' },
    { theme: 'light', name: 'light' },
    { theme: 'host-gray', name: 'the host on its gray card' },
    { theme: 'host-white', name: 'the host on its white card' },
];

interface Findings {
    id: string;
    impact: string | null;
    targets: string[];
}

async function audit(page: import('@playwright/test').Page, scope: string): Promise<Findings[]> {
    const results = await new AxeBuilder({ page }).include(scope).analyze();
    return results.violations.map((v) => ({
        id: v.id,
        impact: v.impact,
        targets: v.nodes.map((n) => n.target.join(' ')),
    }));
}

/** The rules that block a release: serious and critical only. */
const blocking = (found: Findings[]): Findings[] => found.filter((f) => f.impact === 'serious' || f.impact === 'critical');

test.describe('accessibility', () => {
    for (const { theme, name } of themes) {
        test(`no serious or critical rule is broken in ${name}`, async ({ page }) => {
            await open(page, { state: 'sheet', theme, width: 1200 });
            expect(blocking(await audit(page, '.oams-panel'))).toEqual([]);
        });
    }

    test('a tile at 360 px is clean', async ({ page }) => {
        await open(page, { state: 'sheet', theme: 'dark', width: 360 });
        expect(blocking(await audit(page, '.oams-panel'))).toEqual([]);
    });

    test('an open popover meets every rule, contrast included', async ({ page }) => {
        await open(page, { state: 'sheet', theme: 'dark', width: 1200 });
        const states = card(page, 'tile-states');
        await clickRing(states, 'oams11');
        await expect(openPopover(page, 'tile')).toBeVisible();
        expect(blocking(await audit(page, '[data-popover="tile"]:popover-open'))).toEqual([]);
    });

    test('the tag\'s popover meets every rule, contrast included', async ({ page }) => {
        await open(page, { state: 'sheet', theme: 'host-white', width: 1200 });
        const states = card(page, 'tile-states');
        await target(states, 'oams23', 'tag').click();
        await expect(openPopover(page, 'tag')).toBeVisible();
        expect(blocking(await audit(page, '[data-popover="tag"]:popover-open'))).toEqual([]);
    });

    test('an open dialog meets every rule, contrast included', async ({ page }) => {
        await open(page, { state: 'sheet', theme: 'light', width: 1200 });
        const states = card(page, 'tile-states');
        await states.locator('[data-popover-invoker="unit"]').first().click();
        await action(openPopover(page, 'unit'), 'dry_start').click();
        await expect(dialog(page)).toBeVisible();
        expect(blocking(await audit(page, '.oams-panel dialog[open]'))).toEqual([]);
    });

    test('every control on screen has an accessible name', async ({ page }) => {
        // Every control of every card is asked in turn; on a busy machine that
        // ran past the default 30 s now and then, so it gets its own time.
        test.setTimeout(90_000);
        await open(page, { state: 'sheet', theme: 'dark', width: 1200 });
        const panel = page.locator('.oams-panel');
        const named: string[] = [];
        for (const el of await controls(panel).filter({ visible: true }).all()) {
            // Playwright's own name computation, not a re-reading of the markup.
            await expect(el, `${await describe(el)} has a name`).toHaveAccessibleName(/\S/);
            named.push(await describe(el));
        }
        // The controls that carry the panel's actions, by name, as a reader
        // meets them.
        expect(named.length).toBeGreaterThan(40);
        for (const part of ['unload', 'lane-hit', 'ring-hit', 'tag-hit', 'env', 'alert-slot']) {
            expect(named.some((n) => n.includes(part))).toBe(true);
        }
    });

    test('an open popover\'s controls are named', async ({ page }) => {
        await open(page, { state: 'sheet', theme: 'dark', width: 1200 });
        const states = card(page, 'tile-states');
        await clickRing(states, 'oams11');
        const menu = openPopover(page, 'tile');
        const labels = await menu.locator('.action-label').allTextContents();
        expect(labels.length).toBeGreaterThan(0);
        for (const el of await controls(menu).all()) {
            await expect(el).toHaveAccessibleName(/\S/);
        }
    });

    test('a dialog\'s controls are named', async ({ page }) => {
        await open(page, { state: 'sheet', theme: 'dark', width: 1200 });
        const states = card(page, 'tile-states');
        await states.locator('[data-popover-invoker="unit"]').first().click();
        await action(openPopover(page, 'unit'), 'dry_start').click();
        const dlg = dialog(page);
        for (const el of await controls(dlg).all()) {
            await expect(el).toHaveAccessibleName(/\S/);
        }
        // And the dialog says what it is.
        await expect(dlg).toHaveAccessibleName(/Start drying/);
    });

    test('the tile\'s targets are named for what they open', async ({ page }) => {
        await open(page, { state: 'sheet', theme: 'dark', width: 1200 });
        const states = card(page, 'tile-states');
        // The tile and the ring's row open the one menu, so a reader hears the
        // same name on both: the bay's own. The tag has its own name.
        const tileName = await target(states, 'oams11', 'tile').evaluate((n) => n.getAttribute('aria-label'));
        const ringName = await target(states, 'oams11', 'ring').evaluate((n) => n.getAttribute('aria-label'));
        expect(tileName).toMatch(/\S/);
        expect(ringName).toBe(tileName);
        await expect(target(states, 'oams11', 'tile')).toHaveAccessibleName(tileName!);
        await expect(target(states, 'oams23', 'tag')).toHaveAccessibleName('Error');
        // And the tag's name is not the menu's.
        const tagName = await target(states, 'oams23', 'tag').evaluate((n) => n.getAttribute('aria-label'));
        const menuName = await target(states, 'oams23', 'tile').evaluate((n) => n.getAttribute('aria-label'));
        expect(tagName).not.toBe(menuName);
    });
});

/** A short description of a control, for a failure message. */
async function describe(el: import('@playwright/test').Locator): Promise<string> {
    const info = await el.evaluate((n: Element) => ({
        tag: n.tagName.toLowerCase(),
        cls: n.className?.toString().split(' ').filter(Boolean).slice(0, 2).join('.'),
        slot: n.closest<HTMLElement>('[data-slot]')?.dataset.slot,
        action: n.getAttribute('data-action'),
    }));
    return [info.tag, info.cls, info.slot, info.action].filter(Boolean).join(' ');
}

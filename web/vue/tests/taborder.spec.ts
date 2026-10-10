import { expect, test, type Locator } from '@playwright/test';
import { card, controls, open, openPopover, type Theme } from './panel';

// Tab follows reading order, and the focus is always visible (principle 12:
// WCAG 2.4.3 focus order, 2.4.7 focus visible). Reading order is document
// order, so the test is the plain one: nothing may reorder the focus with a
// positive tabindex.

/** A control, described the same way wherever it is measured. */
function describe(el: Element): string {
    const n = el as HTMLElement;
    return [
        n.tagName.toLowerCase(),
        (n.className || '').split(' ')[0],
        n.getAttribute('data-action') || n.getAttribute('data-target') || '',
        n.closest<HTMLElement>('[data-slot]')?.dataset.slot ?? '',
        n.closest<HTMLElement>('[data-unit]')?.dataset.unit ?? '',
    ].join(' ');
}

/** The controls of a scope, in the order the DOM has them. The body is
 *  repeated rather than shared: an evaluate callback runs in the page, where a
 *  closure from here does not exist. */
function order(scope: Locator): Promise<string[]> {
    return scope.evaluateAll((els) => els.map((el) => {
        const n = el as HTMLElement;
        return [
            n.tagName.toLowerCase(),
            (n.className || '').split(' ')[0],
            n.getAttribute('data-action') || n.getAttribute('data-target') || '',
            n.closest<HTMLElement>('[data-slot]')?.dataset.slot ?? '',
            n.closest<HTMLElement>('[data-unit]')?.dataset.unit ?? '',
        ].join(' ');
    }));
}

const one = (el: Locator): Promise<string> => el.evaluate(describe);

test.describe('tab order and focus', () => {
    test('the focus follows the order the panel is read in', async ({ page }) => {
        await open(page, { state: 'sheet', theme: 'dark', width: 1200 });
        const panel = page.locator('.oams-panel').first();
        const expected = await order(controls(panel).filter({ visible: true }));
        expect(expected.length).toBeGreaterThan(10);

        // And the browser agrees: from the panel's first control, Tab walks
        // the rest in exactly that order. (The gallery's own controls sit
        // before the stage and are not the panel's business.)
        const first = controls(panel).filter({ visible: true }).first();
        await first.focus();
        const seen: string[] = [];
        for (let i = 1; i < expected.length; i += 1) {
            await page.keyboard.press('Tab');
            seen.push(await page.evaluate(() => {
                const n = (document.activeElement ?? document.body) as HTMLElement;
                return [
                    n.tagName.toLowerCase(),
                    (n.className || '').split(' ')[0],
                    n.getAttribute('data-action') || n.getAttribute('data-target') || '',
                    n.closest<HTMLElement>('[data-slot]')?.dataset.slot ?? '',
                    n.closest<HTMLElement>('[data-unit]')?.dataset.unit ?? '',
                ].join(' ');
            }));
        }
        expect(seen).toEqual(expected.slice(1));
    });

    test('no control is put out of order with a positive tabindex', async ({ page }) => {
        await open(page, { state: 'sheet', theme: 'dark', width: 1200 });
        const positive = await page.locator('.oams-panel [tabindex]').evaluateAll(
            (els) => els.map((e) => e.getAttribute('tabindex')).filter((v) => Number(v) > 0),
        );
        expect(positive).toEqual([]);
    });

    test('every control shows where the focus is', async ({ page }) => {
        for (const theme of ['dark', 'light', 'host-white'] as Theme[]) {
            await open(page, { state: 'sheet', theme, width: 1200 });
            const panel = page.locator('.oams-panel').first();
            for (const el of await controls(panel).filter({ visible: true }).all()) {
                await el.focus();
                await expect(el, `${await one(el)} takes the focus in ${theme}`).toBeFocused();
                const ring = await el.evaluate((n: Element) => {
                    const cs = getComputedStyle(n);
                    return { width: parseFloat(cs.outlineWidth), style: cs.outlineStyle, shadow: cs.boxShadow };
                });
                // Either an outline or a shadow ring counts: what matters is
                // that something is drawn where the eye is.
                const drawn = (ring.width > 0 && ring.style !== 'none')
                    || (ring.shadow !== 'none' && ring.shadow !== '');
                expect(drawn, `${await one(el)} draws no focus indicator in ${theme}`).toBe(true);
            }
        }
    });

    test('the popover\'s own controls are reachable and show the focus', async ({ page }) => {
        await open(page, { state: 'sheet', theme: 'dark', width: 1200 });
        const states = card(page, 'tile-states');
        await states.locator('[data-target="ring"]').first().click({ position: { x: 6, y: 20 } });
        const ring = openPopover(page, 'tile');
        await expect(ring).toBeVisible();
        for (const el of await controls(ring).all()) {
            await el.focus();
            await expect(el).toBeFocused();
            const width = await el.evaluate((n: Element) => parseFloat(getComputedStyle(n).outlineWidth));
            expect(width, `${await one(el)} shows no outline`).toBeGreaterThan(0);
        }
    });

    test('Escape closes a popover and gives the focus back to its source', async ({ page }) => {
        await open(page, { state: 'sheet', theme: 'dark', width: 1200 });
        const states = card(page, 'tile-states');
        const ring = states.locator('[data-target="ring"]').first();
        await ring.click({ position: { x: 6, y: 20 } });
        await expect(openPopover(page, 'tile')).toBeVisible();
        await page.keyboard.press('Escape');
        await expect(openPopover(page, 'tile')).toBeHidden();
        // The invoker says it is no longer open, and the focus is back on it.
        await expect(ring).toHaveAttribute('aria-expanded', 'false');
        await expect(ring).toBeFocused();
    });
});

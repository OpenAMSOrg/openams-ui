import { expect, test } from '@playwright/test';
import { card, controls, open, target } from './panel';

// The floors of WCAG 2.2 SC 2.5.8 (24 x 24) for every control the panel
// draws, and SC 2.5.5 (44 x 44) for the two targets a finger has to find
// (principle 12). Measured at 360 px, the narrowest the panel is drawn at: a
// hit box may be bigger than what it paints, and the ring's is (UNIFIED_UI.md
// 4b: the tag is drawn over the ring, so the ring's target is its row).
test.describe('hit boxes at 360 px', () => {
    test.beforeEach(async ({ page }) => {
        await open(page, { state: 'sheet', theme: 'dark', width: 360 });
    });

    test('every control is at least 24 by 24', async ({ page }) => {
        const small: string[] = [];
        for (const el of await controls(page.locator('.oams-panel')).filter({ visible: true }).all()) {
            const box = await el.boundingBox();
            if (!box) continue;
            if (box.width < 24 || box.height < 24) small.push(`${await identify(el)} ${box.width}x${box.height}`);
        }
        expect(small).toEqual([]);
    });

    test('the ring\'s target is at least 44 by 44', async ({ page }) => {
        const states = card(page, 'tile-states');
        const small: string[] = [];
        for (const el of await target(states, 'oams11', 'ring').all()) {
            const box = await el.boundingBox();
            if (!box) continue;
            if (box.width < 44 || box.height < 44) small.push(`${box.width}x${box.height}`);
        }
        expect(small).toEqual([]);
        // And the row it covers is the ring's own row: the ring is inside it.
        const row = await target(states, 'oams11', 'ring').boundingBox();
        const ring = await states.locator('.lane[data-slot="oams11"] .ring').boundingBox();
        expect(row).not.toBeNull();
        expect(ring).not.toBeNull();
        expect(row!.x).toBeLessThanOrEqual(ring!.x);
        expect(row!.x + row!.width).toBeGreaterThanOrEqual(ring!.x + ring!.width);
        expect(row!.y).toBeLessThanOrEqual(ring!.y);
        expect(row!.y + row!.height).toBeGreaterThanOrEqual(ring!.y + ring!.height);
    });

    test('the tag\'s target is at least 44 by 44', async ({ page }) => {
        const states = card(page, 'tile-states');
        // oams23 shows the error tag; oams14 shows "Empty".
        for (const slot of ['oams23', 'oams14']) {
            const box = await target(states, slot, 'tag').boundingBox();
            expect(box, `${slot} has a tag target`).not.toBeNull();
            expect(box!.width).toBeGreaterThanOrEqual(44);
            expect(box!.height).toBeGreaterThanOrEqual(44);
        }
    });

    test('the tile\'s own target is the whole tile', async ({ page }) => {
        const states = card(page, 'tile-states');
        const tile = await states.locator('.lane[data-slot="oams12"]').boundingBox();
        const hit = await target(states, 'oams12', 'tile').boundingBox();
        expect(tile).not.toBeNull();
        expect(hit).not.toBeNull();
        expect(Math.round(hit!.width)).toBe(Math.round(tile!.width));
        expect(Math.round(hit!.height)).toBe(Math.round(tile!.height));
    });

    test('the three targets do not overlap each other twice over', async ({ page }) => {
        // The tag is drawn over the ring, so it sits on top of the ring's row
        // and wins there: clicking the middle of the row opens the tag.
        const states = card(page, 'tile-states');
        const ring = await target(states, 'oams23', 'ring').boundingBox();
        const tag = await target(states, 'oams23', 'tag').boundingBox();
        expect(overlaps(ring!, tag!)).toBe(true);
        // Clicking the tag's own middle opens the tag, not the ring.
        await target(states, 'oams23', 'tag').click({ position: { x: 4, y: 4 } });
        await expect(page.locator('[data-popover="tag"]:popover-open')).toBeVisible();
    });

    test('the env pill and the alert badge are at least 24 by 24', async ({ page }) => {
        const drying = card(page, 'drying');
        for (const sel of ['[data-popover-invoker="unit"]', '[data-popover-invoker="alerts"]']) {
            const box = await drying.locator(sel).first().boundingBox();
            expect(box, sel).not.toBeNull();
            expect(box!.width).toBeGreaterThanOrEqual(24);
            expect(box!.height).toBeGreaterThanOrEqual(24);
        }
    });
});

function overlaps(a: { x: number; y: number; width: number; height: number },
    b: { x: number; y: number; width: number; height: number }): boolean {
    return a.x < b.x + b.width && b.x < a.x + a.width && a.y < b.y + b.height && b.y < a.y + a.height;
}

async function identify(el: import('@playwright/test').Locator): Promise<string> {
    const info = await el.evaluate((n: Element) => ({
        cls: n.className?.toString().split(' ').filter(Boolean).slice(0, 2).join('.'),
        slot: n.closest<HTMLElement>('[data-slot]')?.dataset.slot,
        action: n.getAttribute('data-action'),
    }));
    return [info.cls, info.slot, info.action].filter(Boolean).join(' ');
}

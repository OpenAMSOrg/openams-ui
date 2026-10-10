import { expect, type Locator, type Page } from '@playwright/test';
import type { ActionResult } from '@logic';

/** The gallery page, driven by its query string (see gallery/GalleryApp.vue). */
export type PanelState = 'player' | 'sheet';
export type Theme = 'dark' | 'light' | 'host-gray' | 'host-white';

export interface OpenOptions {
    theme?: Theme;
    width?: number;
    fixture?: string;
    logic?: 'none';
    state?: PanelState;
    lang?: string;
    /** How the gallery's printer answers a spool edit: its refusal in these words, or "hold" for no answer. */
    edit?: string;
    /** Stamp every form with the core's two submit-button fields (gallery/forms.ts). */
    form?: 'require-change' | 'plain';
}

/** Open the gallery and wait for the panel to have painted. */
export async function open(page: Page, options: OpenOptions = {}): Promise<void> {
    const q = new URLSearchParams({ mode: options.state ?? 'sheet' });
    if (options.fixture) q.set('fixture', options.fixture);
    if (options.logic) q.set('logic', options.logic);
    if (options.lang) q.set('lang', options.lang);
    if (options.edit) q.set('edit', options.edit);
    if (options.form) q.set('form', options.form);
    q.set('theme', options.theme ?? 'dark');
    q.set('width', String(options.width ?? 1200));
    await page.goto(`/gallery.html?${q}`);
    await page.waitForSelector('#stage[data-ready="true"]');
}

/** One card of the state sheet, the scope a test works in. Its key is the
 *  scenario's own, so a test never has to count cards. */
export function card(page: Page, key: string): Locator {
    return page.locator(`.card[data-key="${key}"]`);
}

/** One tile of a card, by its slot id (`oams11` is a unit's first bay). */
export function tile(scope: Locator, slot: string): Locator {
    return scope.locator(`.lane[data-slot="${slot}"]`);
}

/** A click target of a tile: the whole tile, the ring's row (both open the tile's one menu), or the tag. The
 *  tile's own target is clicked at the left of its middle (clickTile), which is
 *  neither the tool pill, nor the ring's row, nor the tag. */
export function target(scope: Locator, slot: string, which: 'tile' | 'ring' | 'tag'): Locator {
    return tile(scope, slot).locator(`[data-target="${which}"]`);
}

export async function clickTile(scope: Locator, slot: string): Promise<void> {
    // The middle of the tile, left: the spool's name and meta. The top row is
    // the tool pill's own target (it changes the group), the bottom row the
    // ring's.
    const box = await target(scope, slot, 'tile').boundingBox();
    await target(scope, slot, 'tile').click({ position: { x: 8, y: Math.round((box?.height ?? 150) * 0.45) } });
}

/** Click the ring's target at the left end of its row: the tag is drawn over
 *  the middle of the ring (UNIFIED_UI.md 4b) and wins there, exactly as it does
 *  for a user. */
export async function clickRing(scope: Locator, slot: string): Promise<void> {
    await target(scope, slot, 'ring').click({ position: { x: 6, y: 20 } });
}

/** Every popover of the panel, by its role name. */
export function popover(page: Page, name: string): Locator {
    return page.locator(`[data-popover="${name}"]`);
}

/** The one open popover of that role: `:popover-open` is the browser's own
 *  answer, so a test never counts popovers it cannot see. */
export function openPopover(page: Page, name: string): Locator {
    return page.locator(`[data-popover="${name}"]:popover-open`);
}

/** A tile's own popover. */
export function tilePopover(scope: Locator, slot: string, name: 'tag' | 'tile'): Locator {
    return tile(scope, slot).locator(`[data-popover="${name}"]`);
}

/** Let two frames pass. A popover is placed in the frame it opens in (see
 *  src/popover.ts), so a spec that measures where it sits has to let that
 *  frame go by first. */
export async function settled(page: Page): Promise<void> {
    await page.evaluate(
        () => new Promise<void>((done) => requestAnimationFrame(() => requestAnimationFrame(() => done()))),
    );
}

/** The labels of the actions a popover currently lists. */
export async function listedActions(p: Locator): Promise<string[]> {
    return p.locator('.action-label').allTextContents();
}

/** One action of a popover, by its own id. */
export function action(p: Locator, id: string): Locator {
    return p.locator(`.action[data-action="${id}"]`);
}

/** The panel's one dialog, whether it is a form or a confirmation. */
export function dialog(page: Page): Locator {
    return page.locator('.oams-panel dialog[open]');
}

/** A `select` field's button: the one control that says what is chosen. */
export function select(scope: Locator, field: string): Locator {
    return scope.locator(`button[data-field="${field}"]`);
}

/** The options of a `select` field's list, given the list itself. */
export function options(list: Locator): Locator {
    return list.locator('[role="option"]');
}

/** The one open list of a `select` field: `:popover-open` is the browser's own
 *  answer, so a spec never counts a list it cannot see. */
export function openList(scope: Locator, field: string): Locator {
    return scope.locator(`[data-list="${field}"]:popover-open`);
}

/** Open a field's list and hand back the button and the open list, so a spec
 *  never has to know that a list is placed a frame after it opens. */
export async function openSelect(scope: Locator, field: string): Promise<{ button: Locator; list: Locator }> {
    const button = select(scope, field);
    await button.click();
    await expect(openList(scope, field)).toBeVisible();
    await settled(scope.page());
    return { button, list: openList(scope, field) };
}

/** The option of an open list that is lit, by its own index. */
export function activeOption(list: Locator): Locator {
    return list.locator('[role="option"][data-active="true"]');
}

/** What a select field's button currently says. */
export async function chosen(scope: Locator, field: string): Promise<string> {
    return (await select(scope, field).textContent())?.trim() ?? '';
}

/** One panel's message row: the transient one the panel itself wrote, or the
 *  core's own notice. Scoped, because the state sheet has twelve panels. */
export function message(scope: Locator): Locator {
    return scope.locator('[role="status"].notice');
}

/** Every `request` the panel has emitted, as the host would receive it. */
export async function requests(page: Page): Promise<ActionResult[]> {
    return page.evaluate(() => (window as unknown as { openamsRequests?: ActionResult[] }).openamsRequests ?? []);
}

export async function expectRequests(page: Page, count: number): Promise<void> {
    await expect.poll(async () => (await requests(page)).length).toBe(count);
}

/** Every control of a card a user can reach, in the order they are read. */
export function controls(scope: Locator): Locator {
    return scope.locator('a[href], button, input, select, textarea, [tabindex]:not([tabindex="-1"])');
}

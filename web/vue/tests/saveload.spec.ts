import { expect, test, type Page } from '@playwright/test';
import {
    action, clickTile, dialog, expectRequests, message, open, openPopover, openSelect, options,
    requests, select,
} from './panel';

// "Save and load": Load on a bay nobody has identified asks for the spool first,
// then loads (PRINCIPLES.md 13). The form is the spool editor's, the material is
// required, and the load goes out only after the printer has taken the edit.
// (the recorded idle printer, with Spoolman up). The gallery's printer takes a spool edit at once, refuses it in words
// (`?edit=<words>`) or never answers (`?edit=hold`).
test.describe('save and load', () => {
    /** The recorded `idle` printer, with Spoolman up and its edit endpoint
     *  present (the recording predates both): its first two bays are
     *  unidentified, ready, and in a tool, so their Load is the one this asks
     *  about. The gallery reads the recording as a module, and the page is
     *  handed the same events with that one status after them. */
    const withSpoolman = async (page: Page) => {
        await page.route(/vams\/idle\.json\?import/, async (route) => {
            const raw = await route.fetch({ url: route.request().url().replace('?import', '') });
            const fixture = await raw.json();
            fixture.events.push({
                t: 1e6, kind: 'component_status', unit: null,
                payload: { spoolman_online: true, edit: true, bays: {} },
            });
            await route.fulfill({
                contentType: 'text/javascript',
                body: `export default ${JSON.stringify(fixture)};`,
            });
        });
    };
    const openIdle = async (page: Page, edit?: string) => {
        await withSpoolman(page);
        await open(page, { state: 'player', fixture: 'idle', width: 1200, edit });
    };
    /** Press Load on the idle printer's first unidentified tile. */
    const pressLoad = async (page: Page) => {
        const tiles = page.locator('.lane[data-state="unknown"]');
        const slot = await tiles.first().getAttribute('data-slot');
        expect(slot).toBeTruthy();
        const scope = page.locator('.oams-panel').first();
        await clickTile(scope, slot!);
        await action(openPopover(page, 'tile'), 'load').click();
        return { scope, slot: slot!, dlg: dialog(page) };
    };
    const chooseMaterial = async (page: Page, name: string) => {
        const { list } = await openSelect(dialog(page), 'material');
        await options(list).filter({ hasText: new RegExp(`^\\s*${name}\\s*$`) }).click();
    };

    test('Load on an unknown tile opens the spool form, and sends nothing yet', async ({ page }) => {
        await openIdle(page);
        const { dlg } = await pressLoad(page);
        await expect(dlg).toBeVisible();
        await expect(dlg.locator('.dlg-head')).toHaveText('Load');
        const names = ['Material', 'Color', 'Vendor', 'Remaining', 'Initial weight', 'PA value'];
        await expect(dlg.locator('label[for]')).toHaveCount(names.length);
        await expect(select(dlg, 'material')).toHaveText('Choose a material');
        await expect(dlg.locator('[data-action="load"]')).toHaveText('Save and load');
        expect(await requests(page)).toEqual([]);
    });

    test('the button is dead until a material is chosen, and then it is live', async ({ page }) => {
        await openIdle(page);
        const { dlg } = await pressLoad(page);
        const submit = dlg.locator('[data-action="load"]');
        await expect(submit).toBeDisabled();
        // another field does not wake it: only the answer it is waiting for does
        await dlg.locator('[data-field="remaining"]').fill('640');
        await expect(submit).toBeDisabled();
        await submit.click({ force: true });
        expect(await requests(page)).toEqual([]);
        await chooseMaterial(page, 'PLA');
        await expect(submit).toBeEnabled();
    });

    test('a successful save sends the edit, then exactly one load, in that order', async ({ page }) => {
        await openIdle(page);
        const { dlg, slot } = await pressLoad(page);
        await chooseMaterial(page, 'PLA');
        await dlg.locator('[data-action="load"]').click();
        await expect(dlg).toBeHidden();
        await expectRequests(page, 2);
        const sent = await requests(page);
        expect(sent[0]).toMatchObject({ kind: 'rpc', method: 'server.openams_spoolman.edit' });
        expect(sent[0]).toMatchObject({ params: { material: 'PLA' } });
        // the edit names its follow-up, which the panel holds back; the host sends only the call
        expect(sent[0]).toMatchObject({ then: `load ${slot}` });
        expect(sent[1].kind).toBe('gcode');
        expect(JSON.stringify(sent[1])).toContain('OPENAMS_LOAD');
        expect(sent[1]).not.toHaveProperty('then');
        // and not a third, later
        await page.waitForTimeout(300);
        expect(await requests(page)).toHaveLength(2);
    });

    test('a refused save sends no load, and says why', async ({ page }) => {
        await openIdle(page, 'Spoolman is offline');
        const { scope, dlg } = await pressLoad(page);
        await chooseMaterial(page, 'PLA');
        await dlg.locator('[data-action="load"]').click();
        await expectRequests(page, 1);
        await expect(message(scope)).toHaveText('Could not save the spool: Spoolman is offline');
        await page.waitForTimeout(300);
        const sent = await requests(page);
        expect(sent).toHaveLength(1);
        expect(sent[0]).toMatchObject({ kind: 'rpc', method: 'server.openams_spoolman.edit' });
    });

    test('no answer yet sends no load', async ({ page }) => {
        await openIdle(page, 'hold');
        const { dlg } = await pressLoad(page);
        await chooseMaterial(page, 'PLA');
        await dlg.locator('[data-action="load"]').click();
        await expectRequests(page, 1);
        await page.waitForTimeout(300);
        expect(await requests(page)).toHaveLength(1);
    });
});

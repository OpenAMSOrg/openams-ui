import { expect, test, type Page } from '@playwright/test';
import {
    action, card, chosen, clickRing, dialog, expectRequests, message, open, openPopover, openSelect,
    options, requests, select, target,
} from './panel';

// Dialogs: a native <dialog> with showModal(), for a form and for a
// confirmation (docs/design/UNIFIED_UI.md 7; PRINCIPLES.md 7, 11). Nothing is
// sent until the dialog's own button says so.
test.describe('dialogs', () => {
    test.beforeEach(async ({ page }) => {
        await open(page, { state: 'sheet', width: 1200 });
    });

    test('an action with a form opens a dialog with the core\'s fields', async ({ page }) => {
        const states = card(page, 'tile-states');
        await states.locator('[data-popover-invoker="unit"]').first().click();
        await action(openPopover(page, 'unit'), 'dry_start').click();

        const dlg = dialog(page);
        await expect(dlg).toBeVisible();
        // The dialog is the one the browser put in the top layer, and the panel
        // is inert behind it.
        expect(await dlg.evaluate((el: HTMLDialogElement) => el.matches(':modal'))).toBe(true);
        // Its own fields, the core's labels and the core's defaults.
        await expect(dlg.locator('[data-field="target"]')).toHaveAttribute('min', '45');
        await expect(dlg.locator('[data-field="target"]')).toHaveAttribute('max', '65');
        await expect(dlg.locator('[data-field="hours"]')).toHaveAttribute('min', '1');
        await expect(dlg.locator('[data-field="hours"]')).toHaveAttribute('max', '24');
        // The core's own label, and its own unit beside it.
        const target = dlg.locator('label[for]').first();
        await expect(target).toContainText('Target');
        await expect(target.locator('.dlg-unit')).toHaveText('C');
        await expect(dlg.locator('.dlg-unit').nth(1)).toHaveText('h');
        // Every field is labelled by its own label, for a screen reader.
        for (const field of await dlg.locator('[data-field]').all()) {
            const id = await field.getAttribute('id');
            expect(id).toBeTruthy();
            await expect(dlg.locator(`label[for="${id}"]`)).toHaveCount(1);
        }
        // Nothing has been sent: the form is where the task happens.
        expect(await requests(page)).toEqual([]);
    });

    test('the form is sent with the values the user typed', async ({ page }) => {
        const states = card(page, 'tile-states');
        await states.locator('[data-popover-invoker="unit"]').first().click();
        await action(openPopover(page, 'unit'), 'dry_start').click();
        const dlg = dialog(page);
        await dlg.locator('[data-field="target"]').fill('60');
        await dlg.locator('[data-field="hours"]').fill('4');
        await dlg.locator('[data-action="dry_start"]').click();
        await expect(dlg).toBeHidden();
        // The line the core built from the form, with the user's values in it.
        expect(await requests(page)).toEqual([
            { kind: 'gcode', script: 'OAMS_DRYER_START OAMS=1 TARGET=60 DURATION=14400' },
        ]);
    });

    test('Escape closes a form and sends nothing', async ({ page }) => {
        const states = card(page, 'tile-states');
        await states.locator('[data-popover-invoker="unit"]').first().click();
        await action(openPopover(page, 'unit'), 'dry_start').click();
        await expect(dialog(page)).toBeVisible();
        await page.keyboard.press('Escape');
        await expect(dialog(page)).toBeHidden();
        expect(await requests(page)).toEqual([]);
    });

    test('Cancel closes a form and sends nothing', async ({ page }) => {
        const states = card(page, 'tile-states');
        await states.locator('[data-popover-invoker="unit"]').first().click();
        await action(openPopover(page, 'unit'), 'dry_start').click();
        const dlg = dialog(page);
        await dlg.locator('[data-role="cancel"]').click();
        await expect(dlg).toBeHidden();
        expect(await requests(page)).toEqual([]);
    });

    // Calibrate is asked about on the `idle` card's loaded oams11: on the
    // tile-states card, the stop fault latched on fps1 (the feeder stall) holds
    // every motion of that lane, Calibrate included.
    test('a confirm shows the core\'s own words, verbatim', async ({ page }) => {
        const idle = card(page, 'idle');
        await clickRing(idle, 'oams11');
        await action(openPopover(page, 'tile'), 'calibrate').click();

        const dlg = dialog(page);
        await expect(dlg).toBeVisible();
        // {title, text, ok_label} as web/logic wrote them, nothing reworded.
        await expect(dlg.locator('.dlg-head')).toHaveText('Calibrate this spool?');
        await expect(dlg.locator('.dlg-text')).toHaveText('The filament moves while the PTFE length is measured.');
        await expect(dlg.locator('[data-action="calibrate"]')).toHaveText('Calibrate');
        // The dialog says which dialog it is.
        await expect(dlg.locator('.dlg-text')).toHaveCount(1);
        // And nothing happened yet.
        expect(await requests(page)).toEqual([]);
    });

    test('the confirm\'s own button resolves the action', async ({ page }) => {
        const idle = card(page, 'idle');
        await clickRing(idle, 'oams11');
        await action(openPopover(page, 'tile'), 'calibrate').click();
        await dialog(page).locator('[data-action="calibrate"]').click();
        await expect(dialog(page)).toBeHidden();
        expect(await requests(page)).toEqual([
            { kind: 'gcode', script: 'OAMS_CALIBRATE_PTFE_LENGTH OAMS=1 SPOOL=0' },
        ]);
    });

    test('Cancel on a confirm sends nothing', async ({ page }) => {
        const idle = card(page, 'idle');
        await clickRing(idle, 'oams11');
        await action(openPopover(page, 'tile'), 'calibrate').click();
        await dialog(page).locator('[data-role="cancel"]').click();
        await expect(dialog(page)).toBeHidden();
        await expectRequests(page, 0);
    });

    test('a dialog never sits on top of a popover', async ({ page }) => {
        const states = card(page, 'tile-states');
        await states.locator('[data-popover-invoker="unit"]').first().click();
        await action(openPopover(page, 'unit'), 'dry_start').click();
        // One at a time, top to bottom: the dialog replaced the popover, and it
        // does not come back behind the dialog when the dialog closes.
        await expect(dialog(page)).toBeVisible();
        await expect(openPopover(page, 'unit')).toBeHidden();
    });

    test('a tag\'s Clear errors is the host\'s Reset, sent without a confirm', async ({ page }) => {
        // One clear (2026-11-12): a unit's clear_fault is Clear errors, which
        // asks nothing first (the old Clear fault confirm is gone with it).
        const states = card(page, 'tile-states');
        await target(states, 'oams23', 'tag').click();
        const tag = openPopover(page, 'tag');
        await expect(tag).toBeVisible();
        await action(tag, 'clear_errors').click();
        await expect(dialog(page)).toBeHidden();
        expect(await requests(page)).toEqual([{ kind: 'gcode', script: 'OAMSM_CLEAR_ERRORS' }]);
    });
});

// "Edit spool" (PRINCIPLES.md 8, 11): the ring's action opens a form that starts
// from the spool's own facts, sends only what the user changed, and shows the
// printer's refusal in its own words. oams11 is a tag-linked ASA, #1c1c1c,
// Polymaker, 1000 g of 1000 g; oams14 is an empty bay.
test.describe('edit spool', () => {
    const openForm = async (page: Page, slot = 'oams11') => {
        const states = card(page, 'tile-states');
        await clickRing(states, slot);
        await action(openPopover(page, 'tile'), 'edit_spool').click();
        return { states, dlg: dialog(page) };
    };

    test('the form starts from the spool\'s own facts and sends nothing until Edit spool', async ({ page }) => {
        await open(page, { state: 'sheet', width: 1200 });
        const { dlg } = await openForm(page);
        await expect(dlg).toBeVisible();
        await expect(dlg.locator('.dlg-head')).toHaveText('Edit spool');
        // Six fields, each labelled, in the core's words.
        const names = ['Material', 'Color', 'Vendor', 'Remaining', 'Initial weight', 'PA value'];
        await expect(dlg.locator('label[for]')).toHaveCount(names.length);
        for (const [i, name] of names.entries()) await expect(dlg.locator('label[for]').nth(i)).toContainText(name);
        await expect(dlg.locator('.dlg-unit')).toHaveText(['g', 'g']);
        const picked = async (field: string) => chosen(dlg, field);
        expect(await picked('material')).toBe('ASA');
        // #1c1c1c is on no palette, so it is one more choice, and the current one.
        expect(await picked('color')).toBe('#1C1C1C');
        expect(await picked('vendor')).toBe('Polymaker');
        await expect(dlg.locator('[data-field="remaining"]')).toHaveValue('1000');
        await expect(dlg.locator('[data-field="initial"]')).toHaveValue('1000');
        await expect(dlg.locator('[data-field="pa"]')).toHaveValue('0.02');
        // Generic leads, then Spoolman's own vendors, then the common brands
        // Spoolman does not already name (thirty names in all here).
        const { list } = await openSelect(dlg, 'vendor');
        const vendors = await options(list).allInnerTexts();
        expect(vendors.slice(0, 4).map((n) => n.trim())).toEqual(['Generic', 'Bambu Lab', 'Polymaker', 'Prusament']);
        expect(vendors.length).toBe(30);
        expect(await requests(page)).toEqual([]);
    });

    test('only the fields the user changed are sent', async ({ page }) => {
        await open(page, { state: 'sheet', width: 1200 });
        const { dlg } = await openForm(page);
        const pick = async (field: string, label: string) => {
            const { list } = await openSelect(dlg, field);
            await list.getByRole('option', { name: label, exact: true }).click();
        };
        await pick('color', 'red');
        await dlg.locator('[data-field="remaining"]').fill('640');
        await pick('vendor', 'Prusament');
        await dlg.locator('[data-action="edit_spool"]').click();
        await expect(dlg).toBeHidden();
        expect(await requests(page)).toEqual([
            {
                kind: 'rpc',
                method: 'server.openams_spoolman.edit',
                params: { unit: 'oams1', bay: 0, color_hex: 'C12E1F', vendor: 'Prusament', remaining_weight: 640 },
            },
        ]);
    });

    // The materials (C4 §9.3): the gallery's host lists PCTG (no temperature in
    // its table) and PET-CF17 (PET-CF's), and takes a filament's temperatures.
    const pick = async (dlg: ReturnType<typeof dialog>, field: string, label: string) => {
        const { list } = await openSelect(dlg, field);
        await list.getByRole('option', { name: label, exact: true }).click();
    };

    test('Spoolman\'s materials follow the common ones, and Other… ends the list', async ({ page }) => {
        await open(page, { state: 'sheet', width: 1200 });
        const { dlg } = await openForm(page);
        const { list } = await openSelect(dlg, 'material');
        const labels = (await options(list).allInnerTexts()).map((n) => n.trim());
        expect(labels.slice(-3)).toEqual(['PCTG', 'PET-CF17', 'Other…']);
        expect(labels.slice(0, 2)).toEqual(['PLA', 'PETG']);
    });

    test('Other… asks for the new material, kept in capitals, and sends it', async ({ page }) => {
        await open(page, { state: 'sheet', width: 1200 });
        const { dlg } = await openForm(page);
        const typed = dlg.locator('[data-field="custom"]');
        await expect(typed).toHaveCount(0);
        await pick(dlg, 'material', 'Other…');
        await expect(typed).toBeVisible();
        await expect(dlg.locator('[data-note="custom"]')).toContainText('An AMS unit keeps only the first 8');
        // Nothing typed yet: there is nothing to save.
        const save = dlg.locator('[data-action="edit_spool"]');
        await expect(save).toBeDisabled();
        // What cannot be part of a material never reaches the field.
        await typed.pressSequentially('pet cf*17!');
        await expect(typed).toHaveValue('PETCF17');
        await typed.fill('');
        await typed.pressSequentially('pet-cf17');
        await expect(typed).toHaveValue('PET-CF17');
        // A typed type may have no temperature of the printer's own: both are offered, both optional.
        await expect(dlg.locator('[data-field="nozzle"]')).toHaveValue('');
        await expect(dlg.locator('[data-field="bed"]')).toBeVisible();
        await expect(save).toBeEnabled();
        await save.click();
        await expect(dlg).toBeHidden();
        expect(await requests(page)).toEqual([
            { kind: 'rpc', method: 'server.openams_spoolman.edit', params: { unit: 'oams1', bay: 0, material: 'PET-CF17' } },
        ]);
    });

    test('a material the printer has no temperature for asks for one, and sends what is given', async ({ page }) => {
        await open(page, { state: 'sheet', width: 1200 });
        const { dlg } = await openForm(page);
        const nozzle = dlg.locator('[data-field="nozzle"]');
        await expect(nozzle).toHaveCount(0);
        // PET-CF17 has the printer's own (PET-CF's): nothing to ask.
        await pick(dlg, 'material', 'PET-CF17');
        await expect(nozzle).toHaveCount(0);
        await pick(dlg, 'material', 'PCTG');
        await expect(nozzle).toBeVisible();
        await expect(dlg.locator('[data-note="nozzle"]')).toContainText('Optional');
        await nozzle.fill('250');
        await dlg.locator('[data-action="edit_spool"]').click();
        await expect(dlg).toBeHidden();
        expect(await requests(page)).toEqual([
            {
                kind: 'rpc', method: 'server.openams_spoolman.edit',
                params: { unit: 'oams1', bay: 0, material: 'PCTG', extruder_temp: 250 },
            },
        ]);
    });

    test('what was typed under Other… goes when another material is chosen', async ({ page }) => {
        await open(page, { state: 'sheet', width: 1200 });
        const { dlg } = await openForm(page);
        await pick(dlg, 'material', 'Other…');
        await dlg.locator('[data-field="custom"]').pressSequentially('xyz');
        await pick(dlg, 'material', 'ASA');
        await expect(dlg.locator('[data-field="custom"]')).toHaveCount(0);
        // back where it opened: nothing to save
        await expect(dlg.locator('[data-action="edit_spool"]')).toBeDisabled();
    });

    test('a form left as it opened cannot be submitted, and sends nothing', async ({ page }) => {
        await open(page, { state: 'sheet', width: 1200 });
        const { dlg } = await openForm(page);
        const save = dlg.locator('[data-action="edit_spool"]');
        await expect(save).toHaveText('Save edit');
        await expect(save).toBeDisabled();
        expect(await requests(page)).toEqual([]);
    });

    test('a weight out of range is refused with the core\'s own words', async ({ page }) => {
        await open(page, { state: 'sheet', width: 1200 });
        const { states, dlg } = await openForm(page);
        await dlg.locator('[data-field="remaining"]').fill('1500');
        await dlg.locator('[data-action="edit_spool"]').click();
        await expect(message(states)).toHaveText('The remaining weight cannot exceed the initial weight');
        expect(await requests(page)).toEqual([]);
    });

    test('the printer\'s refusal is shown in its own words, and the wait ends', async ({ page }) => {
        await open(page, { state: 'sheet', width: 1200, edit: 'Spoolman is offline' });
        const { states, dlg } = await openForm(page);
        await dlg.locator('[data-field="remaining"]').fill('640');
        await dlg.locator('[data-action="edit_spool"]').click();
        await expectRequests(page, 1);
        await expect(message(states)).toHaveText('Could not save the spool: Spoolman is offline');
        await expect(message(states)).toHaveClass(/tone-error/);
        // Not still waiting: the press is over, so it can be tried again.
        await clickRing(states, 'oams11');
        await expect(action(openPopover(page, 'tile'), 'edit_spool')).not.toHaveAttribute('data-pending', 'true');
    });

    test('while the printer has not answered the action is pending, and is not sent twice', async ({ page }) => {
        await open(page, { state: 'sheet', width: 1200, edit: 'hold' });
        const { states, dlg } = await openForm(page);
        await dlg.locator('[data-field="remaining"]').fill('640');
        await dlg.locator('[data-action="edit_spool"]').click();
        await expectRequests(page, 1);
        await clickRing(states, 'oams11');
        const edit = action(openPopover(page, 'tile'), 'edit_spool');
        await expect(edit).toHaveAttribute('data-pending', 'true');
        await edit.click();
        expect(await requests(page)).toHaveLength(1);
    });

    test('an empty bay is dimmed, and says why', async ({ page }) => {
        await open(page, { state: 'sheet', width: 1200 });
        const states = card(page, 'tile-states');
        await clickRing(states, 'oams14');
        const edit = action(openPopover(page, 'tile'), 'edit_spool');
        await expect(edit).toHaveAttribute('aria-disabled', 'true');
        await expect(edit).toHaveAttribute('title', 'No spool is inserted');
    });
});

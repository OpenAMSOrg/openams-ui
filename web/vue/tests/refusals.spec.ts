import { expect, test, type Page } from '@playwright/test';
import { open } from './panel';

// What the panel does with the printer's answer to a call it waited on
// (PRINCIPLES.md 8: say what happened, why, and the printer's own words;
// H3: a refused action takes the general lead, not the spool editor's).
//
// The host's side of this is `done(message?)` on the panel's `request` event,
// and the gallery only wires it to the spool edit, so these drive the panel's
// own Interactivity in the page - the same module the panel is built from,
// through the dev server that serves it.

const REFUSAL = 'Heat the extruder before unloading';
const EDIT_LEAD = 'Could not save the spool:';
const ACTION_LEAD = 'Could not do that:';

interface Settled {
    message: string;
    tone: string;
    /** Whether the action was still waiting when the answer arrived. */
    waiting: boolean;
    /** Whether it is still waiting after the answer. */
    waitingAfter: boolean;
}

/**
 * Run one action, let the printer answer it in `refusal` words, and report what
 * the panel said. `endTheWait` first moves the printer's view on, which ends
 * the wait the way a load starting does.
 */
async function settle(
    page: Page, kind: 'edit' | 'action', refusal: string | undefined, endTheWait = false,
): Promise<Settled> {
    return page.evaluate(async ({ k, words, end }) => {
        const mod = await import('/src/interact.ts');
        const labels = { edit_failed: 'Could not save the spool:', action_refused: 'Could not do that:' };
        const action = { id: 'load', label: 'Load', line: 'load T3', enabled: true, style: 'normal' };
        const ctrl = new mod.Interactivity(() => ({ kind: 'gcode', script: 'OPENAMS_LOAD GROUP=T3 SLOT=5' }));

        ctrl.setView({ labels, tick: 1 });
        ctrl.run(action);
        const waiting = ctrl.anythingPending;
        // The printer's state moved on: a load under way. The wait is over.
        if (end) ctrl.setView({ labels, tick: 2 });
        ctrl.settle('load T3', words, k);
        await new Promise((done) => setTimeout(done, 0));
        const out = { message: ctrl.message, tone: ctrl.tone, waiting, waitingAfter: ctrl.anythingPending };
        ctrl.dispose();
        return out;
    }, { k: kind, words: refusal, end: endTheWait });
}

test.describe("the printer's answer to an action it waited on", () => {
    test.beforeEach(async ({ page }) => {
        // Any page on the origin will do: the module is served from there, and
        // the player with no logic draws the panel's frame alone.
        await open(page, { state: 'player', logic: 'none' });
    });

    test('a refused action is flashed with the general lead and the printer\'s words', async ({ page }) => {
        const s = await settle(page, 'action', REFUSAL);
        expect(s.waiting).toBe(true);
        expect(s.message).toBe(`${ACTION_LEAD} ${REFUSAL}`);
        expect(s.tone).toBe('error');
        // And the wait is over, so the action can be tried again.
        expect(s.waitingAfter).toBe(false);
    });

    test('a refused edit keeps the spool editor\'s own lead', async ({ page }) => {
        const s = await settle(page, 'edit', 'Spoolman is offline');
        expect(s.message).toBe(`${EDIT_LEAD} Spoolman is offline`);
        expect(s.tone).toBe('error');
    });

    test('a refusal that arrives after the wait ended says nothing', async ({ page }) => {
        // A G-code script is answered only once it has run, which for a load is
        // minutes: by then the view has moved on and the printer's own status
        // is what says it.
        const s = await settle(page, 'action', REFUSAL, true);
        expect(s.waiting).toBe(true);
        expect(s.message).toBe('');
        expect(s.tone).toBe('neutral');
    });

    test('an answer that is not a refusal only ends the wait', async ({ page }) => {
        const s = await settle(page, 'action', undefined);
        expect(s.message).toBe('');
        expect(s.waitingAfter).toBe(false);
    });
});

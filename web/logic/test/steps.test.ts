// The progress steps: a label of the user's, a detail on the running step, and
// the two standard steps that joined the first six (H3). The same in both
// cores: `lane.stage` is [plan, index, failed, detail, labels], a three-element
// array is an older printer's and is read exactly as before, and the view is
// what puts a detail beside the running step's own text.

import assert from 'node:assert/strict';
import { test } from 'node:test';
import { bothCores, firstDiff, resetBoth, setLanguageBoth } from './compare.ts';
import type { Pair } from './compare.ts';
import type { Core } from '../types.ts';

const PLAN = ['brushing', 'heat', 'home', 'clean', 'park'];
const LABELS = { brushing: 'Brushing the nozzle', heat: 'Melting the tip' };
const DETAIL = '250 °C';

/** The lane as a printer sends it: the five-element form, the three-element one
 *  an older printer sends, or either with a different running step. */
const lane = (stage: unknown): Record<string, unknown> => ({ id: 'fps1', op: 'LOADING', bay: 'oams1-0', stage });

const unit = (stage: unknown) => ({
  unit: 'oams1',
  family: 'ams1',
  idx: 1,
  bays: [{ s: 'l' }, { s: 'r' }, { s: 'r' }, { s: 'r' }],
  lane: lane(stage),
});

/** Both cores fed the same lane, in the given language. */
async function setup(stage: unknown, lang = 'en'): Promise<Pair> {
  const p = await bothCores();
  assert.equal(setLanguageBoth(p, lang), true);
  resetBoth(p);
  for (const core of [p.c, p.ts]) core.apply('openams_ui', 'oams1', unit(stage));
  return p;
}

const activity = (core: Core) => core.view().toolheads[0].activity;
const bothAgree = (p: Pair, where: string): void => {
  const d = firstDiff(p.c.view(), p.ts.view());
  assert.equal(d, null, `${where}: ${d}`);
};

test('the five-element stage: a label where there is one, the running step with its detail', async () => {
  const p = await setup([PLAN, 1, null, DETAIL, LABELS]);
  for (const core of [p.c, p.ts]) {
    const a = activity(core);
    assert.deepEqual(a.steps, [
      'Brushing the nozzle',                              // a custom step, labeled by the user
      'Melting the tip · 250 °C',                         // a standard step, relabeled, and running
      'Home the axes',                                    // a standard step, translated
      'Clean the nozzle',                                 // a standard step, translated
      'park',                                             // a name the table does not know
    ]);
    assert.equal(a.index, 1);
    assert.equal(a.failed, -1);
  }
  bothAgree(p, 'five elements');
});

test("an older printer's three-element stage is read exactly as before", async () => {
  const p = await setup([['cut', 'feed'], 1, 'feed']);
  for (const core of [p.c, p.ts]) {
    const a = activity(core);
    // The names the printer sent, translated where the table has them, and no
    // detail anywhere: a three-element array carries none.
    assert.deepEqual(a.steps, ['Cut filament', 'Push new filament into extruder']);
    assert.equal(a.index, 1);
    assert.equal(a.failed, 1);
  }
  bothAgree(p, 'three elements');
});

test('a label is shown as written in every language; the standard names are translated', async () => {
  for (const lang of ['en', 'de', 'ja', 'qps']) {
    const p = await setup([PLAN, 1, null, DETAIL, LABELS], lang);
    // The user's words never move: a label is not a name the table knows.
    for (const core of [p.c, p.ts]) assert.equal(activity(core).steps[0], 'Brushing the nozzle');
    // The relabeled step is the user's too, and the detail rides on it. The
    // separator around it belongs to the language, so both parts are looked
    // for rather than the whole sentence.
    for (const core of [p.c, p.ts]) {
      assert.ok(activity(core).steps[1].includes('Melting the tip'), activity(core).steps[1]);
      assert.ok(activity(core).steps[1].includes(DETAIL), activity(core).steps[1]);
    }
    // The two new standard steps are not the user's words in any language.
    for (const core of [p.c, p.ts]) {
      assert.notEqual(activity(core).steps[2], 'home');
      assert.notEqual(activity(core).steps[3], 'clean');
    }
    if (lang === 'en') {
      assert.equal(activity(p.c).steps[2], 'Home the axes');
      assert.equal(activity(p.c).steps[3], 'Clean the nozzle');
    }
    bothAgree(p, lang);
  }
});

test('the detail belongs to the running step alone', async () => {
  // A different running step: the detail moves with it, and the step it left
  // goes back to its own text.
  const p = await setup([PLAN, 4, null, DETAIL, LABELS]);
  for (const core of [p.c, p.ts]) {
    assert.deepEqual(activity(core).steps, [
      'Brushing the nozzle', 'Melting the tip', 'Home the axes', 'Clean the nozzle', `park · ${DETAIL}`,
    ]);
  }
  bothAgree(p, 'a later step running');
});

test('a detail with no running step has nowhere to go, and none is shown', async () => {
  const p = await setup([PLAN, null, null, DETAIL, LABELS]);
  for (const core of [p.c, p.ts]) {
    assert.deepEqual(activity(core).steps, [
      'Brushing the nozzle', 'Melting the tip', 'Home the axes', 'Clean the nozzle', 'park',
    ]);
    assert.equal(activity(core).index, -1);
  }
  bothAgree(p, 'no running step');
});

test('a plan with no labels and no detail is drawn exactly as it always was', async () => {
  const p = await setup([PLAN, 2, null, null, {}]);
  for (const core of [p.c, p.ts]) {
    // A custom name shows as itself; a standard one is translated, whether or
    // not the printer gave it a label.
    assert.deepEqual(activity(core).steps, [
      'brushing', 'Heat the nozzle', 'Home the axes', 'Clean the nozzle', 'park',
    ]);
  }
  bothAgree(p, 'an empty labels object');
});

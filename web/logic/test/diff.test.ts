// The TypeScript logic against the C core (its wasm build): the same views,
// the same action results, everywhere. A logic change is made in C and
// TypeScript together, and CI refuses a merge until this passes.
//
//   1. every recorded fixture: after every event, view() and apply() agree;
//   2. every action of every view, and a set of odd lines, agree;
//   3. the state sheet's scenarios (built through the real mapping) agree;
//   4. compareSequence(): the hook the random-sequence suite drives;
//   5. all of it again in every language the image carries, because the
//      language is a property of both cores, not of a single test.

import assert from 'node:assert/strict';
import { test } from 'node:test';
import {
  applyBoth, bothCores, compareActions, compareSequence, firstDiff, fixtureNames, fixtureSteps, langCodes,
  resetBoth, setLanguageBoth,
} from './compare.ts';
import { cards, tileStates } from './scenarios.ts';
import { build } from './synth.ts';

export { compareSequence } from './compare.ts';

const LANGS = langCodes();

test('both cores carry the same languages', async () => {
  const p = await bothCores();
  for (const code of LANGS) {
    assert.equal(setLanguageBoth(p, code), true);
    const row = p.c.view().settings.find((s) => s.key === 'language');
    // the pseudo-locale is available to setLanguage() but never in the list
    assert.deepEqual(row?.options.map((o) => o.code), LANGS.filter((c) => c !== 'qps'));
    assert.deepEqual(row?.options.map((o) => o.label), p.ts.view().settings.at(-1)?.options.map((o) => o.label));
  }
  assert.equal(p.c.setLanguage('nope'), false, 'an unknown code changes nothing');
  assert.equal(p.ts.setLanguage('nope'), false);
  setLanguageBoth(p, LANGS[0]);
});

test('firstDiff reports a path', () => {
  const c = { toolheads: [{ message: { text: 'a' } }, { message: { text: 'b' } }] };
  const ts = { toolheads: [{ message: { text: 'a' } }, { message: { text: 'c' } }] };
  assert.equal(firstDiff(c, ts), 'toolheads[1].message.text: C "b" vs TS "c"');
  assert.equal(firstDiff({ a: 1, b: [1, 2] }, { b: [1, 2], a: 1 }), null);
  assert.equal(firstDiff([1], [1, 2]), '.length: C 1 vs TS 2'.replace('.', '(root).'));
});

for (const lang of LANGS) {
  for (const name of fixtureNames()) {
    test(`${lang} fixture ${name}: view() and apply() agree after every event`, async () => {
      const p = await bothCores();
      setLanguageBoth(p, lang);
      resetBoth(p);
      const steps = fixtureSteps(name);
      assert.ok(steps.length > 0);
      steps.forEach((s, i) => {
        // Actions are compared after every event of the small fixtures and at
        // every fifth of the long ones; the views after every event always.
        const withActions = steps.length <= 12 || i % 5 === 0 || i === steps.length - 1;
        const d = applyBoth(p, s, `${name} event ${i} (${s.kind})`, withActions);
        assert.equal(d, null, d ?? '');
      });
    });
  }

  test(`${lang} the fixtures replayed twice without a reset`, async () => {
    const p = await bothCores();
    setLanguageBoth(p, lang);
    resetBoth(p);
    for (const name of fixtureNames()) {
      for (const [i, s] of fixtureSteps(name).entries()) {
        const d = applyBoth(p, s, `${name} event ${i}`, false);
        assert.equal(d, null, d ?? '');
      }
    }
    const d = compareActions(p, p.c.view(), 'all fixtures');
    assert.equal(d, null, d ?? '');
  });

  const scenarios = [{ caption: 'tile states', scenario: tileStates() }, ...cards];
  for (const { caption, scenario } of scenarios) {
    test(`${lang} sheet scenario "${caption}": views and actions agree`, async () => {
      const p = await bothCores();
      setLanguageBoth(p, lang);
      // build() resets its core, then feeds the synthetic objects through apply().
      const vc = build(p.c, scenario);
      const vt = build(p.ts, scenario);
      const d = firstDiff(vc, vt);
      assert.equal(d, null, d ?? '');
      const a = compareActions(p, vc, caption);
      assert.equal(a, null, a ?? '');
    });
  }
}

test('compareSequence: the same objects again, and a reset in between, agree', async () => {
  // Its own pair, in English: the language is module state, so this must not
  // inherit whatever the loop above left selected.
  const p = await bothCores();
  setLanguageBoth(p, LANGS[0]);
  const steps = fixtureSteps('two_lanes').map((s) => [s]);
  assert.equal(await compareSequence(steps, p), null);
  assert.equal(await compareSequence([...steps, ...steps], p), null);
});

test('reset(keepAlerts) keeps the history in both', async () => {
  const p = await bothCores();
  setLanguageBoth(p, LANGS[0]);
  resetBoth(p);
  for (const s of fixtureSteps('two_lanes')) applyBoth(p, s, 'two_lanes');
  p.c.apply('component_status', null, { bays: { 'oams1-0': { spool: { remaining_pct: 5 } } } });
  p.ts.apply('component_status', null, { bays: { 'oams1-0': { spool: { remaining_pct: 5 } } } });
  resetBoth(p, true);
  const d = firstDiff(p.c.view(), p.ts.view());
  assert.equal(d, null, d ?? '');
});

// Host text is cut on a character boundary, in both cores, in every language:
// a unit name, a fault sentence and a spool's material and vendor, each of them
// multi-byte glyphs that straddle the end of their fixed buffer.
for (const lang of LANGS) {
  test(`${lang} multi-byte host text is cut whole in both cores`, async () => {
    const p = await bothCores();
    assert.equal(setLanguageBoth(p, lang), true);
    resetBoth(p);
    const glyph = '日';                                     // 3 bytes
    const unit = glyph.repeat(4);                               // 12 bytes into an 8-byte name
    const fault = `a${glyph.repeat(40)}`;                       // 121 bytes into a 96-byte field
    const obj = {
      unit, family: 'ams1', bays: [{ s: 'p' }, { s: 'e' }],
      lane: { op: 'UNLOADED' },
      fault: { code: `E${glyph.repeat(20)}`, text: fault, sev: 'pause', bay: 0, unit },
      nf: 1,
    };
    p.c.apply('openams_ui', unit, obj);
    p.ts.apply('openams_ui', unit, obj);
    const status = {
      spoolman_online: true,
      bays: { [`${unit}-0`]: { spool: {
        material: glyph.repeat(8), vendor: glyph.repeat(9), remaining_g: 50, remaining_pct: 5,
      } } },
    };
    p.c.apply('component_status', null, status);
    p.ts.apply('component_status', null, status);
    const d = firstDiff(p.c.view(), p.ts.view());
    assert.equal(d, null, d ?? '');
  });
}

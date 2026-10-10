// The stepper slot's resting state: what the toolhead's path is while no plan
// runs (PRINCIPLES.md 2, "A reserved slot has a resting state"). The same in
// both cores: the label is composed by the view, never by a renderer.

import assert from 'node:assert/strict';
import { test } from 'node:test';
import { bothCores, firstDiff, resetBoth, setLanguageBoth } from './compare.ts';
import type { Pair } from './compare.ts';
import type { Core, ViewToolhead } from '../types.ts';

/** What one bay's spool summary says; null for a bay with no spool at all. */
interface Spool {
  material?: string;
  color?: string;      // "#rrggbb"; left out means nobody told us the color
}

/** One FPS lane on a two-bay unit: @p op, the group its loaded bay belongs to
 *  (none at all when @p grouped is false), and the plan it is running. */
function objects(op: string, spools: (Spool | null)[], grouped: boolean, stage?: unknown,
  states: [string, string] = ['l', 'e']): unknown[] {
  const groups = grouped ? [{ n: 'T0', b: ['oams1-0'], s: 'r' }] : [];
  return [
    ['openams_ui', 'oams1', {
      unit: 'oams1',
      family: 'ams1',
      idx: 1,
      bays: [
        { id: 0, s: states[0], cal: 'c', a: '', sp: { id: 1, src: 't' } },
        { id: 1, s: states[1], cal: 'c', a: '', sp: null },
      ],
      lane: {
        id: 'fps1', ext: 'extruder', op, bay: 'oams1-0',
        group: grouped ? 'T0' : null, stage: stage ?? null, runout: 'idle', ms: op === 'LOADED',
      },
      groups,
    }],
    ['component_status', null, {
      bays: Object.fromEntries(spools.map((spool, i) => [`oams1-${i}`, {
        spool: spool
          ? { material: spool.material ?? '', vendor: 'Polymaker', ...(spool.color ? { color_hex: spool.color.slice(1) } : {}) }
          : null,
      }])),
    }],
  ];
}

/** Both cores fed the same objects, in the given language. */
async function setup(objs: unknown[], lang = 'en'): Promise<Pair> {
  const p = await bothCores();
  assert.equal(setLanguageBoth(p, lang), true);
  resetBoth(p);
  for (const [kind, name, obj] of objs as [string, string | null, unknown][]) {
    for (const core of [p.c, p.ts]) core.apply(kind, name, obj);
  }
  return p;
}

const rest = (core: Core): ViewToolhead['activity']['rest'] => core.view().toolheads[0].activity.rest;
const bothAgree = (p: Pair, where: string): void => {
  const d = firstDiff(p.c.view(), p.ts.view());
  assert.equal(d, null, `${where}: ${d}`);
};

test('nothing loaded, a bay ready: the slot says what could be loaded, and carries no color', async () => {
  const p = await setup(objects('UNLOADED', [{ material: 'ABS', color: '#1e90ff' }, null], true, undefined, ['r', 'e']));
  for (const core of [p.c, p.ts]) {
    assert.deepEqual(rest(core), { label: 'Ready: T0', color: null, outline: false, loaded: false });
  }
  bothAgree(p, 'nothing loaded, ready');
});

test('nothing loaded and no bay ready: the next step', async () => {
  // an empty bay, and a bay in no group, name no tool
  for (const [grouped, states] of [[true, ['e', 'e']], [false, ['r', 'e']]] as [boolean, [string, string]][]) {
    const p = await setup(objects('UNLOADED', [{ material: 'ABS', color: '#1e90ff' }, null], grouped, undefined, states));
    for (const core of [p.c, p.ts]) {
      assert.deepEqual(rest(core), { label: 'Insert a spool', color: null, outline: false, loaded: false });
    }
    bothAgree(p, `nothing ready (grouped ${grouped})`);
  }
});

test('several tools ready: joined, in the model\'s group order', async () => {
  const objs = objects('UNLOADED', [{ material: 'ABS' }, { material: 'PLA' }], true, undefined, ['r', 'r']);
  // declared T1 first; the model keeps its groups ordered by tool number
  (objs[0] as [string, string, { groups: unknown[] }])[2].groups = [
    { n: 'T1', b: ['oams1-1'], s: 'r' }, { n: 'T0', b: ['oams1-0'], s: 'r' },
  ];
  const p = await setup(objs);
  for (const core of [p.c, p.ts]) {
    assert.equal(rest(core)?.label, 'Ready: T0 · T1');
  }
  bothAgree(p, 'two tools ready');
});

test('a loaded bay rests as tool, bay and material, in the filament\'s own color', async () => {
  const p = await setup(objects('LOADED', [{ material: 'ABS', color: '#1e90ff' }, null], true));
  for (const core of [p.c, p.ts]) {
    assert.deepEqual(rest(core), { label: 'T0 · oams1:1 · ABS', color: '#1e90ff', outline: false, loaded: true });
  }
  bothAgree(p, 'a loaded bay');
});

test('a material nobody told us stops the path at the bay', async () => {
  const p = await setup(objects('LOADED', [{ color: '#1e90ff' }, null], true));
  for (const core of [p.c, p.ts]) {
    assert.deepEqual(rest(core), { label: 'T0 · oams1:1', color: '#1e90ff', outline: false, loaded: true });
  }
  bothAgree(p, 'no material');
});

test('a color nobody told us: the path is still named, the segment stays neutral', async () => {
  const p = await setup(objects('LOADED', [{ material: 'ABS' }, null], true));
  for (const core of [p.c, p.ts]) {
    assert.deepEqual(rest(core), { label: 'T0 · oams1:1 · ABS', color: null, outline: false, loaded: true });
  }
  bothAgree(p, 'no color');
});

test('a bay in no group: "?" for the tool, the name the hotend icon has for it', async () => {
  const p = await setup(objects('LOADED', [{ material: 'ABS', color: '#1e90ff' }, null], false));
  for (const core of [p.c, p.ts]) {
    assert.deepEqual(rest(core), { label: '? · oams1:1 · ABS', color: '#1e90ff', outline: false, loaded: true });
  }
  bothAgree(p, 'no group');
});

test('while a plan runs there is no resting value at all', async () => {
  const p = await setup(objects('LOADING', [{ material: 'ABS', color: '#1e90ff' }, null], true, [['cut', 'feed'], 1, null]));
  for (const core of [p.c, p.ts]) {
    assert.equal(rest(core), null);
    assert.equal(core.view().toolheads[0].activity.steps.length, 2);
  }
  bothAgree(p, 'a plan running');
});

test('the resting label is the language\'s own, and both cores agree in each', async () => {
  // One scenario at a time: bothCores() hands back the same wasm instance
  // every time, so a second scenario would move the first pair's answer.
  for (const lang of ['en', 'de', 'ja', 'qps']) {
    const loaded = await setup(objects('LOADED', [{ material: 'ABS', color: '#1e90ff' }, null], true), lang);
    // "T0" and "ABS" are the user's own words and never move; the bay label
    // and the separators belong to the language.
    for (const core of [loaded.c, loaded.ts]) {
      const label = rest(core)?.label ?? '';
      assert.ok(label.includes('T0'), label);
      assert.ok(label.includes('ABS'), label);
      if (lang === 'en') assert.equal(label, 'T0 · oams1:1 · ABS');
    }
    bothAgree(loaded, lang);

    // The empty path is the language's own sentence, never the English one.
    const empty = await setup(objects('UNLOADED', [{ material: 'ABS', color: '#1e90ff' }, null], true, undefined, ['r', 'e']), lang);
    for (const core of [empty.c, empty.ts]) {
      const label = rest(core)?.label ?? '';
      assert.equal(label.includes('Spool'), false, label);
      if (lang === 'en') assert.equal(label, 'Ready: T0');
      else assert.notEqual(label, 'Ready: T0', label);
    }
    bothAgree(empty, `${lang} empty`);
  }
});

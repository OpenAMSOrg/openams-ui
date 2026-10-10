// The differential tests' state: seeded scenarios fed to the real core, and
// the rules every view has to obey. Seeds 1..300, 20 steps each.
import test from 'node:test';
import assert from 'node:assert/strict';
import { loadCore } from '../../core/index.js';
import type { Core, View, ViewAction, ViewField, ViewToolhead } from '../../core/index.js';
import { mulberry32 } from './gen/rng.ts';
import { applyAll, events } from './gen/objects.ts';
import { randomScenario, randomSequence } from './gen/random.ts';
import { checkView } from './gen/invariants.ts';

const SEEDS = 300;
const STEPS = 20;

const core: Core = await loadCore();

/** Every action in @p v with the form it would be clicked with. */
function actions(v: View): { line: string; form: Record<string, string | number> | null }[] {
  const out: { line: string; form: Record<string, string | number> | null }[] = [];
  const form = (a: ViewAction) => {
    if (!a.form) return null;
    const f: Record<string, string | number> = {};
    for (const field of a.form.fields) f[field.id] = firstValue(field);
    return f;
  };
  const add = (list: ViewAction[] | undefined) => {
    for (const a of list ?? []) out.push({ line: a.line, form: form(a) });
  };
  for (const th of v.toolheads) {
    add(th.actions);
    add(th.alert?.items.flatMap((i) => i.actions));
    for (const u of th.units) {
      add(u.actions);
      add(u.alert?.items.flatMap((i) => i.actions));
      for (const b of u.bays) add(b.actions);
    }
  }
  for (const u of v.unassigned_units) {
    add(u.actions);
    for (const b of u.bays) add(b.actions);
  }
  for (const a of v.alerts) add(a.actions);
  for (const s of v.settings) add([s.action_on, s.action_off].filter((a): a is ViewAction => a !== null));
  return out;
}

/** A form's first option, or "" when the field offers none. */
function firstValue(field: ViewField): string | number {
  return field.options && field.options.length > 0 ? field.options[0].value : '';
}

const cover = { tags: new Set<string>(), activity: new Set<string>(), levels: new Set<string>(), tones: new Set<string>(), notices: 0 };

function collect(v: View): void {
  for (const th of v.toolheads) {
    cover.activity.add(th.activity.kind);
    cover.tones.add(th.message.tone);
    for (const a of th.alert?.items ?? []) cover.levels.add(a.severity);
    for (const u of th.units) {
      for (const a of u.alert?.items ?? []) cover.levels.add(a.severity);
      for (const b of u.bays) if (b.tag) cover.tags.add(b.tag.text);
    }
  }
  for (const a of v.alerts) cover.levels.add(a.severity);
  if (v.notice) cover.notices += 1;
}

/** Feed @p seq to the core: one reset, then every step on top of the last. */
function play(seq: ReturnType<typeof randomSequence>): View[] {
  core.reset(false);
  const views: View[] = [];
  for (const sc of seq) {
    applyAll(core, events(sc));
    views.push(core.view());
  }
  return views;
}

test('the same seed gives the same sequence', () => {
  for (const seed of [1, 2, 7, 99, 300]) {
    assert.deepEqual(randomSequence(mulberry32(seed), STEPS), randomSequence(mulberry32(seed), STEPS));
  }
  assert.deepEqual(randomScenario(mulberry32(42)), randomScenario(mulberry32(42)));
});

test('the rules hold at every step of every sequence', () => {
  const failed: string[] = [];
  for (let seed = 1; seed <= SEEDS && failed.length < 3; seed++) {
    const seq = randomSequence(mulberry32(seed), STEPS);
    for (const [step, v] of play(seq).entries()) {
      for (const bad of checkView(v)) {
        failed.push(`seed ${seed} step ${step}: ${bad}\n    ${JSON.stringify(events(seq[step]))}`);
      }
      collect(v);
      if (failed.length >= 3) break;
    }
  }
  for (const f of failed) console.error(`FAIL ${f}`);
  assert.deepEqual(failed, []);
});

test('a fresh scenario is sound on its own', () => {
  const failed: string[] = [];
  for (let seed = 1; seed <= SEEDS && failed.length < 3; seed++) {
    const sc = randomScenario(mulberry32(seed * 31 + 5));
    core.reset(false);
    applyAll(core, events(sc));
    for (const bad of checkView(core.view())) failed.push(`seed ${seed}: ${bad}\n    ${JSON.stringify(sc)}`);
  }
  for (const f of failed) console.error(`FAIL ${f}`);
  assert.deepEqual(failed, []);
});

test('every action in every view resolves', () => {
  const failed: string[] = [];
  for (let seed = 1; seed <= SEEDS && failed.length < 3; seed++) {
    const seq = randomSequence(mulberry32(seed), STEPS);
    for (const [step, v] of play(seq).entries()) {
      for (const a of actions(v)) {
        try {
          core.action(a.line, a.form);
        } catch (err) {
          failed.push(`seed ${seed} step ${step}: action("${a.line}") threw ${err}`);
        }
      }
      if (failed.length >= 3) break;
    }
  }
  for (const f of failed) console.error(`FAIL ${f}`);
  assert.deepEqual(failed, []);
});

test('coverage', () => {
  const line = `coverage: tags=[${[...cover.tags].sort().join(' ')}] activity=[${[...cover.activity].sort().join(' ')}]`
    + ` levels=[${[...cover.levels].sort().join(' ')}] tones=[${[...cover.tones].sort().join(' ')}] notices=${cover.notices}`;
  console.log(line);
  // index.d.ts declares tag.text as a plain string, with no union of tag kinds
  // to check the tag coverage against, so only the activity kinds are asserted:
  // index.d.ts declares that union, and every kind has to show up.
  const kinds: ViewToolhead['activity']['kind'][] = ['idle', 'loading', 'unloading', 'runout', 'error'];
  assert.deepEqual([...cover.activity].sort(), [...kinds].sort());
});

// Two-tag RFID on units whose readers several bays share (the host's tag
// attribution, published per bay in openams_ui): the projection's `tg`, `tc`,
// `tr` and `to` per bay, the bay's note (`tile.tag_read`), and "It is this slot"
// (`tag_assert <slot>` -> OAMS_RFID_SCAN OAMS=<idx> BAY=<bay> ASSERT=1). Both
// cores must say the same in every language, so every case compares them.
//
// The fixture is hand-written (tests/fixtures/two_tag/two_tag.json, its
// $comment lists what each bay is): the recorded fixtures cannot carry these
// keys until the host's schema does.

import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import { test } from 'node:test';
import { bothCores, compareActions, firstDiff, langCodes, repoRoot, resetBoth, setLanguageBoth } from './compare.ts';
import type { FixtureEvent, Pair, Step } from './compare.ts';
import type { Core, View, ViewAction, ViewTile } from '../types.ts';

const FIXTURE = new URL('tests/fixtures/two_tag/two_tag.json', repoRoot);

const UNKNOWN = 'Tag not read yet. It is read at the first load.';
const CANDIDATE = 'Tag seen, slot not confirmed';
const TWO_SPOOLS = 'Two spools answered. Load or re-read to settle.';
const CHANGED = "A different spool's tag appeared here. Re-read to settle.";
const NO_TAG = 'No tag found after a turn.';
const DISAGREE = 'The tag reads disagree. Re-read to settle.';
const claimed = (slot: string): string => `This tag is also in slot ${slot}.`;

function fixture(): { steps: Step[]; units: Map<string, Record<string, unknown>> } {
  const doc = JSON.parse(readFileSync(FIXTURE, 'utf8')) as { events: FixtureEvent[] };
  const steps = doc.events.map((e) => (
    e.kind === 'c2_status'
      ? { kind: 'oams_manager', name: null, obj: e.payload }
      : { kind: e.kind, name: e.unit ?? null, obj: e.payload }));
  const units = new Map<string, Record<string, unknown>>();
  for (const e of doc.events) {
    if (e.kind === 'openams_ui' && e.unit) units.set(e.unit, e.payload as Record<string, unknown>);
  }
  return { steps, units };
}

/** Apply one object to both cores; what apply() returned must agree. */
function applyBoth(p: Pair, s: Step): void {
  const rc = p.c.apply(s.kind, s.name, s.obj);
  const rt = p.ts.apply(s.kind, s.name, s.obj);
  assert.equal(rc, rt, `apply(${s.kind}, ${s.name}): C ${rc} vs TS ${rt}`);
}

function bothAgree(p: Pair, where: string): void {
  const view = p.c.view();
  const d = firstDiff(view, p.ts.view());
  assert.equal(d, null, `${where}: ${d}`);
  const a = compareActions(p, view, where);
  assert.equal(a, null, a ?? '');
}

async function setup(code = 'en'): Promise<Pair> {
  const p = await bothCores();
  assert.equal(setLanguageBoth(p, code), true);
  resetBoth(p);
  for (const s of fixture().steps) applyBoth(p, s);
  return p;
}

/** Every bay of the view, by slot id. */
function tiles(view: View): Map<string, ViewTile> {
  const out = new Map<string, ViewTile>();
  for (const th of view.toolheads) for (const u of th.units) for (const t of u.bays) out.set(t.slot_id, t);
  for (const u of view.unassigned_units) for (const t of u.bays) out.set(t.slot_id, t);
  return out;
}

function tile(core: Core, slot: string): ViewTile {
  const t = tiles(core.view()).get(slot);
  assert.ok(t, `no bay ${slot}`);
  return t;
}

const action = (t: ViewTile, id: string): ViewAction | undefined => t.actions.find((a) => a.id === id);

/** What both cores answer for `line`, after checking they agree. */
function sendBoth(p: Pair, line: string): unknown {
  const c = p.c.action(line);
  const d = firstDiff(c, p.ts.action(line));
  assert.equal(d, null, `${line}: ${d}`);
  return c;
}

test('both cores agree on every bay of the two-tag fixture, in every language', async () => {
  const p = await bothCores();
  for (const code of langCodes()) {
    assert.equal(setLanguageBoth(p, code), true);
    resetBoth(p);
    for (const s of fixture().steps) {
      applyBoth(p, s);
      const d = firstDiff(p.c.view(), p.ts.view());
      assert.equal(d, null, `${code}, after ${s.kind} ${s.name}: ${d}`);
    }
    bothAgree(p, code);
    // every note is in the language, never cut: the composed one included
    for (const t of tiles(p.c.view()).values()) {
      if (!t.tag_read) continue;
      assert.notEqual(t.tag_read.text, '', `${code} ${t.slot_id}: an empty note`);
      if (code !== 'en' && code !== 'qps') continue;
      assert.ok(!t.tag_read.text.includes('{'), `${code} ${t.slot_id}: a placeholder left in "${t.tag_read.text}"`);
    }
  }
});

test('unknown: the plain note, no warning color, Re-read enabled and nothing to assert', async () => {
  const p = await setup();
  for (const core of [p.c, p.ts]) {
    const t = tile(core, 'oams11');
    assert.deepEqual(t.tag_read, { state: 'unknown', tone: 'neutral', text: UNKNOWN, candidates: [] });
    assert.equal(action(t, 'reread')?.enabled, true);
    assert.equal(action(t, 'reread')?.target, 'ring');
    assert.equal(action(t, 'tag_assert'), undefined);
    // `tc` and `tr` sent beside `tg` "u" are not this state's
    assert.deepEqual(tile(core, 'oams44').tag_read, { state: 'unknown', tone: 'neutral', text: UNKNOWN, candidates: [] });
  }
});

test('candidate: the note with the tag\'s material and color, "It is this slot" and Re-read', async () => {
  const p = await setup();
  for (const core of [p.c, p.ts]) {
    const t = tile(core, 'oams12');
    assert.deepEqual(t.tag_read, {
      state: 'candidate', tone: 'neutral', text: CANDIDATE,
      candidates: [{ material: 'PLA', color: '#d23b3b', color_name: 'red' }],
    });
    const assertAct = action(t, 'tag_assert');
    assert.ok(assertAct);
    assert.equal(assertAct.label, 'It is this slot');
    assert.equal(assertAct.line, 'tag_assert oams12');
    assert.equal(assertAct.enabled, true);
    assert.equal(assertAct.target, 'ring');
    // in the ring's menu, just before Re-read
    const ids = t.actions.map((a) => a.id);
    assert.equal(ids.indexOf('tag_assert') + 1, ids.indexOf('reread'));
    // the tile keeps its Spoolman link (a manual one here), and no RFID mark
    assert.equal(t.rfid, false);
    assert.equal(t.tag, null);

    // two candidates: one with neither material nor color, one cut to the
    // model's 15 bytes; a third is not kept
    assert.deepEqual(tile(core, 'oams41').tag_read?.candidates, [
      { material: '', color: null, color_name: '' },
      { material: 'PETG-CF Translu', color: '#00ae42', color_name: 'green' },
    ]);
    // an unreadable color is no color; a `tc` that is not a list is no candidate
    assert.deepEqual(tile(core, 'oams42').tag_read?.candidates, [{ material: 'ABS', color: null, color_name: '' }]);
    assert.deepEqual(tile(core, 'oams52').tag_read?.candidates, []);
  }
});

test('conflict: the amber info look, with the text of its reason', async () => {
  const p = await setup();
  const want: [string, string][] = [
    ['oams13', claimed('oams2:2')],     // `to` 5 is oams2's bay 2
    ['oams21', TWO_SPOOLS],             // two_moving
    ['oams22', claimed('oams1:3')],     // `to` 2 is oams1's bay 3
    ['oams23', CHANGED],
    ['oams24', NO_TAG],
    ['oams31', TWO_SPOOLS],             // ambiguous_pair
    ['oams32', DISAGREE],               // a reason this build does not know
    ['oams33', claimed('99')],          // a slot id no bay of this model has
    ['oams34', claimed('?')],           // claimed_twice without `to`
  ];
  for (const core of [p.c, p.ts]) {
    for (const [slot, text] of want) {
      const t = tile(core, slot);
      assert.deepEqual(t.tag_read, { state: 'conflict', tone: 'info', text, candidates: [] }, slot);
      // never the fault look on the tile: its tag and state are the bay's own
      assert.equal(t.tag, null, slot);
      assert.equal(t.state, 'unknown', slot);
      assert.equal(action(t, 'tag_assert'), undefined, slot);
      assert.equal(action(t, 'reread')?.enabled, true, slot);
    }
  }
});

test('nothing to say: attributed, no `tg`, or a letter this build does not know', async () => {
  const p = await setup();
  for (const core of [p.c, p.ts]) {
    for (const slot of ['oams14', 'oams43', 'oams54']) {
      const t = tile(core, slot);
      assert.equal('tag_read' in t, false, slot);
      assert.equal(action(t, 'tag_assert'), undefined, slot);
    }
  }
});

test('the tile\'s RFID mark keeps its rule: the link source, whatever `tg` says', async () => {
  const p = await setup();
  for (const core of [p.c, p.ts]) {
    assert.equal(tile(core, 'oams14').rfid, true);     // attributed, linked from its tag
    assert.equal(tile(core, 'oams12').rfid, false);    // a candidate with a manual link
    assert.equal(tile(core, 'oams11').rfid, false);    // unknown, not linked
    assert.equal(tile(core, 'oams53').rfid, true);     // `src` "t", even beside a candidate
  }
});

test('tag_assert: OAMS_RFID_SCAN OAMS=<idx> BAY=<bay> ASSERT=1, only for a candidate', async () => {
  const p = await setup();
  assert.deepEqual(sendBoth(p, 'tag_assert oams12'), { kind: 'gcode', script: 'OAMS_RFID_SCAN OAMS=1 BAY=1 ASSERT=1' });
  assert.deepEqual(sendBoth(p, 'tag_assert oams41'), { kind: 'gcode', script: 'OAMS_RFID_SCAN OAMS=4 BAY=0 ASSERT=1' });
  // Re-read is unchanged
  assert.deepEqual(sendBoth(p, 'reread oams12'), { kind: 'gcode', script: 'OAMS_RFID_SCAN OAMS=1 BAY=1' });
  // a bay that is not a candidate
  for (const slot of ['oams11', 'oams13', 'oams14', 'oams43']) {
    assert.deepEqual(sendBoth(p, `tag_assert ${slot}`), { kind: 'error', reason: 'Not available right now' }, slot);
  }
  // an empty bay, a unit that does not list rfid_scan, a bay nobody has
  assert.deepEqual(sendBoth(p, 'tag_assert oams42'), { kind: 'error', reason: 'No spool is inserted' });
  assert.equal(action(tile(p.c, 'oams42'), 'tag_assert')?.reason, 'No spool is inserted');
  assert.deepEqual(sendBoth(p, 'tag_assert oams51'), { kind: 'error', reason: 'Not supported on this unit yet' });
  assert.deepEqual(sendBoth(p, 'tag_assert oams99'), { kind: 'local' });
  // a unit with no reader offers neither tag action, but still says what it knows
  for (const core of [p.c, p.ts]) {
    const t = tile(core, 'oams51');
    assert.equal(action(t, 'tag_assert'), undefined);
    assert.equal(action(t, 'reread'), undefined);
    assert.equal(t.tag_read?.state, 'candidate');
  }
});

test('a later projection replaces the bay\'s attribution whole', async () => {
  const p = await setup();
  const ui = structuredClone(fixture().units.get('oams1')) as { bays: Record<string, unknown>[] };
  // bay 0: no `tg` any more; bay 1: attributed, its `tc` gone; bay 2: a candidate
  delete ui.bays[0].tg;
  ui.bays[1] = { ...ui.bays[1], tg: 'a', sp: { id: 7, src: 't' } };
  delete ui.bays[1].tc;
  ui.bays[2] = { ...ui.bays[2], tg: 'c', tc: [{ m: 'TPU', c: '5E43B7' }] };
  delete ui.bays[2].tr;
  delete ui.bays[2].to;
  applyBoth(p, { kind: 'openams_ui', name: 'oams1', obj: ui });
  bothAgree(p, 'after the update');
  for (const core of [p.c, p.ts]) {
    assert.equal('tag_read' in tile(core, 'oams11'), false);
    const t = tile(core, 'oams12');
    assert.equal('tag_read' in t, false);
    assert.equal(t.rfid, true);
    assert.equal(action(t, 'tag_assert'), undefined);
    assert.deepEqual(tile(core, 'oams13').tag_read, {
      state: 'candidate', tone: 'neutral', text: CANDIDATE,
      candidates: [{ material: 'TPU', color: '#5e43b7', color_name: 'purple' }],
    });
  }
  // a diff without `bays` keeps what the bays said (the core merges it)
  applyBoth(p, { kind: 'openams_ui', name: 'oams1', obj: { online: true } });
  bothAgree(p, 'after a diff without bays');
  assert.equal(tile(p.c, 'oams13').tag_read?.state, 'candidate');
});

test('the composed note fits its buffer in the pseudo-locale with a long unit name', async () => {
  const p = await bothCores();
  assert.equal(setLanguageBoth(p, 'qps'), true);
  resetBoth(p);
  const long = 'u'.repeat(31);
  const bays = [
    { id: 0, s: 'r', cal: 'c', a: 'lcad', sp: null, tg: 'x', tr: 'claimed_twice', to: 1 },
    { id: 1, s: 'r', cal: 'c', a: 'lcad', sp: null, tg: 'x', tr: 'claimed_twice', to: 0 },
  ];
  applyBoth(p, {
    kind: 'openams_ui', name: long,
    obj: { unit: long, idx: 1, family: 'ams1', online: true, ready: true, act: ['rfid_scan'], bays,
      lane: { id: 'fps1', op: 'UNLOADED' }, groups: [] },
  });
  bothAgree(p, 'qps, a 31-byte unit name');
  const t = tiles(p.c.view());
  for (const v of t.values()) {
    const text = v.tag_read?.text ?? '';
    assert.ok(text.endsWith(']'), `the pseudo-locale's note is whole: "${text}"`);
  }
});

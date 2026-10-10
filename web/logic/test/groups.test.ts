// "Change group..." and the groups list: the tile action's own line, the exact
// G-code each choice of its list maps to, and the list the renderer draws.
// Both cores are asked the same questions through the same scenario and must
// answer them the same way; the scripts are spelled out here so a mapping that
// quietly changes shows up as a diff rather than as a different set of
// commands.

import assert from 'node:assert/strict';
import { test } from 'node:test';
import { bothCores, firstDiff, resetBoth } from './compare.ts';
import type { Pair } from './compare.ts';
import { build } from './synth.ts';
import type { Scenario } from './synth.ts';
import type { ActionResult, Core, ViewAction, ViewUnit } from '../types.ts';

const spool = (color: string, pct: number, over = {}) => ({ s: 'r', material: 'ASA', color, pct, link: 'm', ...over });

/** Two units on one lane. T0 holds the bay the lane is running; T1 is
 *  declared but empty; T2 spans both units, so it shows one bay named like a
 *  tile and one named with its unit. */
const twoUnits: Scenario = {
  units: [
    { name: 'oams1', idx: 1, family: 'ams2', lane: 'fps1', bays: [
      spool('#1c1c1c', 88, { s: 'l', link: 't' }), spool('#d32f2f', 46), { s: 'e' }, spool('#2f8cf0', 66) ] },
    { name: 'oams2', idx: 2, family: 'ams2', lane: 'fps1', bays: [ spool('#3db84c', 71) ] },
  ],
  groups: { T0: ['oams1-0'], T1: [], T2: ['oams2-0', 'oams1-3'] },
  lanes: [{ id: 'fps1', ext: 'extruder', op: 'LOADED', bay: 'oams1-0', group: 'T0' }],
};

/** Both cores run @p sc and return a pair of views, asserted identical. */
async function view(p: Pair, sc: Scenario): Promise<[Core, Core]> {
  build(p.c, sc);
  build(p.ts, sc);
  const d = firstDiff(p.c.view(), p.ts.view());
  assert.equal(d, null, d ?? '');
  return [p.c, p.ts];
}

const unitOf = (core: Core, name: string): ViewUnit =>
  core.view().toolheads.flatMap((t) => t.units).find((u) => u.id === name)!;

const changeGroup = (core: Core, unit: string, bay: number): ViewAction =>
  unitOf(core, unit).bays[bay].actions.find((a) => a.id === 'change_group')!;

/** An action line resolved by both cores; they must agree. */
function both(p: Pair, line: string, form?: Record<string, string | number | boolean>): ActionResult {
  const c = p.c.action(line, form);
  const ts = p.ts.action(line, form);
  const d = firstDiff(c, ts, `action(${JSON.stringify(line)})`);
  assert.equal(d, null, d ?? '');
  return ts;
}

const gcode = (r: ActionResult): string[] => (r.kind === 'gcode' ? r.script.split('\n') : []);

test('the bay lists Change group... in its fixed place, with the core\'s words', async () => {
  const p = await bothCores();
  for (const core of await view(p, twoUnits)) {
    const ids = unitOf(core, 'oams1').bays[0].actions.map((a) => a.id);
    assert.deepEqual(ids.slice(0, 7), [
      'load', 'unload', 'edit_spool', 'spool_list_refresh', 'unlink', 'change_group', 'reread',
    ]);
    const a = changeGroup(core, 'oams1', 0);
    assert.equal(a.label, 'Change group…');
    assert.equal(a.line, 'change group {group} oams11');
    assert.equal(a.target, 'ring');
    // Bay 0 holds the loaded group, so this one is the dimmed example; bay 1,
    // in no group, is the live one.
    assert.equal(a.enabled, false);
    assert.equal(changeGroup(core, 'oams1', 1).enabled, true);
  }
});

test('the list is every group on this lane, then "No group" and "New group"', async () => {
  const p = await bothCores();
  for (const core of await view(p, twoUnits)) {
    const f = changeGroup(core, 'oams1', 1).form!.fields[0];
    assert.equal(f.id, 'group');
    assert.equal(f.label, 'Group');
    assert.equal(f.kind, 'select');
    assert.deepEqual(f.options, [
      { value: 0, label: 'T0' },
      { value: 1, label: 'T1' },
      { value: 2, label: 'T2' },
      { value: -1, label: 'No group' },
      { value: -2, label: 'New group' },
    ]);
    // Bay 0's default is its own group: submitting unchanged moves nothing.
    assert.equal(changeGroup(core, 'oams1', 0).form!.fields[0].value, 0);
    // Bay 1 is in no group, and "No group" is the one choice the mapper
    // refuses on it, so its default is the lane's first group that is not the
    // one loaded in the toolhead - T0 is loaded, T1 is next.
    assert.equal(f.value, 1);
  }
});

test('a group of another lane is not offered', async () => {
  const p = await bothCores();
  const sc: Scenario = {
    units: [
      { name: 'oams1', idx: 1, family: 'ams2', lane: 'fps1', bays: [ spool('#1c1c1c', 88), { s: 'e' } ] },
      { name: 'oams2', idx: 2, family: 'ams2', lane: 'fps2', bays: [ spool('#d32f2f', 46) ] },
    ],
    groups: { T0: ['oams1-0'], T1: ['oams2-0'] },
    lanes: [
      { id: 'fps1', ext: 'extruder', op: 'LOADED', bay: 'oams1-0', group: 'T0' },
      { id: 'fps2', ext: 'extruder', op: 'LOADED', bay: 'oams2-0', group: 'T1' },
    ],
  };
  for (const core of await view(p, sc)) {
    assert.deepEqual(changeGroup(core, 'oams1', 1).form!.fields[0].options, [
      { value: 0, label: 'T0' },
      { value: -1, label: 'No group' },
      { value: -2, label: 'New group' },
    ]);
    // The other lane's unit sees only its own group, plus the two sentinels.
    assert.deepEqual(changeGroup(core, 'oams2', 0).form!.fields[0].options.map((o) => o.label),
      ['T1', 'No group', 'New group']);
  }
});

test('the form opens on a choice the host accepts', async () => {
  const p = await bothCores();
  const sc: Scenario = {
    units: [{ name: 'oams1', idx: 1, family: 'ams2', lane: 'fps1', bays: [
      spool('#1c1c1c', 88, { s: 'l', link: 't' }), spool('#d32f2f', 46) ] }],
    groups: { T0: ['oams1-0'] },
    lanes: [{ id: 'fps1', ext: 'extruder', op: 'LOADED', bay: 'oams1-0', group: 'T0' }],
  };
  for (const core of await view(p, sc)) {
    // Bay 1 is in no group and the lane's only group is the loaded one, which
    // no bay may join - so the list ends in "New group", which is what it
    // opens on, and the action is live.
    const a = changeGroup(core, 'oams1', 1);
    assert.deepEqual(a.form!.fields[0].options.map((o) => o.label), ['T0', 'No group', 'New group']);
    assert.equal(a.form!.fields[0].value, -2);
    assert.equal(a.enabled, true);
  }
  // Which is what submitting it unchanged sends.
  assert.deepEqual(gcode(both(p, 'change group {group} oams12', { group: -2 })), [
    'OAMSM_CREATE_GROUP GROUP=T1',
    'OAMSM_ASSIGN_BAY GROUP=T1 OAMS=oams1 BAY=1',
  ]);
});

test('each choice maps to the command(s) it means', async () => {
  const p = await bothCores();
  await view(p, twoUnits);

  // a group's own index: an assign, naming the group by what it is
  assert.deepEqual(gcode(both(p, 'change group 1 oams12')), [
    'OAMSM_ASSIGN_BAY GROUP=T1 OAMS=oams1 BAY=1',
  ]);
  // "No group": an unassign, naming the group the bay is leaving
  assert.deepEqual(gcode(both(p, 'change group -1 oams14')), [
    'OAMSM_UNASSIGN_BAY GROUP=T2 OAMS=oams1 BAY=3',
  ]);
  // "New group": the next free T<n>, created and then filled, in one script
  assert.deepEqual(gcode(both(p, 'change group -2 oams12')), [
    'OAMSM_CREATE_GROUP GROUP=T3',
    'OAMSM_ASSIGN_BAY GROUP=T3 OAMS=oams1 BAY=1',
  ]);
  // "No group" on a bay that is in none: the host has no group to name
  assert.equal(both(p, 'change group -1 oams12').kind, 'error');
  // A choice outside the table and a bay the model does not hold go to nobody:
  // nothing is sent rather than something wrong.
  assert.equal(both(p, 'change group 7 oams12').kind, 'local');
  assert.equal(both(p, 'change group -3 oams12').kind, 'local');
  assert.equal(both(p, 'change group 1 oams99').kind, 'local');
});

test('a form submits as the line the view wrote', async () => {
  const p = await bothCores();
  await view(p, twoUnits);
  // What the panel does: it fills {group} with the chosen value and sends.
  assert.deepEqual(gcode(both(p, 'change group {group} oams12', { group: -2 })), [
    'OAMSM_CREATE_GROUP GROUP=T3',
    'OAMSM_ASSIGN_BAY GROUP=T3 OAMS=oams1 BAY=1',
  ]);
  assert.deepEqual(gcode(both(p, 'change group {group} oams12', { group: 2 })), [
    'OAMSM_ASSIGN_BAY GROUP=T2 OAMS=oams1 BAY=1',
  ]);
});

test('each refusal names its own rule, on the action and on the group', async () => {
  const p = await bothCores();
  for (const core of await view(p, twoUnits)) {
    const a = changeGroup(core, 'oams1', 0);
    assert.equal(a.enabled, false);
    assert.equal(a.reason, 'This group is loaded in the toolhead');
    // Dimmed, never hidden: it is still in its place in the list.
    assert.ok(unitOf(core, 'oams1').bays[0].actions.some((x) => x.id === 'change_group'));

    const g = unitOf(core, 'oams1').groups.find((x) => x.name === 'T0')!;
    assert.equal(g.editable, false);
    assert.equal(g.reason, 'This group is loaded in the toolhead');
    const del = g.actions[0];
    assert.equal(del.id, 'delete_group');
    assert.equal(del.label, 'Delete group');
    assert.equal(del.line, 'delete group T0');
    assert.equal(del.enabled, false);
    assert.equal(del.reason, 'This group is loaded in the toolhead');
  }
  assert.equal(both(p, 'change group 0 oams12').kind, 'error');
  assert.equal(both(p, 'delete group T0').kind, 'error');
  assert.deepEqual(gcode(both(p, 'delete group T1')), ['OAMSM_DELETE_GROUP GROUP=T1']);
  assert.equal(both(p, 'delete group T9').kind, 'local');
});

test('the groups list holds every group that holds one of this unit\'s bays', async () => {
  const p = await bothCores();
  for (const core of await view(p, twoUnits)) {
    const u1 = unitOf(core, 'oams1');
    // T0 holds bay 0 of oams1 and T2 holds bay 3; T1 is declared but empty and
    // holds none, so it is a destination in the list above and not in this
    // unit's list.
    assert.deepEqual(u1.groups.map((g) => g.name), ['T0', 'T2']);

    // T0: this unit's only bay, named like its tile, with its state.
    assert.equal(u1.groups[0].members.length, 1);
    const mine = u1.groups[0].members[0];
    assert.equal(mine.slot_id, 'oams11');
    assert.equal(mine.bay, 0);
    assert.equal(mine.mine, true);
    assert.equal(mine.dim, false);
    assert.equal(mine.label, 'Spool 1');
    assert.equal(mine.state, 'Loaded');
    assert.equal(mine.material, 'ASA');

    // T2 spans the two units, in the order its own list declares: the other
    // unit's bay first, named with its unit and dimmed, this unit's bay named
    // like a tile; both carry their state and material.
    assert.deepEqual(u1.groups[1].members.map((m) => [m.slot_id, m.mine]), [['oams21', false], ['oams14', true]]);
    const other = u1.groups[1].members[0];
    assert.equal(other.dim, true);
    assert.equal(other.label, 'oams2 Spool 1');
    assert.notEqual(other.state, '');
    assert.equal(other.material, unitOf(core, 'oams2').bays[0].material);

    // The same group under the other unit, in the group's own member order.
    const u2 = unitOf(core, 'oams2');
    assert.deepEqual(u2.groups.map((g) => g.name), ['T2']);
    assert.deepEqual(u2.groups[0].members.map((m) => [m.slot_id, m.mine]), [['oams21', true], ['oams14', false]]);
    assert.equal(u2.groups[0].members[1].label, 'oams1 Spool 4');
    assert.equal(u2.groups[0].members[0].label, 'Spool 1');
    assert.equal(u2.groups[0].members[0].state, 'Ready');
  }
});

test('the bays that hold no group are listed under the unit that holds them', async () => {
  const p = await bothCores();
  for (const core of await view(p, twoUnits)) {
    assert.deepEqual(unitOf(core, 'oams1').ungrouped_bays.map((b) => b.slot_id), ['oams12', 'oams13']);
    assert.equal(unitOf(core, 'oams1').ungrouped_bays[1].state, 'Empty');
    assert.deepEqual(unitOf(core, 'oams2').ungrouped_bays, []);
  }
});

test('a full table still offers the two sentinels', async () => {
  const p = await bothCores();
  resetBoth(p);
  const bays = Array.from({ length: 9 }, (_, i) => spool('#1c1c1c', 50, i === 0 ? { s: 'l' } : {}));
  const groups = Object.fromEntries(Array.from({ length: 8 }, (_, i) => [`T${i}`, [`oams1-${i}`]]));
  const sc: Scenario = {
    units: [{ name: 'oams1', idx: 1, family: 'ams2', lane: 'fps1', bays }],
    groups,
    lanes: [{ id: 'fps1', ext: 'extruder', op: 'LOADED', bay: 'oams1-0', group: 'T0' }],
  };
  for (const core of await view(p, sc)) {
    const f = changeGroup(core, 'oams1', 8).form!.fields[0];
    assert.equal(f.options.length, 10);
    assert.deepEqual(f.options.slice(-2), [
      { value: -1, label: 'No group' },
      { value: -2, label: 'New group' },
    ]);
  }
  // "New group" still names a group, and the host is the one that refuses the
  // ninth: the mapper's job is to say what the choice means, not to guess.
  assert.deepEqual(gcode(both(p, 'change group -2 oams19')), [
    'OAMSM_CREATE_GROUP GROUP=T8',
    'OAMSM_ASSIGN_BAY GROUP=T8 OAMS=oams1 BAY=8',
  ]);
});

/* ------------------------------------- the host edits the groups at runtime */

/** One `openams_ui` projection, as the host sends it: @p bays, @p groups as
 *  this unit's whole answer, @p lane as its FPS lane. */
const projection = (unit: string, idx: number, bays: unknown, groups: unknown, lane: unknown) => ({
  v: 1, unit, idx, family: 'ams1', serial: '', online: true, ready: true, cal: 'c',
  lane, bays, groups, env: { t: 24, rh: 31 }, dryer: null, fault: null, nf: 0, act: [],
});

/** Apply one projection to both cores, which must answer the same, and hand
 *  back the two of them. */
function applyBoth(p: Pair, unit: string, obj: unknown): [Core, Core] {
  const rc = p.c.apply('openams_ui', unit, obj);
  const rt = p.ts.apply('openams_ui', unit, obj);
  assert.equal(rc, rt);
  const d = firstDiff(p.c.view(), p.ts.view());
  assert.equal(d, null, d ?? '');
  return [p.c, p.ts];
}

/** The groups of @p unit as its list shows them: name and member order. */
const shown = (core: Core, unit: string): [string, string[]][] =>
  unitOf(core, unit).groups.map((g) => [g.name, g.members.map((mem) => mem.slot_id)]);

/** The four groups of oams1, and the same list once bay 2 has moved out of T2
 *  into T1. The `back` state of that move is the first list again. */
const oams1Bays = [{ id: 0, s: 'e' }, { id: 1, s: 'r' }, { id: 2, s: 'r' }, { id: 3, s: 'l' }];
const groupsBefore = [
  { n: 'T0', b: ['oams1-0'], s: 'e' },
  { n: 'T1', b: ['oams1-1'], s: 'r' },
  { n: 'T2', b: ['oams1-2'], s: 'r' },
  { n: 'T3', b: ['oams1-3'], s: 'r' },
];
const groupsAfter = [
  { n: 'T0', b: ['oams1-0'], s: 'e' },
  { n: 'T1', b: ['oams1-1', 'oams1-2'], s: 'rr' },
  { n: 'T3', b: ['oams1-3'], s: 'r' },
];
const laneLoadedT3 = {
  id: 'fps1', ext: 'extruder', op: 'LOADED', group: 'T3', bay: 'oams1-3',
  stage: null, runout: 'idle', from: null, to: null, ms: true,
};
const oams1 = (groups: unknown) => projection('oams1', 1, oams1Bays, groups, laneLoadedT3);

test('a groups list is the host\'s whole answer, so a moved bay and a dropped group follow', async () => {
  const p = await bothCores();
  resetBoth(p);
  const manager = { ready: true, settings: {}, lanes_by_fps: { fps1: { pressure: 0.5, set_point: 0.5 } } };
  assert.equal(p.c.apply('oams_manager', null, manager), p.ts.apply('oams_manager', null, manager));

  // before: T0..T3, one bay each, and the lane is loaded out of T3
  for (const core of applyBoth(p, 'oams1', oams1(groupsBefore))) {
    assert.deepEqual(shown(core, 'oams1'), [
      ['T0', ['oams11']], ['T1', ['oams12']], ['T2', ['oams13']], ['T3', ['oams14']],
    ]);
    assert.deepEqual(changeGroup(core, 'oams1', 1).form!.fields[0].options.map((o) => o.label),
      ['T0', 'T1', 'T2', 'T3', 'No group', 'New group']);
  }

  // after: bay 2 is a member of T1 in the order the host lists it, and T2 is
  // not listed at all, so it leaves the table with its last bay gone
  for (const core of applyBoth(p, 'oams1', oams1(groupsAfter))) {
    assert.deepEqual(shown(core, 'oams1'), [
      ['T0', ['oams11']], ['T1', ['oams12', 'oams13']], ['T3', ['oams14']],
    ]);
    assert.deepEqual(changeGroup(core, 'oams1', 1).form!.fields[0].options.map((o) => o.label),
      ['T0', 'T1', 'T3', 'No group', 'New group']);
    assert.deepEqual(unitOf(core, 'oams1').ungrouped_bays, []);
    // The lane is still loaded out of T3, and the name the toolhead reads is
    // the one the table holds at the index T3 moved to.
    assert.equal(core.view().toolheads[0].tool!.label, 'T3');
  }

  // back: T2 is listed again and holds bay 2, T1 is bay 1 alone. It takes its
  // place by name, not the end of the table.
  for (const core of applyBoth(p, 'oams1', oams1(groupsBefore))) {
    assert.deepEqual(shown(core, 'oams1'), [
      ['T0', ['oams11']], ['T1', ['oams12']], ['T2', ['oams13']], ['T3', ['oams14']],
    ]);
    assert.equal(core.view().toolheads[0].tool!.label, 'T3');
  }
});

test('a group that spans two units keeps the other unit\'s bay across an update', async () => {
  const p = await bothCores();
  resetBoth(p);
  const manager = { ready: true, settings: {}, lanes_by_fps: { fps1: { pressure: 0.5, set_point: 0.5 } } };
  assert.equal(p.c.apply('oams_manager', null, manager), p.ts.apply('oams_manager', null, manager));

  const lane = {
    id: 'fps1', ext: 'extruder', op: 'LOADED', group: 'T0', bay: 'oams1-0',
    stage: null, runout: 'idle', from: null, to: null, ms: true,
  };
  const oams2 = (groups: unknown) => projection('oams2', 2, [{ id: 0, s: 'r' }], groups, lane);

  // oams1's answer names a bay of oams2, a unit this model does not carry
  // yet: that member is skipped and the group keeps what it can show.
  for (const core of applyBoth(p, 'oams1', projection('oams1', 1, oams1Bays, [
    { n: 'T0', b: ['oams1-0'], s: 'l' },
    { n: 'T2', b: ['oams2-0', 'oams1-3'], s: 'rl' },
  ], lane))) {
    assert.deepEqual(shown(core, 'oams1'), [['T0', ['oams11']], ['T2', ['oams14']]]);
  }

  // oams2 arrives and lists T0 without a `b`: it holds none of oams2's bays, so
  // oams1's bay stays. T2 is listed with both, in the order it declares them.
  for (const core of applyBoth(p, 'oams2', oams2([
    { n: 'T0' },
    { n: 'T2', b: ['oams2-0', 'oams1-3'], s: 'rl' },
  ]))) {
    assert.deepEqual(shown(core, 'oams1'), [['T0', ['oams11']], ['T2', ['oams21', 'oams14']]]);
    assert.deepEqual(shown(core, 'oams2'), [['T2', ['oams21', 'oams14']]]);
  }

  // Now oams1 moves its bay out of T2 into T1, and its update says so: T1
  // carries the bay, and T2 comes with a bare name, so it keeps oams2's bay
  // and only that one.
  for (const core of applyBoth(p, 'oams1', projection('oams1', 1, oams1Bays, [
    { n: 'T0', b: ['oams1-0'], s: 'l' },
    { n: 'T1', b: ['oams1-3'], s: 'l' },
    { n: 'T2' },
  ], lane))) {
    assert.deepEqual(shown(core, 'oams1'), [['T0', ['oams11']], ['T1', ['oams14']]]);
    assert.deepEqual(shown(core, 'oams2'), [['T2', ['oams21']]]);
  }
});
test('groups are listed by name, a run of digits read as a number', async () => {
  const p = await bothCores();
  resetBoth(p);
  const cores = applyBoth(p, 'oams1', oams1([
    { n: 'T10', b: ['oams1-0'], s: 'r' },
    { n: 'T2', b: ['oams1-1'], s: 'r' },
    { n: 'T1', b: ['oams1-2'], s: 'r' },
    { n: 'A', b: ['oams1-3'], s: 'r' },
  ]));
  for (const core of cores) {
    assert.deepEqual(shown(core, 'oams1').map(([name]) => name), ['A', 'T1', 'T2', 'T10']);
    assert.deepEqual(shown(core, 'oams1')[3], ['T10', ['oams11']]);
  }
});

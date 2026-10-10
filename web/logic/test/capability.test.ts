// Capability-limited units: the host's own lists (`bays[i].a`, `act`, `ready`)
// decide what is offered. An action the host did not list is dimmed with a
// reason and is never sent, in both cores, in every language.

import assert from 'node:assert/strict';
import { test } from 'node:test';
import {
  allActions, bothCores, fixtureSteps, firstDiff, langCodes, resetBoth, setLanguageBoth,
} from './compare.ts';
import type { Pair } from './compare.ts';
import type { Core, View } from '../types.ts';

const NOT_SUPPORTED = 'Not supported on this unit yet';
const NOT_NOW = 'Not available right now';

async function loaded(name: string, upTo = Infinity): Promise<Pair> {
  const p = await bothCores();
  resetBoth(p);
  setLanguageBoth(p, 'en');
  for (const s of fixtureSteps(name).slice(0, upTo)) {
    p.c.apply(s.kind as never, s.name, s.obj);
    p.ts.apply(s.kind as never, s.name, s.obj);
  }
  return p;
}

const bothAnswer = (p: Pair, line: string): unknown => {
  const c = p.c.action(line);
  const ts = p.ts.action(line);
  assert.equal(firstDiff(c, ts), null, `action(${JSON.stringify(line)})`);
  return c;
};

const refused = (p: Pair, line: string, reason: string): void => {
  assert.deepEqual(bothAnswer(p, line), { kind: 'error', reason }, line);
};

const sent = (p: Pair, line: string, script: string): void => {
  assert.deepEqual(bothAnswer(p, line), { kind: 'gcode', script }, line);
};

const units = (v: View) => (v.toolheads[0]?.units ?? v.unassigned_units);
const action = (core: Core, id: string, bay?: number) => {
  const u = units(core.view())[0];
  return (bay === undefined ? u.actions : u.bays[bay].actions).find((a) => a.id === id);
};

test('a unit that only reports offers no motion, on any bay', async () => {
  for (const name of ['cap_monitor_only', 'cap_monitor_only_empty']) {
    const p = await loaded(name);
    for (const core of [p.c, p.ts]) {
      const u = units(core.view())[0];
      assert.equal(u.bays.length, 4, name);
      for (const [i, bay] of u.bays.entries()) {
        for (const id of ['load', 'unload', 'calibrate']) {
          const a = bay.actions.find((x) => x.id === id);
          assert.ok(a, `${name} bay ${i} has ${id}`);
          assert.equal(a.enabled, false, `${name} bay ${i} ${id}`);
          assert.equal(a.reason, NOT_SUPPORTED, `${name} bay ${i} ${id}`);
        }
        assert.equal(bay.actions.find((x) => x.id === 'reread'), undefined, 'no reader listed, no reread');
      }
      assert.deepEqual(u.actions, [], 'dryer null: no dryer actions');
    }
    refused(p, 'load mon11', NOT_SUPPORTED);
    refused(p, 'calibrate mon11', NOT_SUPPORTED);
    // a reader the host does not list: by state first, so only where a spool is
    refused(p, 'reread mon11', name === 'cap_monitor_only' ? NOT_SUPPORTED : 'No spool is inserted');
  }
});

test('a host that lists no bay at all gives a unit with no tiles and no actions', async () => {
  const p = await loaded('cap_monitor_only_nobays');
  for (const core of [p.c, p.ts]) {
    const u = units(core.view())[0];
    assert.equal(u.bays.length, 0);
    assert.deepEqual(u.actions, []);
  }
});

test('an AMS 1 whose bays list load but not calibrate dims calibrate and nothing else', async () => {
  const p = await loaded('cap_ams1');
  for (const core of [p.c, p.ts]) {
    assert.equal(action(core, 'load', 0)?.enabled, true);
    const cal = action(core, 'calibrate', 0);
    assert.equal(cal?.enabled, false);
    assert.equal(cal?.reason, NOT_NOW);
    assert.equal(action(core, 'calibrate', 2)?.reason, 'No spool is inserted', 'an empty bay says why by state');
    assert.equal(action(core, 'reread', 0), undefined, 'act lists no rfid_scan');
  }
  sent(p, 'load am11', 'OPENAMS_LOAD GROUP=T0 SLOT=0');
  refused(p, 'calibrate am11', NOT_NOW);
});

test('drying follows act: no dryer_start, the reason is set; no dryer_stop, stop is dimmed', async () => {
  let p = await loaded('cap_ams2_no_dryer_actions');
  for (const core of [p.c, p.ts]) {
    const start = action(core, 'dry_start');
    assert.equal(start?.enabled, false);
    assert.equal(start?.reason, NOT_SUPPORTED);
  }
  refused(p, 'dry dry1 start 50C 4h', NOT_SUPPORTED);
  sent(p, 'reread dry11', 'OAMS_RFID_SCAN OAMS=1');
  sent(p, 'fault clear_fault dry1', 'OAMS_CLEAR_FAULT OAMS=1');

  p = await loaded('cap_ams2_dryer_no_adapter');
  refused(p, 'dry dry1 start 50C 4h', "The dryer's power adapter is not connected.");

  p = await loaded('cap_ams_ht_one_bay');
  assert.equal(action(p.c, 'dry_start')?.enabled, true);
  sent(p, 'dry ht1 start 55C 4h', 'OAMS_DRYER_START OAMS=1 TARGET=55 DURATION=14400');
  refused(p, 'dry ht1 stop', NOT_SUPPORTED);
  assert.equal(action(p.ts, 'dry_stop')?.reason, NOT_SUPPORTED);
});

test('the dryer reason is worded in the language in use, also after a switch', async () => {
  const p = await loaded('cap_ams2_no_dryer_actions');
  for (const code of langCodes()) {
    setLanguageBoth(p, code);
    const c = action(p.c, 'dry_start');
    const ts = action(p.ts, 'dry_start');
    assert.equal(c?.enabled, false, code);
    assert.ok(c?.reason, code);
    assert.equal(c?.reason, ts?.reason, code);
    assert.deepEqual(bothAnswer(p, 'dry dry1 start 50C 4h'), { kind: 'error', reason: c?.reason }, code);
  }
  setLanguageBoth(p, 'en');
});

test('offline, then online and not ready, then ready: each answers for its own state', async () => {
  let p = await loaded('cap_offline_then_online', 2);
  refused(p, 'load off11', 'Unit is not connected');
  p = await loaded('cap_offline_then_online', 4);
  refused(p, 'load off11', NOT_SUPPORTED);
  refused(p, 'calibrate off11', NOT_SUPPORTED);
  p = await loaded('cap_offline_then_online');
  sent(p, 'load off11', 'OPENAMS_LOAD GROUP=T0 SLOT=0');
  sent(p, 'calibrate off11', 'OAMS_CALIBRATE_PTFE_LENGTH OAMS=1 SPOOL=0');
});

test('a unit with full lists offers what it always did', async () => {
  const p = await loaded('cap_full');
  sent(p, 'load full11', 'OPENAMS_LOAD GROUP=T0 SLOT=0');
  sent(p, 'calibrate full11', 'OAMS_CALIBRATE_PTFE_LENGTH OAMS=1 SPOOL=0');
  sent(p, 'reread full11', 'OAMS_RFID_SCAN OAMS=1');
  sent(p, 'dry full1 start 50C 4h', 'OAMS_DRYER_START OAMS=1 TARGET=50 DURATION=14400');
  sent(p, 'fault clear_fault full1', 'OAMS_CLEAR_FAULT OAMS=1');
});

test('a host that sends no lists withholds nothing', async () => {
  const p = await bothCores();
  resetBoth(p);
  const obj = {
    unit: 'old1', family: 'ams2', idx: 1,
    bays: [{ s: 'r' }, { s: 'e' }],
    lane: { id: 'fps1', op: 'UNLOADED' },
    groups: [{ n: 'T0', b: ['old1-0'], s: 'r' }],
  };
  p.c.apply('openams_ui', 'old1', obj);
  p.ts.apply('openams_ui', 'old1', obj);
  sent(p, 'calibrate old11', 'OAMS_CALIBRATE_PTFE_LENGTH OAMS=1 SPOOL=0');
});

test('an action that is dimmed is never sent, in any fixture and any language', async () => {
  const names = ['cap_monitor_only', 'cap_ams1', 'cap_ams2_no_dryer_actions', 'cap_ams_ht_one_bay', 'cap_offline_then_online'];
  for (const name of names) {
    const p = await loaded(name);
    for (const code of langCodes()) {
      setLanguageBoth(p, code);
      for (const core of [p.c, p.ts]) {
        for (const a of allActions(core.view())) {
          if (a.enabled) continue;
          if (a.form) continue;   // a form line has placeholders; the views' own check covers it
          assert.notEqual(core.action(a.line).kind, 'gcode', `${name}/${code}: ${a.line}`);
        }
      }
    }
    setLanguageBoth(p, 'en');
  }
});

test('the recorded capability-limited units are dimmed the way their lists say', async () => {
  // not ready, no motion listed: load, unload and calibrate are all withheld
  for (const name of ['rec_monitor_only_idle', 'rec_load_refused']) {
    const p = await loaded(name);
    for (const core of [p.c, p.ts]) {
      for (const bay of units(core.view())[0].bays) {
        for (const id of ['load', 'unload', 'calibrate']) {
          const a = bay.actions.find((x) => x.id === id);
          if (a) assert.equal(a.enabled, false, `${name} ${id}`);
        }
      }
    }
  }
  // no dryer on the unit: no dryer actions, whatever else it lists
  for (const name of ['rec_dryer_start_refused', 'rec_ams1_no_dryer']) {
    const p = await loaded(name);
    for (const core of [p.c, p.ts]) assert.deepEqual(units(core.view())[0].actions, [], name);
  }
  // the one-bay HT: a dryer the host lists, bays it does not let move
  const ht = await loaded('rec_ams_ht_one_bay');
  assert.equal(action(ht.c, 'dry_start')?.enabled, true);
  assert.equal(action(ht.c, 'load', 0)?.enabled, false);
});

test('a unit name of any length is one unit, in both cores', async () => {
  const names = ['ams_left', 'ams_left_unit_01', 'u'.repeat(31), 'u'.repeat(32), 'v'.repeat(40)];
  for (const name of names) {
    const p = await bothCores();
    resetBoth(p);
    const obj = {
      unit: name, family: 'ams2', idx: 1, ready: true,
      bays: [{ id: 0, s: 'r', a: 'lca' }, { id: 1, s: 'e', a: '' }],
      lane: { id: `lane_${'x'.repeat(40)}` },
      groups: [{ n: 'T0', b: [`${name}-0`], s: 'r' }],
      act: [],
    };
    for (let i = 0; i < 3; i++) {
      p.c.apply('openams_ui', name, obj);
      p.ts.apply('openams_ui', name, obj);
    }
    for (const core of [p.c, p.ts]) {
      const v = core.view();
      const all = [...v.toolheads.flatMap((t) => t.units), ...v.unassigned_units];
      assert.equal(all.length, 1, `${name.length} chars: one unit`);
      assert.equal(v.toolheads.length, 1, `${name.length} chars: one lane`);
    }
    assert.equal(firstDiff(p.c.view(), p.ts.view()), null, `${name.length} chars`);
    const id = units(p.c.view())[0].bays[0].slot_id;
    assert.deepEqual(bothAnswer(p, `load ${id}`), { kind: 'gcode', script: 'OPENAMS_LOAD GROUP=T0 SLOT=0' }, id);
  }
});

// An alert slot's "clear" flag (PRINCIPLES.md 2 and 4): true only when nothing
// in the scope alerts at all. A slot with no badge is empty either because
// nothing alerts (the check, "No alerts") or because what alerts is shown
// beside it (the message row, a tile's tag), and the two must not be confused.
// Both cores must say the same, so every case compares them.

import assert from 'node:assert/strict';
import { test } from 'node:test';
import { bothCores, firstDiff, resetBoth, setLanguageBoth } from './compare.ts';
import type { Pair } from './compare.ts';

interface Fault { code: string; text: string; sev: string; unit: string | null; bay: number | null; act: string[] }

function objects(fault: Fault | null, bayStates: [string, string] = ['r', 'r']): unknown[] {
  return [
    ['openams_ui', 'oams1', {
      unit: 'oams1', family: 'ams1', idx: 1,
      bays: [
        { id: 0, s: bayStates[0], cal: 'c', a: '', sp: { id: 1, src: 't' } },
        { id: 1, s: bayStates[1], cal: 'c', a: '', sp: { id: 2, src: 't' } },
      ],
      lane: { id: 'fps1', ext: 'extruder', op: 'UNLOADED', bay: null, group: null, stage: null, runout: 'idle', ms: false },
      groups: [{ n: 'T0', b: ['oams1-0'], s: 'r' }],
      fault, nf: fault ? 1 : 0,
    }],
  ];
}

async function setup(objs: unknown[]): Promise<Pair> {
  const p = await bothCores();
  assert.equal(setLanguageBoth(p, 'en'), true);
  resetBoth(p);
  for (const [kind, name, obj] of objs as [string, string | null, unknown][]) {
    for (const core of [p.c, p.ts]) core.apply(kind, name, obj);
  }
  return p;
}

const bothAgree = (p: Pair, where: string): void => {
  const d = firstDiff(p.c.view(), p.ts.view());
  assert.equal(d, null, `${where}: ${d}`);
};

test('nothing alerting anywhere: both scopes are clear', async () => {
  const p = await setup(objects(null));
  for (const core of [p.c, p.ts]) {
    const th = core.view().toolheads[0];
    assert.equal(th.alert, null);
    assert.equal(th.alert_clear, true);
    assert.equal(th.units[0].alert_clear, true);
  }
  bothAgree(p, 'clear');
});

test('a fault the message row says, no badge: not clear, and not a badge either', async () => {
  const p = await setup(objects({ code: 'x', text: 'Hotend cold', sev: 'pause', unit: null, bay: null, act: [] }));
  for (const core of [p.c, p.ts]) {
    const th = core.view().toolheads[0];
    assert.equal(th.message.tone, 'error');
    assert.equal(th.alert_clear, false);
  }
  bothAgree(p, 'a fault in the row');
});

test('a fault on a bay: the lane and its unit are not clear', async () => {
  const p = await setup(objects({ code: 'stall', text: 'Feeder stalled', sev: 'stop', unit: 'oams1', bay: 1, act: ['clear_fault'] }));
  for (const core of [p.c, p.ts]) {
    const th = core.view().toolheads[0];
    assert.equal(th.alert_clear, false);
    assert.equal(th.units[0].alert_clear, false);
  }
  bothAgree(p, 'a fault on a bay');
});

test('a faulted bay with no lane fault: its unit and its lane are not clear', async () => {
  const p = await setup(objects(null, ['x', 'r']));
  for (const core of [p.c, p.ts]) {
    const th = core.view().toolheads[0];
    assert.equal(th.units[0].alert_clear, false);
    assert.equal(th.alert_clear, false);   // the row says "Spool 1 reported an error"
  }
  bothAgree(p, 'a faulted bay');
});

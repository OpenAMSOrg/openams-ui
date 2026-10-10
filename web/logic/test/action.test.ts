// The answer to a load or an unload, read against what the host reported:
// Moonraker answers a G-code script only once it has run, so a failure that
// comes after the host showed the load running (the display's own timeout, an
// error from mid-run) is not a refusal to start. The C's
// tests/test_screen_edit.c test_load_answered_after_it_ran() asks the same.

import assert from 'node:assert/strict';
import { test } from 'node:test';
import { ActionKind, actionResult, actionSent, Busy, newModel, refreshThis } from '../model.ts';
import type { Model, Toolhead, Unit } from '../model.ts';

/** A model with one unit on one toolhead, the display's own. */
function oneLane(): { m: Model; th: Toolhead } {
  const m = newModel();
  const th = { busy: Busy.NONE, busyUnit: -1, busySlot: -1, loaded: false, steps: [] } as unknown as Toolhead;
  m.toolheads.push(th);
  m.units.push({ toolhead: 0, slots: [] } as unknown as Unit);
  m.thisUnit = 0;
  return { m, th };
}

function run(m: Model, th: Toolhead, busy: number): void {
  th.busy = busy as Toolhead['busy'];
  th.busyUnit = busy === Busy.NONE ? -1 : 0;
  th.busySlot = busy === Busy.NONE ? -1 : 1;
  refreshThis(m);
}

test('a late timeout for a load that ran is not a refusal', () => {
  const { m, th } = oneLane();
  actionSent(m, ActionKind.LOAD);
  run(m, th, Busy.LOAD);
  run(m, th, Busy.NONE);
  actionResult(m, ActionKind.LOAD, false, 'timeout');
  assert.equal(m.actionRefusal, '');
});

test('an error after the load started is not a refusal to start', () => {
  const { m, th } = oneLane();
  actionSent(m, ActionKind.LOAD);
  run(m, th, Busy.LOAD);
  actionResult(m, ActionKind.LOAD, false, 'Hub sensor did not trigger');
  assert.equal(m.actionRefusal, '');
});

test('a refusal before the load started is still said', () => {
  const { m } = oneLane();
  actionSent(m, ActionKind.LOAD);
  actionResult(m, ActionKind.LOAD, false, 'Heat the extruder first');
  assert.equal(m.actionRefusal, 'Heat the extruder first');
  assert.equal(m.actionRefusalKind, ActionKind.LOAD);
});

// Moonraker sends diffs: a lane-only openams_ui diff must not erase the bays
// a full object delivered (the merge lives in the core, core.h).
import assert from 'node:assert/strict';
import { test } from 'node:test';
import { loadCore } from '../index.js';
import { eventArgs, loadFixture } from './helpers.mjs';

test('a lane-only diff keeps the bays', async () => {
  const core = await loadCore();
  const fx = loadFixture('loaded');
  const manager = fx.events.find((e) => e.kind === 'c2_status');
  const full = fx.events.find((e) => e.kind === 'openams_ui');

  core.reset(false);
  assert.equal(core.apply(...eventArgs(manager)), true);
  assert.equal(core.apply(...eventArgs(full)), true);
  const before = core.view();
  const unitBefore = before.toolheads[0].units[0];
  assert.equal(unitBefore.bays.length, 4);
  assert.equal(before.toolheads[0].activity.kind, 'idle');

  // The same full object again changes nothing.
  assert.equal(core.apply(...eventArgs(full)), false);

  // A diff with only `lane`: a load of T0 starts.
  const lane = { ...full.payload.lane, op: 'LOADING', group: 'T0', bay: 'oams1-0', ms: false };
  assert.equal(core.apply('openams_ui', 'oams1', { lane }), true);

  const after = core.view();
  const unitAfter = after.toolheads[0].units[0];
  assert.equal(unitAfter.bays.length, 4, 'the diff must not erase the bays');
  assert.deepStrictEqual(unitAfter.bays.map((b) => b.slot_id), unitBefore.bays.map((b) => b.slot_id));
  assert.equal(unitAfter.info.serial, unitBefore.info.serial, 'idx/serial survive the diff');
  assert.equal(after.toolheads[0].activity.kind, 'loading', 'the lane part of the diff took effect');

  // And a diff carrying only a bay list replaces just that key: bay 3 becomes
  // a copy of bay 0 (a bay holding a spool, like bay 1), the lane is untouched.
  assert.equal(unitAfter.bays[3].state, 'empty');
  const bays = full.payload.bays.map((b) => (b.id === 3 ? { ...full.payload.bays[0], id: 3 } : b));
  assert.equal(core.apply('openams_ui', 'oams1', { bays }), true);
  const last = core.view().toolheads[0].units[0];
  assert.equal(last.bays[3].state, last.bays[1].state);
  assert.notEqual(last.bays[3].state, 'empty');
  assert.equal(core.view().toolheads[0].activity.kind, 'loading', 'the lane survives a bays-only diff');
});

test('an unknown kind or a non-object is not applied', async () => {
  const core = await loadCore();
  core.reset(false);
  assert.equal(core.apply('nonsense', null, { a: 1 }), false);
  assert.equal(core.apply('openams_ui', 'oams1', [1, 2]), false);
  assert.throws(() => core.apply('openams_ui', 'oams1', undefined), TypeError);
});

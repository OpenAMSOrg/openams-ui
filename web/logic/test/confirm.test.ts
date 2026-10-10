// "Confirm spool": a bay whose linked Spoolman spool waits for the user's
// confirmation wears the "Confirm" tag, and the tag carries the answer
// (UNIFIED_UI.md 7, SpoolConfirm): how much is left, starting from the spool's
// full weight, sent as `spool confirm <slot> <grams>`. Both cores are asked the
// same thing.

import assert from 'node:assert/strict';
import { test } from 'node:test';
import { bothCores, firstDiff, resetBoth } from './compare.ts';
import type { Pair } from './compare.ts';
import type { Core, ViewAction, ViewTile } from '../types.ts';

const unit = {
  unit: 'oams1',
  family: 'ams1',
  idx: 1,
  // bay 0: linked and loaded, bay 1: linked and ready, bay 2: empty, bay 3: ready, nothing pending
  bays: [
    { id: 4, cal: 'c', s: 'l', sp: { id: 12, src: 'm' } },
    { id: 5, cal: 'c', s: 'r', sp: { id: 13, src: 'r' } },
    { id: 6, cal: 'c', s: 'e' },
    { id: 7, cal: 'c', s: 'r', sp: { id: 14, src: 'm' } },
  ],
  lane: { id: 'fps1', op: 'IDLE', loaded: 'oams1-0' },
  groups: [{ n: 'T0', b: ['oams1-0'], s: 'l' }, { n: 'T1', b: ['oams1-1'], s: 'r' }],
};

const spool = (initial: number | null) => ({
  material: 'PLA', vendor: 'Polymaker', color_hex: 'FFFFFF', remaining_g: 300, remaining_pct: 40,
  ...(initial === null ? {} : { initial_g: initial }),
});

async function setup(): Promise<Pair> {
  const p = await bothCores();
  resetBoth(p);
  for (const core of [p.c, p.ts]) {
    core.apply('openams_ui', 'oams1', unit);
    core.apply('component_status', null, {
      spoolman_online: true,
      bays: {
        'oams1-0': { state: 'pending_confirmation', pending_confirmation: true, spool: spool(null) },
        'oams1-1': { state: 'pending_confirmation', pending_confirmation: true, spool: spool(750) },
        'oams1-2': { state: 'pending_confirmation', pending_confirmation: true, spool: null },
        'oams1-3': { state: 'linked', pending_confirmation: false, spool: spool(1000) },
      },
    });
  }
  return p;
}

const tiles = (core: Core): ViewTile[] => {
  const v = core.view();
  return (v.toolheads[0]?.units ?? v.unassigned_units)[0].bays;
};
const confirmOf = (core: Core, bay: number): ViewAction | undefined =>
  tiles(core)[bay].actions.find((a) => a.id === 'confirm_spool');

test('the Confirm tag carries the confirmation, with the full weight to start from, in both cores', async () => {
  const p = await setup();
  for (const core of [p.c, p.ts]) {
    const t = tiles(core)[1];
    assert.equal(t.tag?.text, 'Confirm');
    assert.equal(t.pending_confirmation, true);
    const a = confirmOf(core, 1)!;
    assert.ok(a, 'a pending bay offers the confirmation');
    assert.equal(a.label, 'Confirm spool');
    assert.equal(a.line, 'spool confirm oams12 {grams}');
    assert.equal(a.target, 'tag');
    assert.equal(a.style, 'primary');
    assert.equal(a.enabled, true);
    assert.equal(a.reason, '');
    assert.equal(a.confirm, null);
    assert.equal(a.form!.submit_label, 'Confirm');
    assert.equal(a.form!.require_change, false);
    assert.deepEqual(a.form!.fields, [{
      id: 'grams', label: 'Remaining', kind: 'number', value: 750, min: 0, max: 10000, step: 1, unit: 'g',
      required: false,
    }]);
    // The tile's own menu (tile and ring) stays as it was: the answer is the tag's.
    assert.deepEqual(t.actions.filter((x) => x.target === 'tag').map((x) => x.id), ['confirm_spool']);
  }
  const d = firstDiff(p.c.view(), p.ts.view());
  assert.equal(d, null, d ?? '');
});

test('a spool that never said its size starts from what the editor would, in both cores', async () => {
  const p = await setup();
  for (const core of [p.c, p.ts]) {
    // 300 g at 40 %: read back as 750 g, the Edit spool form's own "initial"
    const a = confirmOf(core, 0)!;
    assert.equal(a.line, 'spool confirm oams11 {grams}');
    const initial = tiles(core)[0].actions.find((x) => x.id === 'edit_spool')!.form!.fields
      .find((f) => f.id === 'initial')!.value;
    assert.equal(a.form!.fields[0].value, initial);
  }
});

test('only the tile wearing the Confirm tag offers it, in both cores', async () => {
  const p = await setup();
  for (const core of [p.c, p.ts]) {
    // an empty bay says "Empty", and there is nothing on it to confirm
    assert.equal(tiles(core)[2].tag?.text, 'Empty');
    assert.equal(confirmOf(core, 2), undefined);
    // nothing pending, nothing to confirm
    assert.equal(tiles(core)[3].tag, null);
    assert.equal(confirmOf(core, 3), undefined);
  }
});

test('the form as the panel fills it goes to the host as the confirmation, in both cores', async () => {
  const p = await setup();
  for (const core of [p.c, p.ts]) {
    const a = confirmOf(core, 1)!;
    assert.deepEqual(core.action(a.line, { grams: 600 }), {
      kind: 'rpc', method: 'server.openams_spoolman.confirm', params: { bay: 'oams1-1', remaining_g: 600 },
    });
    // the full spool, as the form opens on it
    assert.deepEqual(core.action(a.line, { grams: a.form!.fields[0].value }), {
      kind: 'rpc', method: 'server.openams_spoolman.confirm', params: { bay: 'oams1-1', remaining_g: 750 },
    });
  }
});

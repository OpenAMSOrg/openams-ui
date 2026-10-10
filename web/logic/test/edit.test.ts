// "Edit spool": the ring action, its form and the `spool edit` line, the same in
// both cores. The line is the one mapping for both renderers: the web form fills
// its placeholders, the ESP32's edit screen writes it out directly, and only
// what differs from what the form starts from is sent.

import assert from 'node:assert/strict';
import { test } from 'node:test';
import { bothCores, firstDiff, resetBoth } from './compare.ts';
import type { Pair } from './compare.ts';
import type { ActionResult, Core, ViewAction } from '../types.ts';

const unit = {
  unit: 'oams1',
  family: 'ams1',
  idx: 1,
  // bay 0: a linked spool, bay 1: a spool with no record, bay 2: empty, bay 3: a linked spool of its own kind
  bays: [{ s: 'r', sp: { id: 12, src: 'm' } }, { s: 'r' }, { s: 'e' }, { s: 'r', sp: { id: 15, src: 't' } }],
  lane: { id: 'fps1', op: 'IDLE' },
};

const status = (edit: boolean, online = true) => ({
  spoolman_online: online,
  ...(edit ? { edit: true } : {}),
  bays: {
    'oams1-0': { spool: { material: 'PETG', vendor: 'Polymaker', color_hex: '0A2989', remaining_g: 430, remaining_pct: 43 } },
    'oams1-3': { spool: { material: 'Nylon X', vendor: 'Acme Co', color_hex: '123456', remaining_g: 500, remaining_pct: 50 } },
  },
});

const VENDORS = [{ id: 1, name: 'Polymaker' }, { id: 2, name: 'bambu lab' }, { id: 3, name: 'eSUN' }];

async function setup(edit = true, online = true): Promise<Pair> {
  const p = await bothCores();
  resetBoth(p);
  for (const core of [p.c, p.ts]) {
    core.apply('openams_ui', 'oams1', unit);
    core.apply('component_status', null, status(edit, online));
    core.apply('vendor_list', null, VENDORS);
  }
  return p;
}

const tiles = (core: Core) => {
  const v = core.view();
  return (v.toolheads[0]?.units ?? v.unassigned_units)[0].bays;
};
const editOf = (core: Core, bay: number): ViewAction => tiles(core)[bay].actions.find((a) => a.id === 'edit_spool')!;

/** An action line resolved by both cores; they must agree. */
function both(p: Pair, line: string, form?: Record<string, string | number | boolean>): ActionResult {
  const c = p.c.action(line, form);
  const ts = p.ts.action(line, form);
  const d = firstDiff(c, ts, `action(${JSON.stringify(line)})`);
  assert.equal(d, null, d ?? '');
  return ts;
}

const rpc = (params: Record<string, unknown>): ActionResult =>
  ({ kind: 'rpc', method: 'server.openams_spoolman.edit', params });

test('the ring lists Edit spool after load and unload, in both cores', async () => {
  const p = await setup();
  for (const core of [p.c, p.ts]) {
    const ids = tiles(core)[0].actions.map((a) => a.id);
    assert.deepEqual(ids.slice(0, 4), ['load', 'unload', 'edit_spool', 'spool_list_refresh']);
    const a = editOf(core, 0);
    assert.equal(a.target, 'ring');
    assert.equal(a.label, 'Edit spool');
    assert.equal(a.enabled, true);
    assert.equal(a.reason, '');
  }
  const d = firstDiff(p.c.view(), p.ts.view());
  assert.equal(d, null, d ?? '');
});

test('Edit spool is dimmed with its reason, in both cores', async () => {
  const cases: [string, boolean, boolean, number, string][] = [
    ['no edit endpoint', false, true, 0, 'Spoolman is not set up'],
    ['Spoolman offline', true, false, 0, 'Spoolman is offline'],
    ['an empty bay', true, true, 2, 'No spool is inserted'],
  ];
  for (const [name, edit, online, bay, reason] of cases) {
    const p = await setup(edit, online);
    for (const core of [p.c, p.ts]) {
      const a = editOf(core, bay);
      assert.equal(a.enabled, false, name);
      assert.equal(a.reason, reason, name);
    }
  }
});

test("the form starts from the bay's own facts, in both cores", async () => {
  const p = await setup();
  for (const core of [p.c, p.ts]) {
    const f = Object.fromEntries(editOf(core, 0).form!.fields.map((x) => [x.id, x]));
    assert.deepEqual(Object.keys(f), ['material', 'color', 'vendor', 'remaining', 'initial', 'pa']);
    // PETG is index 1 of the ten
    assert.equal(f.material.options!.length, 10);
    assert.equal(f.material.value, 1);
    // 0A2989 is the palette's blue, so no color of its own is added
    assert.equal(f.color.value, 0x0a2989);
    assert.equal(f.color.options!.length, 18);
    // Spoolman's three, sorted, with the curated brands behind them and
    // Generic first; Polymaker's place
    assert.deepEqual(f.vendor.options!.map((o) => o.label), [
      'Generic', 'bambu lab', 'eSUN', 'Polymaker',
      'Prusament', 'Sunlu', 'Elegoo', 'Overture', 'Hatchbox', 'Creality', 'Anycubic', 'Jayo',
      'Eryone', 'Inland', 'Fillamentum', 'Fiberlogy', 'ColorFabb', 'Extrudr', 'Protopasta',
      'Siraya Tech', 'Atomic Filament', 'MatterHackers', 'Spectrum', 'Azurefilm', 'Kingroon',
      'Geeetech', 'Voxelab', 'Amolen', '3DXTech', 'Das Filament',
    ]);
    assert.equal(f.vendor.value, 3);
    // what is left, and the size read back from the grams and the percent
    assert.equal(f.remaining.value, 430);
    assert.equal(f.initial.value, 1000);
    assert.equal(f.pa.kind, 'number');
    assert.equal(f.pa.max, 2);
  }
});

test("a material, vendor or color that is not on the list is one more choice, in both cores", async () => {
  const p = await setup();
  for (const core of [p.c, p.ts]) {
    const f = Object.fromEntries(editOf(core, 3).form!.fields.map((x) => [x.id, x]));
    assert.equal(f.material.options!.length, 11);
    assert.equal(f.material.options![10].label, 'Nylon X');
    assert.equal(f.material.value, 10);
    assert.equal(f.color.options!.length, 19);
    assert.deepEqual(f.color.options![18], { value: 0x123456, label: '#123456' });
    assert.equal(f.color.value, 0x123456);
    // the 30 curated brands, with the bay's own vendor as one more choice
    assert.equal(f.vendor.options!.length, 31);
    assert.equal(f.vendor.options![30].label, 'Acme Co');
    assert.equal(f.vendor.value, 30);
    assert.equal(f.initial.value, 1000);
  }
});

test('an unknown color is a choice of its own, and an unknown vendor is Generic, in both cores', async () => {
  const p = await setup();
  for (const core of [p.c, p.ts]) {
    const f = Object.fromEntries(editOf(core, 1).form!.fields.map((x) => [x.id, x]));
    assert.equal(f.color.value, -1);
    assert.equal(f.color.options![0].value, -1);
    assert.equal(f.color.options![0].label, 'Not set');
    assert.equal(f.vendor.options![f.vendor.value].label, 'Generic');
    assert.equal(f.material.value, 0);
  }
});

test('only what changed is sent, in both cores', async () => {
  const p = await setup();
  assert.deepEqual(both(p, 'spool edit oams11 color=255'), rpc({ unit: 'oams1', bay: 0, color_hex: '0000FF' }));
  assert.deepEqual(both(p, 'spool edit oams11 material=0'), rpc({ unit: 'oams1', bay: 0, material: 'PLA' }));
  assert.deepEqual(both(p, 'spool edit oams11 vendor=0'), rpc({ unit: 'oams1', bay: 0, vendor: 'Generic' }));
  assert.deepEqual(both(p, 'spool edit oams11 vendor=1'), rpc({ unit: 'oams1', bay: 0, vendor: 'bambu lab' }));
  assert.deepEqual(
    both(p, 'spool edit oams11 remaining=300 initial=1200 pa=0.045'),
    rpc({ unit: 'oams1', bay: 0, remaining_weight: 300, initial_weight: 1200, flow_k: 0.045 }),
  );
  // what the form starts from is not a change, and a line that changes nothing says so
  const same = 'spool edit oams11 material=1 color=666249 vendor=3 remaining=430 initial=1000 pa=0.02';
  assert.deepEqual(both(p, same.replace('666249', String(0x0a2989))), { kind: 'error', reason: 'No changes to save' });
  assert.deepEqual(both(p, 'spool edit oams11'), { kind: 'error', reason: 'No changes to save' });
  // the bay's own material, which is not on the list, is index ten
  assert.deepEqual(both(p, 'spool edit oams14 material=10 pa=0.1'), rpc({ unit: 'oams1', bay: 3, flow_k: 0.1 }));
  // the bay's own vendor, which is not on the list, is the one past the end
  assert.deepEqual(both(p, 'spool edit oams14 vendor=30 remaining=100'), rpc({ unit: 'oams1', bay: 3, remaining_weight: 100 }));
});

test("a spool's own size and pressure advance prefill the form, in both cores", async () => {
  const p = await setup();
  // a status is whole: `edit` and `spoolman_online` are re-read every time
  const apply = (bays: Record<string, unknown>): void => {
    for (const core of [p.c, p.ts]) core.apply('component_status', null, { spoolman_online: true, edit: true, bays });
  };
  const fields = (core: Core, bay: number) => Object.fromEntries(editOf(core, bay).form!.fields.map((x) => [x.id, x]));

  // bay 0 knows both; bay 1 sends a value of the wrong type and bay 3 a null,
  // which are unknown exactly as a field the host leaves out is
  apply({
    'oams1-0': { spool: { initial_g: 2000, flow_k: 0.045 } },
    'oams1-1': { spool: { initial_g: '2000', flow_k: '0.045' } },
    'oams1-3': { spool: { initial_g: null, flow_k: null } },
  });
  for (const core of [p.c, p.ts]) {
    const f = fields(core, 0);
    // the spool's own size, not the one read back from 430 g of 43 %
    assert.equal(f.remaining.value, 430);
    assert.equal(f.initial.value, 2000);
    assert.equal(f.pa.value, 0.045);
    for (const bay of [1, 3]) {
      assert.equal(fields(core, bay).initial.value, 1000, `bay ${bay}`);
      assert.equal(fields(core, bay).pa.value, 0.02, `bay ${bay}`);
    }
  }
  // the form as it opens carries them, so submitting it sends nothing; the
  // read-back weight and the 0.020 default are changes now
  const opening = Object.fromEntries(editOf(p.ts, 0).form!.fields.map((f) => [f.id, f.value]));
  assert.deepEqual(both(p, editOf(p.ts, 0).line, opening), { kind: 'error', reason: 'No changes to save' });
  assert.deepEqual(both(p, 'spool edit oams11 pa=0.02'), rpc({ unit: 'oams1', bay: 0, flow_k: 0.02 }));
  assert.deepEqual(both(p, 'spool edit oams11 initial=1000'), rpc({ unit: 'oams1', bay: 0, initial_weight: 1000 }));

  // a PA only rounding gets right: 1.001 * 1000 is 1000.9999999999999
  apply({ 'oams1-1': { spool: { flow_k: 1.001 } } });
  for (const core of [p.c, p.ts]) assert.equal(fields(core, 1).pa.value, 1.001);

  // the spool goes: what it alone told us goes with it
  apply({ 'oams1-0': { spool: null } });
  for (const core of [p.c, p.ts]) {
    const f = fields(core, 0);
    assert.equal(f.remaining.value, 1000);
    assert.equal(f.initial.value, 1000);
    assert.equal(f.pa.value, 0.02);
  }

  // outside the contract's range (a weight that does not fit the model, a PA
  // past 0-2): unknown in both cores, never wrapped into a value the form shows
  apply({
    'oams1-0': { spool: { initial_g: 70000, flow_k: 5 } },
    'oams1-1': { spool: { initial_g: 0, flow_k: -0.1 } },
  });
  for (const core of [p.c, p.ts]) {
    for (const bay of [0, 1]) {
      assert.equal(fields(core, bay).initial.value, 1000, `bay ${bay}`);
      assert.equal(fields(core, bay).pa.value, 0.02, `bay ${bay}`);
    }
  }
});

test('a spool with no host record is added, always with a material, in both cores', async () => {
  const p = await setup();
  assert.deepEqual(both(p, 'spool edit oams12'), rpc({ unit: 'oams1', bay: 1, material: 'PLA' }));
  assert.deepEqual(
    // oams12 has no vendor list from the host, so these are the curated ones
    both(p, 'spool edit oams12 material=1 color=16711680 vendor=2 remaining=250'),
    rpc({ unit: 'oams1', bay: 1, material: 'PETG', color_hex: 'FF0000', vendor: 'eSUN', remaining_weight: 250 }),
  );
});

test('an edit the model refuses says why, in both cores', async () => {
  const cases: [string, string][] = [
    ['spool edit oams11 color=255 remaining=-1', 'A weight or the PA value is out of range'],
    ['spool edit oams11 initial=0', 'A weight or the PA value is out of range'],
    ['spool edit oams11 pa=2.5', 'A weight or the PA value is out of range'],
    ['spool edit oams11 pa=-0.1', 'A weight or the PA value is out of range'],
    ['spool edit oams11 color=16777216', 'A weight or the PA value is out of range'],
    ['spool edit oams11 remaining=900 initial=800', 'The remaining weight cannot exceed the initial weight'],
    ['spool edit oams11 remaining=1100', 'The remaining weight cannot exceed the initial weight'],
    ['spool edit oams11 material=99', 'That choice is no longer on the list. Edit the spool again.'],
    // 33 vendors on oams11 (Generic, Spoolman's three, the curated ones) and
    // 30 on oams12 (no vendor list from the host), so these are past both
    ['spool edit oams11 vendor=34', 'That choice is no longer on the list. Edit the spool again.'],
    ['spool edit oams12 vendor=31', 'That choice is no longer on the list. Edit the spool again.'],
    ['spool edit oams12 material=10', 'That choice is no longer on the list. Edit the spool again.'],
    ['spool edit oams11 vendor=3', 'No changes to save'],
    ['spool edit oams11 material=10', 'No changes to save'],
    ['spool edit oams13 color=1', 'No spool is inserted'],
  ];
  const p = await setup();
  for (const [line, reason] of cases) assert.deepEqual(both(p, line), { kind: 'error', reason }, line);

  const off = await setup(false);
  assert.deepEqual(both(off, 'spool edit oams11 color=1'), { kind: 'error', reason: 'Spoolman is not set up' });
  const offline = await setup(true, false);
  assert.deepEqual(both(offline, 'spool edit oams11 color=1'), { kind: 'error', reason: 'Spoolman is offline' });
});

test('a line that is not a spool edit is not one, in both cores', async () => {
  const p = await setup();
  for (const line of [
    'spool edit oams99 color=1', 'spool edit oams11 bogus=1', 'spool edit oams11 color=x', 'spool edit oams11 color=',
    'spool edit oams11 pa=1e-2', 'spool edit oams11 color=1.5', 'spool edit oams11 =1',
  ]) {
    assert.deepEqual(both(p, line), { kind: 'local' }, line);
  }
});

test('the form, submitted as it opens, sends nothing; changed, it sends the change, in both cores', async () => {
  const p = await setup();
  const fields = editOf(p.ts, 0).form!.fields;
  const opening = Object.fromEntries(fields.map((f) => [f.id, f.value]));
  const line = editOf(p.ts, 0).line;
  assert.deepEqual(both(p, line, opening), { kind: 'error', reason: 'No changes to save' });
  assert.deepEqual(
    both(p, line, { ...opening, color: 0xf55a74, remaining: 400, pa: 0.04 }),
    rpc({ unit: 'oams1', bay: 0, color_hex: 'F55A74', remaining_weight: 400, flow_k: 0.04 }),
  );
});

test('the vendor list is applied whole, sorted, without repeats or names that do not fit', async () => {
  const p = await setup();
  for (const core of [p.c, p.ts]) {
    core.apply('vendor_list', null, [
      { id: 1, name: 'zeta' }, { id: 2, name: 'Alpha' }, { id: 3, name: 'alpha' }, { id: 4, name: 'x'.repeat(32) },
      { id: 5 }, { id: 6, name: 'y'.repeat(31) }, { id: 7, name: '' }, 'nope',
    ]);
    const names = editOf(core, 0).form!.fields[2].options!.map((o) => o.label);
    // the host's three, sorted, then the curated brands it does not already
    // name, with "Generic" moved to the front
    assert.deepEqual(names, [
      'Generic', 'Alpha', 'y'.repeat(31), 'zeta', 'Bambu Lab', 'Polymaker', 'Prusament', 'eSUN',
      'Sunlu', 'Elegoo', 'Overture', 'Hatchbox', 'Creality', 'Anycubic', 'Jayo', 'Eryone', 'Inland',
      'Fillamentum', 'Fiberlogy', 'ColorFabb', 'Extrudr', 'Protopasta', 'Siraya Tech',
      'Atomic Filament', 'MatterHackers', 'Spectrum', 'Azurefilm', 'Kingroon', 'Geeetech',
      'Voxelab', 'Amolen', '3DXTech', 'Das Filament',
    ]);
  }
  const d = firstDiff(p.c.view(), p.ts.view());
  assert.equal(d, null, d ?? '');
});

test('a host without the edit endpoint sends no `edit`, so the action is dimmed, in both cores', async () => {
  const p = await setup(true);
  for (const core of [p.c, p.ts]) {
    assert.equal(editOf(core, 0).enabled, true);
    core.apply('component_status', null, { ...status(false), edit: undefined });
  }
  // the merged copy keeps `edit: true` from before: a host sends it whole each time, so a status without it is older
  const d = firstDiff(p.c.view(), p.ts.view());
  assert.equal(d, null, d ?? '');
});

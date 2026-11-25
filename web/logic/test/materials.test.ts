// The spool editor's materials (C4 §9.3): the ones Spoolman holds beside the
// common ten, "Other…" with a typed type, and the filament temperatures a host
// that takes them is asked for, the same in both cores. A PET-CF17 nobody has
// used yet goes from the form to the host's edit call.

import assert from 'node:assert/strict';
import { test } from 'node:test';
import { bothCores, firstDiff, resetBoth } from './compare.ts';
import type { Pair } from './compare.ts';
import type { ActionResult, Core, ViewAction, ViewField } from '../types.ts';

const unit = {
  unit: 'oams1',
  family: 'ams1',
  idx: 1,
  // bay 0: a linked spool of a material Spoolman holds; bay 1: a spool with no
  // record; bay 2: empty
  bays: [{ s: 'r', sp: { id: 12, src: 'm' } }, { s: 'r' }, { s: 'e' }],
  lane: { id: 'fps1', op: 'IDLE' },
};

/** The component's status: `edit_temps` and `materials` only when the host is
 *  new enough to say them (an older host sends neither). */
const status = (newHost: boolean) => ({
  spoolman_online: true,
  edit: true,
  ...(newHost ? { edit_temps: true, materials: true } : {}),
  bays: {
    'oams1-0': { spool: { material: 'pctg', vendor: 'Polymaker', color_hex: '0A2989', remaining_g: 430, remaining_pct: 43 } },
  },
});

/** The host's list (C4 §9.3): a common material and a repeat the panel leaves
 *  out, a name too long to keep whole, and two with no temperature. */
const HOST_MATERIALS = {
  materials: [
    { name: 'PCTG', extruder_temp: null },
    { name: 'pla', extruder_temp: 210 },
    { name: 'PET-CF17', extruder_temp: 270 },
    { name: 'pctg', extruder_temp: null },
    { name: 'ABCDEFGHIJKLMNOPQ', extruder_temp: null },
    { name: 'Alpha', extruder_temp: null },
  ],
};

async function setup(newHost = true, materials: unknown = HOST_MATERIALS): Promise<Pair> {
  const p = await bothCores();
  resetBoth(p);
  for (const core of [p.c, p.ts]) {
    core.apply('openams_ui', 'oams1', unit);
    core.apply('component_status', null, status(newHost));
    core.apply('material_list', null, materials);
  }
  return p;
}

const tiles = (core: Core) => {
  const v = core.view();
  return (v.toolheads[0]?.units ?? v.unassigned_units)[0].bays;
};
const actionOf = (core: Core, bay: number, id: string): ViewAction =>
  tiles(core)[bay].actions.find((a) => a.id === id)!;
const fieldsOf = (a: ViewAction): Record<string, ViewField> =>
  Object.fromEntries(a.form!.fields.map((f) => [f.id, f]));

/** An action line resolved by both cores; they must agree. */
function both(p: Pair, line: string, form?: Record<string, string | number | boolean>): ActionResult {
  const c = p.c.action(line, form);
  const ts = p.ts.action(line, form);
  const d = firstDiff(c, ts, `action(${JSON.stringify(line)})`);
  assert.equal(d, null, d ?? '');
  return ts;
}

/** The form a renderer would send: every field at what it opens with (a text
 *  field empty, as a renderer opens it), then the changes. */
function formOf(a: ViewAction, changes: Record<string, string | number> = {}): Record<string, string | number> {
  const out: Record<string, string | number> = {};
  for (const f of a.form!.fields) out[f.id] = f.kind === 'text' ? '' : f.value;
  return { ...out, ...changes };
}

const edit = (params: Record<string, unknown>): ActionResult =>
  ({ kind: 'rpc', method: 'server.openams_spoolman.edit', params });

test('the views agree with the host list, its temperatures and the new fields', async () => {
  const p = await setup();
  const d = firstDiff(p.c.view(), p.ts.view());
  assert.equal(d, null, d ?? '');
});

test("Spoolman's materials follow the common ten, sorted, no_temp where the host has none", async () => {
  const p = await setup();
  for (const core of [p.c, p.ts]) {
    const f = fieldsOf(actionOf(core, 0, 'edit_spool'));
    const labels = f.material.options!.map((o) => o.label);
    assert.deepEqual(labels, [
      'PLA', 'PETG', 'ABS', 'ASA', 'TPU', 'PA', 'PC', 'PLA-CF', 'PETG-CF', 'PVA',
      'Alpha', 'PCTG', 'PET-CF17', 'Other…',
    ]);
    // the bay's own "pctg" is Spoolman's PCTG: no extra choice, and it is chosen
    assert.equal(f.material.value, 11);
    assert.deepEqual(f.material.options!.filter((o) => o.no_temp).map((o) => o.label), ['Alpha', 'PCTG']);
    assert.equal(f.material.options!.at(-1)!.value, -2);
  }
});

test('"Other…" types its own material into a text field that shows only then', async () => {
  const p = await setup();
  for (const core of [p.c, p.ts]) {
    const a = actionOf(core, 0, 'edit_spool');
    assert.deepEqual(a.form!.fields.map((f) => f.id),
      ['material', 'custom', 'nozzle', 'bed', 'color', 'vendor', 'remaining', 'initial', 'pa']);
    const f = fieldsOf(a);
    assert.equal(f.custom.kind, 'text');
    assert.equal(f.custom.max_len, 16);
    assert.equal(f.custom.required, true);
    assert.deepEqual(f.custom.show_when, { field: 'material', is: 'other' });
    assert.match(f.custom.note!, /first 8/);
    for (const id of ['nozzle', 'bed']) {
      assert.equal(f[id].optional, true);
      assert.equal(f[id].value, 0);
      assert.deepEqual(f[id].show_when, { field: 'material', is: 'no_temp' });
    }
    assert.equal(f.nozzle.min, 150);
    assert.equal(f.nozzle.max, 400);
    assert.equal(f.bed.max, 150);
    assert.equal(a.line, 'spool edit oams11 material={material} custom={custom} nozzle={nozzle} bed={bed} '
      + 'color={color} vendor={vendor} remaining={remaining} initial={initial} pa={pa}');
  }
});

test('PET-CF17, typed under "Other…", is the host edit\'s material', async () => {
  const p = await setup();
  const a = actionOf(p.ts, 1, 'edit_spool');
  // the renderer's own clean-up (capitals) is not needed: the core upper-cases too
  assert.deepEqual(both(p, a.line, formOf(a, { material: -2, custom: 'pet-cf17' })),
    edit({ unit: 'oams1', bay: 1, material: 'PET-CF17' }));
  // with its temperatures, when the person gave them
  assert.deepEqual(both(p, a.line, formOf(a, { material: -2, custom: 'PET-CF17', nozzle: 265, bed: 85 })),
    edit({ unit: 'oams1', bay: 1, material: 'PET-CF17', extruder_temp: 265, bed_temp: 85 }));
  // and a material from Spoolman's list by its place
  assert.deepEqual(both(p, a.line, formOf(a, { material: 12 })),
    edit({ unit: 'oams1', bay: 1, material: 'PET-CF17' }));
});

test('a typed material is refused when it is empty, too long or not one', async () => {
  const p = await setup();
  const a = actionOf(p.ts, 0, 'edit_spool');
  const refused = { kind: 'error', reason: 'Type the new material: up to 16 letters, digits or - + . _ /' };
  for (const custom of ['', 'PET*CF', 'ABCDEFGHIJKLMNOPQ', 'PET CF']) {
    assert.deepEqual(both(p, a.line, formOf(a, { material: -2, custom })), refused, custom);
  }
});

test('the temperatures go only to a host that takes them, and are checked', async () => {
  const p = await setup();
  const a = actionOf(p.ts, 0, 'edit_spool');
  assert.deepEqual(both(p, a.line, formOf(a, { nozzle: 260 })), edit({ unit: 'oams1', bay: 0, extruder_temp: 260 }));
  assert.deepEqual(both(p, a.line, formOf(a, { nozzle: 120 })),
    { kind: 'error', reason: 'The nozzle temperature is 150 to 400 °C, the bed up to 150 °C' });
  // nothing given is no change
  assert.deepEqual(both(p, a.line, formOf(a)), { kind: 'error', reason: 'No changes to save' });

  // an older host: no temperature fields, and none is ever sent
  const old = await setup(false, ['PCTG', 'PET-CF17']);
  for (const core of [old.c, old.ts]) {
    const o = actionOf(core, 0, 'edit_spool');
    assert.deepEqual(o.form!.fields.map((f) => f.id), ['material', 'custom', 'color', 'vendor', 'remaining', 'initial', 'pa']);
    assert.equal(o.line, 'spool edit oams11 material={material} custom={custom} color={color} vendor={vendor} '
      + 'remaining={remaining} initial={initial} pa={pa}');
    // Spoolman's names through the proxy carry no temperatures: nothing asks for one
    assert.equal(fieldsOf(o).material.options!.some((x) => x.no_temp), false);
  }
  assert.deepEqual(both(old, 'spool edit oams11 material=-2 custom=XYZ nozzle=255', undefined),
    edit({ unit: 'oams1', bay: 0, material: 'XYZ' }));
});

test('`material list` reads the host\'s list when it has one, else Spoolman\'s', async () => {
  const p = await setup();
  assert.deepEqual(both(p, 'material list'),
    { kind: 'rpc', method: 'server.openams_spoolman.materials', params: {} });
  const old = await setup(false, []);
  assert.deepEqual(both(old, 'material list'),
    { kind: 'rpc', method: 'server.spoolman.proxy', params: { request_method: 'GET', path: '/v1/material' } });
});

test('the unidentified bay\'s Save and load offers the same materials after its own choice', async () => {
  const p = await setup();
  for (const core of [p.c, p.ts]) {
    const f = fieldsOf(actionOf(core, 1, 'load'));
    assert.equal(f.material.options![0].value, -1);
    assert.equal(f.material.options!.length, 1 + 13 + 1);
  }
  const d = firstDiff(p.c.view(), p.ts.view());
  assert.equal(d, null, d ?? '');
});

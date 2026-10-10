// "Save and load": Load on a bay nobody has identified carries the spool form,
// and the line it sends is the edit with ` then load` (PRINCIPLES.md 13). Both
// cores are asked the same thing; the follow-up line itself is the one field
// the wasm bridge does not carry yet, so it is asserted on the TypeScript side.

import assert from 'node:assert/strict';
import { test } from 'node:test';
import { bothCores, firstDiff, resetBoth } from './compare.ts';
import type { Pair } from './compare.ts';
import type { ActionResult, Core, ViewAction } from '../types.ts';

const unit = {
  unit: 'oams1',
  family: 'ams1',
  idx: 1,
  // bay 0: a linked spool, bay 1: ready and unidentified, bay 2: empty, bay 3: a spool with no record that a tag read
  bays: [
    { id: 4, cal: 'c', s: 'r', sp: { id: 12, src: 'm' } },
    { id: 5, cal: 'c', s: 'r' },
    { id: 6, cal: 'c', s: 'e' },
    { id: 7, cal: 'c', s: 'r' },
  ],
  lane: { id: 'fps1', op: 'IDLE' },
  // tool T1 holds the unidentified bay, so the load that follows the save is allowed
  groups: [{ n: 'T1', b: ['oams1-1'], s: 'r' }],
};

async function setup(edit = true, online = true): Promise<Pair> {
  const p = await bothCores();
  resetBoth(p);
  for (const core of [p.c, p.ts]) {
    core.apply('openams_ui', 'oams1', unit);
    core.apply('component_status', null, {
      spoolman_online: online,
      ...(edit ? { edit: true } : {}),
      bays: { 'oams1-0': { spool: { material: 'PETG', vendor: 'Polymaker', color_hex: '0A2989', remaining_g: 430, remaining_pct: 43 } } },
    });
    core.apply('vendor_list', null, [{ id: 1, name: 'Polymaker' }, { id: 2, name: 'bambu lab' }]);
  }
  return p;
}

const tiles = (core: Core) => {
  const v = core.view();
  return (v.toolheads[0]?.units ?? v.unassigned_units)[0].bays;
};
const loadOf = (core: Core, bay: number): ViewAction => tiles(core)[bay].actions.find((a) => a.id === 'load')!;
const FORM_LINE = 'spool edit oams12 material={material} color={color} vendor={vendor} remaining={remaining} '
  + 'initial={initial} pa={pa} then load';

test('an unknown bay is what this is about', async () => {
  const p = await setup();
  for (const core of [p.c, p.ts]) {
    assert.equal(tiles(core)[1].state, 'unknown');
    assert.equal(tiles(core)[0].state, 'ready');
  }
});

test("an unknown bay's Load carries the spool form, in both cores", async () => {
  const p = await setup();
  for (const core of [p.c, p.ts]) {
    const ids = tiles(core)[1].actions.map((a) => a.id);
    assert.deepEqual(ids.slice(0, 3), ['load', 'unload', 'edit_spool']);
    const a = loadOf(core, 1);
    assert.equal(a.label, 'Load');
    assert.equal(a.line, FORM_LINE);
    assert.equal(a.enabled, true);
    assert.equal(a.reason, '');
    assert.equal(a.form!.submit_label, 'Save and load');
    assert.deepEqual(a.form!.fields.map((f) => f.id), ['material', 'color', 'vendor', 'remaining', 'initial', 'pa']);
    const m = a.form!.fields[0];
    assert.equal(m.required, true);
    assert.equal(m.value, -1);
    assert.deepEqual(m.options![0], { value: -1, label: 'Choose a material' });
    assert.equal(m.options!.length, 11);
    assert.deepEqual(a.form!.fields.slice(1).map((f) => f.required), [false, false, false, false, false]);
  }
  const d = firstDiff(p.c.view(), p.ts.view());
  assert.equal(d, null, d ?? '');
});

test('the other fields start from the Edit spool defaults, in both cores', async () => {
  const p = await setup();
  for (const core of [p.c, p.ts]) {
    const edit = tiles(core)[1].actions.find((a) => a.id === 'edit_spool')!.form!.fields;
    const load = loadOf(core, 1).form!.fields;
    for (const id of ['color', 'vendor', 'remaining', 'initial', 'pa']) {
      assert.deepEqual(load.find((f) => f.id === id), edit.find((f) => f.id === id), id);
    }
    assert.equal(edit[0].required, false);
    assert.equal(edit[0].value, 0);
  }
});

test("a known bay's Load is unchanged, in both cores", async () => {
  const p = await setup();
  for (const core of [p.c, p.ts]) {
    const a = loadOf(core, 0);
    assert.equal(a.line, 'load oams11');
    assert.equal(a.form, null);
    // nothing a known bay offers is required, and Edit spool never is
    for (const t of tiles(core)) {
      for (const act of t.actions) for (const f of act.form?.fields ?? []) {
        if (t.state === 'unknown' && act.id === 'load' && f.id === 'material') continue;
        assert.equal(f.required, false, `${act.id}.${f.id}`);
      }
    }
  }
});

test('Load on an unknown bay is dimmed with the Edit spool reason, in both cores', async () => {
  const cases: [string, boolean, boolean, string][] = [
    ['no edit endpoint', false, true, 'Spoolman is not set up'],
    ['Spoolman offline', true, false, 'Spoolman is offline'],
  ];
  for (const [name, edit, online, reason] of cases) {
    const p = await setup(edit, online);
    for (const core of [p.c, p.ts]) {
      const a = loadOf(core, 1);
      assert.equal(a.enabled, false, name);
      assert.equal(a.reason, reason, name);
      assert.equal(a.reason, tiles(core)[1].actions.find((x) => x.id === 'edit_spool')!.reason, name);
    }
  }
});

test('an unknown bay in no tool is dimmed in the plain Load\'s words, in both cores', async () => {
  const p = await setup();
  for (const core of [p.c, p.ts]) {
    core.apply('openams_ui', 'oams1', { ...unit, groups: [] });
    const a = loadOf(core, 1);
    assert.equal(a.enabled, false);
    assert.equal(a.reason, 'Assign this spool to a tool first');
    assert.equal(a.form!.submit_label, 'Save and load');
  }
});

test('the then-load line maps to the edit plus the load, and is refused without a material', async () => {
  const p = await setup();
  const line = 'spool edit oams12 material=2 color=255 vendor=0 remaining=250 initial=1000 pa=0.020 then load';
  const expected = { unit: 'oams1', bay: 1, material: 'ABS', color_hex: '0000FF', remaining_weight: 250 };
  const c = p.c.action(line) as ActionResult;
  const ts = p.ts.action(line) as ActionResult;
  assert.deepEqual(ts, { kind: 'rpc', method: 'server.openams_spoolman.edit', params: expected, then: 'load oams12' });
  assert.deepEqual({ ...ts, then: undefined }, { ...c, then: undefined });
  // the same call as the plain edit's, nothing of the follow-up in it
  const plain = p.ts.action(line.replace(' then load', '')) as ActionResult;
  assert.deepEqual(plain, { kind: 'rpc', method: 'server.openams_spoolman.edit', params: expected });

  const none = 'spool edit oams12 material=-1 color=255 vendor=0 remaining=250 initial=1000 pa=0.020 then load';
  for (const core of [p.c, p.ts]) {
    assert.deepEqual(core.action(none), { kind: 'error', reason: 'Choose a material' });
    // a bare edit has no such choice: -1 is a stale list
    assert.equal((core.action(none.replace(' then load', '')) as { kind: string }).kind, 'error');
    assert.equal((core.action('spool edit oams12 material=2 then') as { kind: string }).kind, 'local');
    assert.equal((core.action('spool edit oams12 material=2 then unload') as { kind: string }).kind, 'local');
  }
});

test('the form as the panel fills it: a material sends, none is refused, in both cores', async () => {
  const p = await setup();
  const a = loadOf(p.ts, 1);
  const opening = Object.fromEntries(a.form!.fields.map((f) => [f.id, f.value]));
  for (const core of [p.c, p.ts]) {
    assert.deepEqual(core.action(a.line, opening), { kind: 'error', reason: 'Choose a material' });
  }
  const chosen = { ...opening, material: 4 };
  const ts = p.ts.action(a.line, chosen) as ActionResult;
  assert.equal(ts.kind, 'rpc');
  assert.equal((ts as { then?: string }).then, 'load oams12');
  assert.equal((ts as { params: { material: string } }).params.material, 'TPU');
  const c = p.c.action(a.line, chosen) as ActionResult;
  const d = firstDiff({ ...c, then: undefined }, { ...ts, then: undefined });
  assert.equal(d, null, d ?? '');
});

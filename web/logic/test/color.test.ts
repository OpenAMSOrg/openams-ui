// A spool's color is known, unknown, or known to be black, the same in both
// cores: an unknown color has no color and no gradient in the view (the tile
// draws the neutral surface), and "000000" is black filament, drawn black.

import assert from 'node:assert/strict';
import { test } from 'node:test';
import { bothCores, firstDiff, resetBoth } from './compare.ts';
import type { Core, ViewTile } from '../types.ts';

const unit = {
  unit: 'oams1',
  family: 'ams1',
  idx: 1,
  bays: [{ s: 'r' }, { s: 'r' }, { s: 'r' }, { s: 'r' }],
  lane: { id: 'fps1', op: 'IDLE' },
};

const spool = (color_hex: string | null | undefined) => ({
  material: 'PLA',
  vendor: 'Bambu Lab',
  remaining_g: 800,
  remaining_pct: 80,
  ...(color_hex === undefined ? {} : { color_hex }),
});

const status = {
  bays: {
    'oams1-0': { spool: spool('000000') },
    'oams1-1': { spool: spool(undefined) },
    'oams1-2': { spool: spool(null) },
    'oams1-3': { spool: spool('not a color') },
  },
};

const tiles = (core: Core): ViewTile[] => {
  const v = core.view();
  return (v.toolheads[0]?.units ?? v.unassigned_units)[0].bays;
};

test('an unknown color has no color in the view; known black is black, in both cores', async () => {
  const p = await bothCores();
  resetBoth(p);
  for (const core of [p.c, p.ts]) {
    core.apply('openams_ui', 'oams1', unit);
    core.apply('component_status', null, status);
  }

  for (const core of [p.c, p.ts]) {
    const [black, none, nul, bad] = tiles(core);
    assert.equal(black.color, '#000000');
    assert.equal(black.gradient_top, '#000000');
    for (const t of [none, nul, bad]) {
      assert.equal(t.color, null);
      assert.equal(t.gradient_top, null);
      assert.equal(t.gradient_bottom, null);
      assert.equal(t.ink, 'light');
    }
  }
  const d = firstDiff(p.c.view(), p.ts.view());
  assert.equal(d, null, d ?? '');
});

test('a spool that goes away leaves an unknown color behind, in both cores', async () => {
  const p = await bothCores();
  resetBoth(p);
  for (const core of [p.c, p.ts]) {
    core.apply('openams_ui', 'oams1', unit);
    core.apply('component_status', null, { bays: { 'oams1-0': { spool: spool('D32F2F') } } });
    assert.equal(tiles(core)[0].color, '#d32f2f');
    core.apply('component_status', null, { bays: { 'oams1-0': { spool: null } } });
    assert.equal(tiles(core)[0].color, null);
  }
  const d = firstDiff(p.c.view(), p.ts.view());
  assert.equal(d, null, d ?? '');
});

test('a spool option carries its filament color, and no other select does, in both cores', async () => {
  const p = await bothCores();
  resetBoth(p);
  const spools = [
    { id: 1, remaining_weight: 740, filament: { id: 1, material: 'PLA', color_hex: 'C12E1F', vendor: { id: 1, name: 'Polymaker' } } },
    { id: 2, remaining_weight: 500, filament: { id: 2, material: 'ASA', vendor: { id: 2, name: 'Prusament' } } },
  ];
  for (const core of [p.c, p.ts]) {
    core.apply('openams_ui', 'oams1', unit);
    core.apply('component_status', null, { spoolman_online: true, edit: true, bays: status.bays });
    core.apply('spool_list', null, spools);
    const bay = tiles(core)[0];
    const assign = bay.actions.find((a) => a.id === 'assign_spool')!;
    const opts = assign.form!.fields[0].options!;
    assert.equal(opts.length, 2);
    assert.equal(opts[0].color, '#c12e1f');
    assert.equal(opts[1].color, null);
    assert.ok('color' in opts[1]);
    for (const a of bay.actions) {
      if (a.id === 'assign_spool') continue;
      for (const f of a.form?.fields ?? []) for (const o of f.options ?? []) assert.equal('color' in o, false, `${a.id}/${f.id}`);
    }
  }
  const d = firstDiff(p.c.view(), p.ts.view());
  assert.equal(d, null, d ?? '');
});

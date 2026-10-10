// The klipper-mmu families OpenAMS now sends: a unit is titled by the product
// its family names, in both cores. The family only names the unit - the
// variant, and with it the tile kind and the dryer limits, stays the one it
// falls back to - so every other answer is the variant's and stays as it was.

import assert from 'node:assert/strict';
import { test } from 'node:test';
import { bothCores, firstDiff, resetBoth } from './compare.ts';
import type { Core, ViewUnit } from '../types.ts';

/** The exact title each family is shown under, and the families that are not
 *  products at all, which keep their variant's name. */
const TITLES: [string, string][] = [
  ['boxturtle', 'BoxTurtle'],
  ['nightowl', 'NightOwl'],
  ['emu', 'EMU'],
  ['kms', 'KMS'],
  ['qidi_box', 'QIDI Box'],
  ['angry_beaver', 'Angry Beaver'],
  ['3ms', '3MS'],
  ['quattrobox_1_0', 'QuattroBox 1.0'],
  ['quattrobox_1_1', 'QuattroBox 1.1'],
  ['quattrobox_2', 'QuattroBox 2'],
  ['custom', 'Custom MMU'],
  ['ercf_1_1', 'ERCF 1.1'],
  ['ercf_2_0', 'ERCF 2.0'],
  ['ercf_3_0', 'ERCF 3.0'],
  ['tradrack', 'Tradrack'],
  ['3d_chameleon', '3D Chameleon'],
  ['htlf', 'HTLF'],
  ['mmx6', 'MMX6'],
  ['mmx', 'MMX'],
  ['low_rider', 'Low Rider'],
  ['pico_mmu', 'Pico MMU'],
  ['btt_vivid', 'BTT ViViD'],
  ['claymore', 'Claymore'],
  ['ams1', 'AMS'],
  ['ams2', 'AMS 2 Pro'],
  ['ams_ht', 'AMS HT'],
  ['inline_follower', 'Lane MMU'],
  ['a_family_from_the_future', 'AMS'],
];

const object = (family: string) => ({
  unit: 'oams1',
  family,
  idx: 1,
  bays: [{ s: 'e' }, { s: 'e' }, { s: 'e' }, { s: 'e' }],
  lane: { id: 'fps1', op: 'UNLOADED' },
});

/** The one unit card of a core, on whichever screen the view put it. */
const card = (core: Core): ViewUnit => {
  const v = core.view();
  return (v.toolheads[0]?.units ?? v.unassigned_units)[0];
};

for (const [family, title] of TITLES) {
  test(`family ${family} is titled "${title}" in both cores`, async () => {
    const p = await bothCores();
    resetBoth(p);
    const obj = object(family);

    p.c.apply('openams_ui', 'oams1', obj);
    p.ts.apply('openams_ui', 'oams1', obj);

    assert.equal(card(p.c).title, title);
    assert.equal(card(p.ts).title, title);
    assert.equal(card(p.c).info?.family, title);

    // the name is the family's alone: four bays, as the host sent them
    assert.equal(card(p.c).bays.length, 4);
    const d = firstDiff(p.c.view(), p.ts.view());
    assert.equal(d, null, d ?? '');
  });
}

test('a family that names no product leaves no product title behind', async () => {
  const p = await bothCores();
  resetBoth(p);
  for (const [family, title] of [['boxturtle', 'BoxTurtle'], ['ams2', 'AMS 2 Pro']] as
    [string, string][]) {
    const obj = object(family);
    p.c.apply('openams_ui', 'oams1', obj);
    p.ts.apply('openams_ui', 'oams1', obj);
    assert.equal(card(p.c).title, title);
    assert.equal(card(p.ts).title, title);
  }
  const d = firstDiff(p.c.view(), p.ts.view());
  assert.equal(d, null, d ?? '');
});
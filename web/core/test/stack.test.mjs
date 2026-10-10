// The JSON path fits the module's C stack (-sSTACK_SIZE in
// platform/core-wasm/CMakeLists.txt, next to the fixed 1 MB heap): the
// largest golden (four_units: four units on two toolheads; before it, boxturtle_loading: an eight-lane MMU, two toolheads, every
// tile with its action lists) is rendered through core_view(), repeatedly, and
// the heap must not be leaking between renders.
import assert from 'node:assert/strict';
import { test } from 'node:test';
import { loadCore } from '../index.js';
import { fixtureNames, loadFixture, loadGolden, replay } from './helpers.mjs';

// The heaviest fixture is whichever golden is longest: the render loop below
// is a stack and heap check, so it has to run on the worst case, and naming
// the worst case here keeps it on it when a fixture grows.
const largest = () => {
  const sizes = fixtureNames().map((n) => [n, JSON.stringify(loadGolden(n)).length]);
  sizes.sort((a, b) => b[1] - a[1]);
  return sizes[0][0];
};

test('the largest golden is known', () => {
  assert.equal(largest(), 'four_units');
});

test('the largest golden renders through core_view() many times in a fixed heap', async () => {
  const core = await loadCore();
  const name = largest();
  core.reset(false);
  replay(core, loadFixture(name));
  const golden = loadGolden(name);
  // A heap that leaked on each render would run out of its fixed 1 MB.
  for (let i = 0; i < 500; i++) {
    const v = core.view();
    if (i % 100 === 0) assert.deepStrictEqual(v, golden);
  }
  assert.deepStrictEqual(core.view(), golden);
});

test('a large spool list fits (or fails cleanly) in the fixed heap', async () => {
  const core = await loadCore();
  core.reset(false);
  replay(core, loadFixture('two_lanes'));
  const spools = Array.from({ length: 60 }, (_, i) => ({
    id: i + 1,
    remaining_weight: 800 - i,
    filament: {
      id: i + 1, name: 'PLA ' + i, material: 'PLA', color_hex: 'ff0000',
      vendor: { id: 1, name: 'Vendor' }, weight: 1000, diameter: 1.75,
    },
  }));
  let applied;
  try {
    applied = core.apply('spool_list', null, spools);
  } catch (err) {
    applied = err;
  }
  assert.ok(applied === true || applied instanceof Error, String(applied));
  // Whatever happened, the core is still usable afterwards.
  assert.equal(core.view().toolheads.length, 2);
});

// The wasm core and the native builders agree: every recorded fixture,
// replayed through apply() in file order, gives the U1c golden
// (tests/fixtures/recorded/<name>.view.json, which the simulator's --dump-view
// produces natively).
import assert from 'node:assert/strict';
import { test } from 'node:test';
import { loadCore } from '../index.js';
import { fixtureNames, loadFixture, loadGolden, replay } from './helpers.mjs';

const names = fixtureNames();

test('there are fixtures to replay', () => {
  assert.ok(names.length >= 6, 'expected the recorded fixtures, found ' + names.join(','));
});

for (const name of names) {
  test('replay ' + name + ' matches its golden', async () => {
    const core = await loadCore();
    // Twice: once from whatever state the previous test left (reset must
    // return to a clean model), once again after a second reset.
    for (let pass = 0; pass < 2; pass++) {
      core.reset(false);
      replay(core, loadFixture(name));
      assert.deepStrictEqual(core.view(), loadGolden(name), name + ' pass ' + pass);
    }
  });
}

// The random differential suite: sequences drawn from a seeded generator, fed
// to the C core (its wasm build) and the TypeScript logic step by step. The two
// must answer identically after every object and every step, for every action
// too; the first seed that does not is the bug.
//
//   node --test web/logic/test/diff.random.test.ts
//   SEEDS=50 STEPS=20 node --test web/logic/test/diff.random.test.ts   (a subset)

import assert from 'node:assert/strict';
import test from 'node:test';
import { compareSequence, bothCores } from './compare.ts';
import { events } from './gen/objects.ts';
import { randomSequence } from './gen/random.ts';
import { mulberry32 } from './gen/rng.ts';

const SEEDS = Number(process.env.SEEDS ?? 200);
const STEPS = Number(process.env.STEPS ?? 20);

/** Seed @p seed's sequence as core objects, one array per step. */
function stepsOf(seed: number) {
  return randomSequence(mulberry32(seed), STEPS).map((sc) => events(sc));
}

test('every random sequence drives both cores the same way', async () => {
  // One pair of cores for the whole sweep: compareSequence() resets both at the
  // start of every sequence, so a seed can only see what its own steps built.
  const p = await bothCores();
  const failed: string[] = [];
  for (let seed = 1; seed <= SEEDS && failed.length < 3; seed++) {
    const steps = stepsOf(seed);
    const d = await compareSequence(steps, p);
    if (d) {
      // The step the path names, so the report points at the objects to replay.
      const at = /^step (\d+)/.exec(d);
      const step = at ? steps[Number(at[1])] : undefined;
      failed.push(`seed ${seed}: ${d}\n    ${JSON.stringify(step)}`);
    }
  }
  for (const f of failed) console.error(`FAIL ${f}`);
  assert.deepEqual(failed, []);
});

test('a seed gives the same sequence every time', () => {
  for (const seed of [1, 2, 7, 99, 200]) {
    assert.deepEqual(stepsOf(seed), stepsOf(seed));
  }
});

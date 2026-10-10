// One core per page: loadCore() is a singleton, and every subscriber gets
// the views.
import assert from 'node:assert/strict';
import { test } from 'node:test';
import { loadCore } from '../index.js';
import { eventArgs, loadFixture } from './helpers.mjs';

test('two loadCore() calls return the same instance', async () => {
  const p1 = loadCore();
  const p2 = loadCore();
  assert.strictEqual(p1, p2, 'the same promise');
  const [a, b] = await Promise.all([p1, p2]);
  assert.strictEqual(a, b);
  assert.strictEqual(await loadCore(), a, 'and later calls too');
});

test('two subscribers both get views, once per burst of applies', async () => {
  const core = await loadCore();
  core.reset(false);
  const events = loadFixture('idle').events;

  const got1 = [];
  const got2 = [];
  const off1 = core.subscribe((v) => got1.push(v));
  const off2 = core.subscribe((v) => got2.push(v));

  // A burst of applies in one tick is one flush, not one per apply.
  for (const ev of events) core.apply(...eventArgs(ev));
  assert.equal(got1.length, 0, 'never synchronously');
  await new Promise((resolve) => setTimeout(resolve, 0));

  assert.equal(got1.length, 1);
  assert.equal(got2.length, 1);
  assert.deepStrictEqual(got1[0], core.view());
  assert.strictEqual(got1[0], got2[0], 'the same view object is fanned out');
  assert.equal(got1[0].toolheads.length, 1);

  // An unsubscribed listener stops; the other continues.
  off1();
  core.apply('openams_ui', 'oams1', { online: false });
  await new Promise((resolve) => setTimeout(resolve, 0));
  assert.equal(got1.length, 1);
  assert.equal(got2.length, 2);

  // A throwing subscriber does not starve the others.
  const realError = console.error;
  console.error = () => {};
  try {
    const off3 = core.subscribe(() => { throw new Error('boom'); });
    core.apply('openams_ui', 'oams1', { online: true });
    await new Promise((resolve) => setTimeout(resolve, 0));
    assert.equal(got2.length, 3);
    off3();
  } finally {
    console.error = realError;
  }
  off2();
});

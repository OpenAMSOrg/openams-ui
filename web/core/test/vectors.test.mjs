// tests/fixtures/actions_vectors.json (exported from tests/test_actions_map.c
// by tools/export_actions_vectors.py): action(line) gives the expected
// script, RPC, error or local.
import assert from 'node:assert/strict';
import { test } from 'node:test';
import { loadCore } from '../index.js';
import { fixtureNames, loadFixture, readJson, replay, repoRoot } from './helpers.mjs';

const doc = readJson(new URL('tests/fixtures/actions_vectors.json', repoRoot));

async function coreOnFixture() {
  const core = await loadCore();
  core.reset(false);
  replay(core, loadFixture(doc.fixture));
  return core;
}

test('the vectors file has every kind', () => {
  const kinds = new Set(doc.vectors.map((v) => v.kind));
  for (const k of ['gcode', 'rpc', 'error', 'local']) assert.ok(kinds.has(k), 'no ' + k + ' vector');
  assert.ok(doc.vectors.length >= 40);
});

test('every action vector', async () => {
  const core = await coreOnFixture();
  for (const v of doc.vectors) {
    const got = core.action(v.line);
    let want;
    if (v.kind === 'gcode') want = { kind: 'gcode', script: v.script };
    else if (v.kind === 'rpc') want = { kind: 'rpc', method: v.method, params: v.params };
    else if (v.kind === 'error') want = { kind: 'error', reason: v.reason };
    else want = { kind: 'local' };
    assert.deepStrictEqual(got, want, 'action(' + JSON.stringify(v.line) + ')');
  }
});

test('form values fill the placeholders of a view action', async () => {
  const core = await coreOnFixture();
  const view = core.view();
  const dry = view.toolheads[0].units[0].actions.find((a) => a.id === 'dry_start');
  assert.ok(dry, 'the unit offers dry_start');
  assert.match(dry.line, /\{target\}/);
  assert.deepStrictEqual(core.action(dry.line, { target: 50, hours: 4 }),
    { kind: 'gcode', script: 'OAMS_DRYER_START OAMS=1 TARGET=50 DURATION=14400' });
  // strings and numbers are both fine
  assert.deepStrictEqual(core.action(dry.line, { target: '60', hours: '1' }),
    { kind: 'gcode', script: 'OAMS_DRYER_START OAMS=1 TARGET=60 DURATION=3600' });
});

test('a missing form value is an error, not a half-filled line', async () => {
  const core = await coreOnFixture();
  assert.deepStrictEqual(core.action('dry oams1 start {target}C {hours}h', { target: 50 }),
    { kind: 'error', reason: 'missing form value: hours' });
  assert.deepStrictEqual(core.action('dry oams1 start {target}C {hours}h'),
    { kind: 'error', reason: 'missing form value: target' });
});

// The one predicate: for every action of every recorded fixture, the view's
// enabled/reason are exactly what action() answers (a refusal with the same
// reason, or a line that resolves).
test('the enabled/reason of every view action equals what action() returns', async () => {
  const core = await loadCore();
  let checked = 0, dimmed = 0;
  for (const name of fixtureNames()) {
    core.reset(false);
    replay(core, loadFixture(name));
    const view = core.view();
    const actions = [];
    for (const th of view.toolheads) {
      actions.push(...th.actions);
      for (const u of th.units) {
        actions.push(...u.actions);
        for (const t of u.bays) actions.push(...t.actions);
      }
    }
    assert.ok(actions.length > 0, name);
    for (const a of actions) {
      // A form line is resolved with each field's default value.
      const form = a.form ? Object.fromEntries(a.form.fields.map((f) => [f.id, f.value])) : null;
      const got = core.action(a.line, form);
      const where = name + ': ' + a.line;
      if (a.enabled) {
        // A form that needs a change (require_change) starts on values that are
        // no change: action() then refuses with exactly that reason, which is the
        // form's own rule (the submit button stays dim until something is
        // edited; "Choose a material" is the save-and-load dialog's own required field),
        // not a refusal of the action the view offers.
        if (a.form?.require_change && got.kind === 'error'
            && ['No changes to save', 'Choose a material'].includes(got.reason)) {
          assert.equal(a.reason, '', where);
          checked++;
          continue;
        }
        assert.notEqual(got.kind, 'error', where + ' is enabled but action() refuses it: ' + got.reason);
        assert.notEqual(got.kind, 'local', where + ' is enabled but is a local line');
        assert.equal(a.reason, '', where);
      } else {
        dimmed++;
        assert.equal(got.kind, 'error', where + ' is dimmed but action() accepts it');
        assert.equal(got.reason, a.reason, where);
      }
      checked++;
    }
  }
  assert.ok(checked > 100 && dimmed > 20, checked + ' checked, ' + dimmed + ' dimmed');
});

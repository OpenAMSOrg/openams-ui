// component_status is 16 characters. With a 16-character kind limit (15 kept)
// the stored kind never matched again, every apply took a new entry, and once
// the table filled every later update was dropped: the Spoolman component's
// online, edit and stop flags froze. The C's test_core asks the same.

import assert from 'node:assert/strict';
import { test } from 'node:test';
import { createLogic } from '../core.ts';

test('component_status keeps applying, update after update', () => {
  const logic = createLogic();
  for (let i = 0; i < 40; i++) {
    const on = i % 2 === 1;
    const changed = logic.apply('component_status', null,
      { spoolman_online: on, edit: on, stop: on, bays: {} });
    assert.equal(changed, true, `update ${i} was dropped`);
  }
});

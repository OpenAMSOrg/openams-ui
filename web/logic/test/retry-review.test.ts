// The 2026-11-09 review of the failed toolchange's Retry: it is sent once per
// fault (until the fault changes or clears, or the host answers that retry), a
// lane id that cannot go into a script gets neither Retry nor Unload, a plan
// already on the lane when the fault appeared hides nothing, the reason is
// kept whole to 255 bytes, the step shows the plan's label, and a step index
// no int8 holds is no step. Both cores must say the same, so every case
// compares them.

import assert from 'node:assert/strict';
import { test } from 'node:test';
import { bothCores, firstDiff, resetBoth, setLanguageBoth } from './compare.ts';
import type { Pair } from './compare.ts';
import type { Core, ViewAlert } from '../types.ts';

const RETRY = { load: 'OPENAMS_LOAD', unload: 'OPENAMS_UNLOAD', reset: 'OAMSM_CLEAR_ERRORS', retry: 'OAMSM_RETRY' };

interface Spec {
  lane?: string;
  fault?: boolean;
  since?: number;
  step?: string;
  reason?: string;
  /** `lanes_by_fps.<lane>.stage`, the host's own plan object. */
  laneStage?: Record<string, unknown> | null;
  /** `openams_ui.lane.stage`, the compact array the steps are drawn from. */
  uiStage?: unknown[] | null;
  op?: 'UNLOADED' | 'LOADED';
}

function manager(s: Spec): Record<string, unknown> {
  const lane: Record<string, unknown> = { pressure: 0.5, set_point: 0.5 };
  if (s.fault) {
    lane.toolchange_fault = {
      kind: 'load', group: 'T1', slot: null, temp: null, step: s.step ?? 'feed',
      reason: s.reason ?? 'Filament not detected at toolhead extruder inlet', panel: false, fed: true,
      since: s.since ?? 12.5,
    };
  }
  if (s.laneStage) lane.stage = s.laneStage;
  return { ready: true, commands: RETRY, lanes_by_fps: { [s.lane ?? 'fps1']: lane } };
}

function ui(s: Spec): Record<string, unknown> {
  const loaded = s.op === 'LOADED';
  return {
    unit: 'oams1', family: 'ams2', idx: 1,
    bays: [
      { id: 0, s: loaded ? 'l' : 'r', cal: 'c', a: loaded ? 'u' : 'l', sp: { id: 1, src: 't' } },
      { id: 1, s: 'r', cal: 'c', a: loaded ? '' : 'l', sp: { id: 2, src: 't' } },
    ],
    lane: {
      id: s.lane ?? 'fps1', ext: 'extruder', op: s.op ?? 'UNLOADED', bay: loaded ? 'oams1-0' : null,
      group: loaded ? 'T0' : null, stage: s.uiStage ?? null, runout: 'idle', ms: !loaded,
    },
    groups: [{ n: 'T0', b: ['oams1-0'], s: 'r' }, { n: 'T1', b: ['oams1-1'], s: 'r' }],
    fault: null, nf: 0,
  };
}

function apply(p: Pair, s: Spec): void {
  for (const core of [p.c, p.ts]) {
    core.apply('oams_manager', null, manager(s));
    core.apply('openams_ui', 'oams1', ui(s));
  }
}

async function setup(s: Spec): Promise<Pair> {
  const p = await bothCores();
  assert.equal(setLanguageBoth(p, 'en'), true);
  resetBoth(p);
  apply(p, s);
  return p;
}

const bothAgree = (p: Pair, where: string): void => {
  const d = firstDiff(p.c.view(), p.ts.view());
  assert.equal(d, null, `${where}: ${d}`);
};

const items = (core: Core): ViewAlert[] => core.view().toolheads[0].alert?.items ?? [];
const ids = (item: ViewAlert | undefined): string[] => (item?.actions ?? []).map((a) => a.id);
const answer = (p: Pair, ok: boolean, message: string): void => {
  for (const core of [p.c, p.ts]) assert.equal(core.apply('action_result', '', { kind: 3, ok, message }), true);
};

test('Retry is sent once for a fault, and offered again when the fault changes', async () => {
  const p = await setup({ fault: true, since: 12.5 });
  for (const core of [p.c, p.ts]) {
    assert.deepEqual(ids(items(core)[0]), ['retry', 'unload', 'clear_errors']);
    assert.deepEqual(core.action('fault retry fps1'), { kind: 'gcode', script: 'OAMSM_RETRY FPS=fps1' });
    assert.deepEqual(ids(items(core)[0]), ['unload', 'clear_errors']);
    assert.deepEqual(core.action('fault retry fps1'), { kind: 'error', reason: 'Not available right now' });
  }
  bothAgree(p, 'sent');
  // the same fault in the next status: still sent
  apply(p, { fault: true, since: 12.5 });
  for (const core of [p.c, p.ts]) assert.deepEqual(ids(items(core)[0]), ['unload', 'clear_errors']);
  // the retry failed: a new fault, Retry again
  apply(p, { fault: true, since: 30.25 });
  for (const core of [p.c, p.ts]) assert.deepEqual(ids(items(core)[0]), ['retry', 'unload', 'clear_errors']);
  bothAgree(p, 'new fault');
});

test('the host answering the retry offers Retry again, taken or refused', async () => {
  for (const ok of [true, false]) {
    const p = await setup({ fault: true });
    for (const core of [p.c, p.ts]) core.action('fault retry fps1');
    answer(p, ok, ok ? '' : 'OpenAMS could not retry the toolchange to T1: a fault stayed latched');
    for (const core of [p.c, p.ts]) {
      assert.deepEqual(ids(items(core)[0]), ['retry', 'unload', 'clear_errors']);
      assert.equal(core.action('fault retry fps1').kind, 'gcode');
    }
    bothAgree(p, `answered ${ok}`);
  }
});

test('a fault that clears and comes back is a new one', async () => {
  const p = await setup({ fault: true });
  for (const core of [p.c, p.ts]) core.action('fault retry fps1');
  apply(p, {});
  for (const core of [p.c, p.ts]) assert.deepEqual(items(core), []);
  apply(p, { fault: true });
  for (const core of [p.c, p.ts]) assert.deepEqual(ids(items(core)[0]), ['retry', 'unload', 'clear_errors']);
  bothAgree(p, 'back');
});

test('a lane id that cannot go into a script gets neither Retry nor Unload', async () => {
  const p = await setup({ lane: 'fps1 M112', fault: true, op: 'LOADED' });
  for (const core of [p.c, p.ts]) {
    assert.deepEqual(ids(items(core)[0]), ['clear_errors']);
    assert.deepEqual(core.action('fault retry fps1 M112'),
                     { kind: 'error', reason: 'Not supported on this unit yet' });
    assert.deepEqual(core.action('unload fps1 M112'), { kind: 'error', reason: 'Not supported on this unit yet' });
    assert.deepEqual(core.action('unload oams11'), { kind: 'error', reason: 'Not supported on this unit yet' });
  }
  bothAgree(p, 'bad lane id');
});

test('a plan already on the lane when the fault appeared hides nothing', async () => {
  const plan = ['heat', 'cut', 'feed'];
  const p = await setup({});            // the lane seen without a fault first
  apply(p, { fault: true, laneStage: { plan, index: 1 }, uiStage: [plan, 1, null] });
  for (const core of [p.c, p.ts]) {
    assert.deepEqual(ids(items(core)[0]), ['retry', 'unload', 'clear_errors']);
    assert.equal(core.action('fault retry fps1').kind, 'gcode');
  }
  bothAgree(p, 'stale plan');
  // the plan moving on is a retry running: the fault gives way to its steps
  const q = await setup({});
  apply(q, { fault: true, laneStage: { plan, index: 1 }, uiStage: [plan, 1, null] });
  apply(q, { fault: true, laneStage: { plan, index: 2 }, uiStage: [plan, 2, null] });
  for (const core of [q.c, q.ts]) {
    assert.deepEqual(items(core), []);
    assert.deepEqual(core.action('fault retry fps1'), { kind: 'error', reason: 'The toolhead is busy' });
  }
  bothAgree(q, 'plan moved on');
});

test('the reason is kept whole to 255 bytes, and the step shows the plan label', async () => {
  const reason = 'r'.repeat(255);
  const p = await setup({
    fault: true, step: 'purge', reason,
    laneStage: { plan: ['feed', 'purge'], index: 1, failed: 'purge', labels: { purge: 'Purge line' } },
  });
  for (const core of [p.c, p.ts]) {
    const item = items(core)[0];
    assert.equal(item.text, reason);
    assert.match(item.title, /Purge line/);
  }
  bothAgree(p, 'reason and label');
});

test('a step index no int8 holds is no current step', async () => {
  const plan = ['cut', 'feed'];
  for (const index of [300, -129, 1e300]) {
    const p = await setup({ op: 'UNLOADED', uiStage: [plan, index, null] });
    const q = await setup({ op: 'UNLOADED', uiStage: [plan, null, null] });
    assert.deepEqual(p.c.view(), q.c.view(), `C core, index ${index}`);
    assert.deepEqual(p.ts.view(), q.ts.view(), `TS core, index ${index}`);
    bothAgree(p, `index ${index}`);
  }
});

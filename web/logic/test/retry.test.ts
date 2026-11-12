// A failed toolchange or unload (`oams_manager.lanes_by_fps.<l>.toolchange_fault`,
// C2 4.2) and the Retry the host advertises (`oams_manager.commands.retry`):
// the toolhead's message row says what failed, its badge holds the item with
// Retry, Unload and Clear errors, and Retry is offered only with the host's own
// word. Both cores must say the same, so every case compares them.

import assert from 'node:assert/strict';
import { test } from 'node:test';
import { bothCores, firstDiff, langCodes, resetBoth, setLanguageBoth } from './compare.ts';
import type { Pair } from './compare.ts';
import type { Core, ViewAlert } from '../types.ts';

interface Tc { kind: 'load' | 'unload'; group: string | null; step: string | null; reason: string }

interface Spec {
  tc?: Tc | null;
  /** The advertised commands; undefined = an older host with no map at all. */
  commands?: Record<string, string>;
  op?: 'UNLOADED' | 'LOADED' | 'LOADING';
  stage?: [string[], number | null, string | null] | null;
  fault?: { code: string; text: string; sev: string; unit: string | null; bay: number | null; act: string[] } | null;
}

function objects(s: Spec): [string, string | null, unknown][] {
  const lane: Record<string, unknown> = { pressure: 0.5, set_point: 0.5 };
  if (s.tc) {
    lane.toolchange_fault = {
      kind: s.tc.kind, group: s.tc.group, slot: s.tc.kind === 'load' ? '1' : null, temp: null,
      step: s.tc.step, reason: s.tc.reason, panel: true, fed: false, since: 12.5,
    };
  }
  const manager: Record<string, unknown> = { ready: true, lanes_by_fps: { fps1: lane } };
  if (s.commands) manager.commands = s.commands;
  const loaded = s.op === 'LOADED';
  return [
    ['oams_manager', null, manager],
    ['openams_ui', 'oams1', {
      unit: 'oams1', family: 'ams2', idx: 1,
      bays: [
        { id: 0, s: loaded ? 'l' : 'r', cal: 'c', a: loaded ? 'u' : 'l', sp: { id: 1, src: 't' } },
        { id: 1, s: 'r', cal: 'c', a: loaded ? '' : 'l', sp: { id: 2, src: 't' } },
      ],
      lane: {
        id: 'fps1', ext: 'extruder', op: s.op ?? 'UNLOADED', bay: loaded ? 'oams1-0' : null,
        group: loaded ? 'T0' : null, stage: s.stage ?? null, runout: 'idle', ms: !loaded,
      },
      groups: [{ n: 'T0', b: ['oams1-0'], s: 'r' }, { n: 'T1', b: ['oams1-1'], s: 'r' }],
      fault: s.fault ?? null, nf: s.fault ? 1 : 0,
    }],
  ];
}

async function setup(s: Spec, lang = 'en'): Promise<Pair> {
  const p = await bothCores();
  assert.equal(setLanguageBoth(p, lang), true);
  resetBoth(p);
  for (const [kind, name, obj] of objects(s)) {
    for (const core of [p.c, p.ts]) core.apply(kind, name, obj);
  }
  return p;
}

const bothAgree = (p: Pair, where: string): void => {
  const d = firstDiff(p.c.view(), p.ts.view());
  assert.equal(d, null, `${where}: ${d}`);
};

const items = (core: Core): ViewAlert[] => core.view().toolheads[0].alert?.items ?? [];
const ids = (item: ViewAlert | undefined): string[] => (item?.actions ?? []).map((a) => a.id);

const LOAD: Tc = { kind: 'load', group: 'T1', step: 'feed', reason: 'Spool loading failed (NO_PROGRESS)' };
const RETRY = { load: 'OPENAMS_LOAD', unload: 'OPENAMS_UNLOAD', reset: 'OAMSM_CLEAR_ERRORS', retry: 'OAMSM_RETRY' };

test('an older host: no fault, no command, nothing offered and Retry refused', async () => {
  const p = await setup({});
  for (const core of [p.c, p.ts]) {
    assert.deepEqual(items(core), []);
    assert.deepEqual(core.action('fault retry fps1'), { kind: 'error', reason: 'Not supported on this unit yet' });
  }
  bothAgree(p, 'older host');
});

test('a failed load says what failed in the row, and its badge offers Retry, Unload and Clear errors', async () => {
  const p = await setup({ tc: LOAD, commands: RETRY });
  for (const core of [p.c, p.ts]) {
    const th = core.view().toolheads[0];
    assert.equal(th.message.tone, 'error');
    assert.equal(th.message.text, 'Loading T1 failed: Spool loading failed (NO_PROGRESS)');
    assert.equal(th.activity.kind, 'error');
    assert.equal(th.alert?.tone, 'error');
    const [item] = items(core);
    assert.equal(item.title, 'Loading T1 failed (Push new filament into extruder)');
    assert.equal(item.text, 'Spool loading failed (NO_PROGRESS)');
    assert.equal(item.severity, 'pause');
    assert.equal(item.unit, null);
    assert.deepEqual(ids(item), ['retry', 'unload', 'clear_errors']);
    const [retry, unload, clear] = item.actions;
    assert.equal(retry.label, 'Retry');
    assert.equal(retry.line, 'fault retry fps1');
    assert.equal(retry.enabled, true);
    assert.equal(retry.style, 'primary');
    // nothing was fed, so there is nothing to back out
    assert.equal(unload.line, 'unload fps1');
    assert.equal(unload.enabled, false);
    assert.equal(unload.reason, 'No filament is loaded');
    assert.equal(clear.line, 'fault clear_errors');
    assert.equal(clear.enabled, true);
    // the line resolves to the host's own word, always with the lane
    assert.deepEqual(core.action(retry.line), { kind: 'gcode', script: 'OAMSM_RETRY FPS=fps1' });
    assert.deepEqual(core.action(clear.line), { kind: 'gcode', script: 'OAMSM_CLEAR_ERRORS' });
    // the unit's badge does not list it: it is the toolhead's
    assert.equal(th.units[0].alert, null);
  }
  bothAgree(p, 'failed load');
});

test('without the advertised command there is no Retry, and the rest stays', async () => {
  const p = await setup({ tc: LOAD, commands: { reset: 'OAMSM_CLEAR_ERRORS' } });
  for (const core of [p.c, p.ts]) {
    assert.deepEqual(ids(items(core)[0]), ['unload', 'clear_errors']);
    assert.deepEqual(core.action('fault retry fps1'), { kind: 'error', reason: 'Not supported on this unit yet' });
  }
  bothAgree(p, 'no retry command');
});

test('the advertised word is sent as it is, and a word that is not one is not taken', async () => {
  let p = await setup({ tc: LOAD, commands: { retry: 'MY_RETRY' } });
  for (const core of [p.c, p.ts]) {
    assert.deepEqual(core.action('fault retry fps1'), { kind: 'gcode', script: 'MY_RETRY FPS=fps1' });
  }
  p = await setup({ tc: LOAD, commands: { retry: 'OAMSM_RETRY\nM112' } });
  for (const core of [p.c, p.ts]) {
    assert.deepEqual(ids(items(core)[0]), ['unload', 'clear_errors']);
  }
  bothAgree(p, 'not a word');
});

test('advertised but nothing failed: no item, and the line is refused', async () => {
  const p = await setup({ commands: RETRY });
  for (const core of [p.c, p.ts]) {
    assert.deepEqual(items(core), []);
    assert.deepEqual(core.action('fault retry fps1'), { kind: 'error', reason: 'Not available right now' });
  }
  bothAgree(p, 'nothing failed');
});

test('a fed load can be backed out: Unload is enabled', async () => {
  const p = await setup({ tc: { ...LOAD, step: 'purge' }, commands: RETRY, op: 'LOADED' });
  for (const core of [p.c, p.ts]) {
    const [item] = items(core);
    assert.equal(item.title, 'Loading T1 failed (Purge old filament)');
    assert.equal(item.actions[1].enabled, true);
    assert.deepEqual(core.action(item.actions[1].line), { kind: 'gcode', script: 'OPENAMS_UNLOAD FPS=fps1' });
  }
  bothAgree(p, 'fed load');
});

test('a retry in progress shows its own steps, not the old fault', async () => {
  let p = await setup({ tc: LOAD, commands: RETRY, stage: [['heat', 'feed'], 0, null] });
  for (const core of [p.c, p.ts]) {
    const th = core.view().toolheads[0];
    assert.deepEqual(items(core), []);
    assert.notEqual(th.message.tone, 'error');
    assert.deepEqual(core.action('fault retry fps1'), { kind: 'error', reason: 'The toolhead is busy' });
  }
  bothAgree(p, 'retry running');
  // it failed again: the plan marks the step, and the fault is back
  p = await setup({ tc: LOAD, commands: RETRY, stage: [['heat', 'feed'], 1, 'feed'] });
  for (const core of [p.c, p.ts]) assert.deepEqual(ids(items(core)[0]), ['retry', 'unload', 'clear_errors']);
  bothAgree(p, 'retry failed again');
});

test('a lane fault offering retry_load gives way to the one Retry', async () => {
  // the AMS fault names the bay T1 was loading from: its tile's tag holds its actions
  const fault = { code: 'mmu_no_progress', text: 'No progress; retry the load', sev: 'pause', unit: 'oams1', bay: 1,
    act: ['retry_load', 'clear_errors'] };
  const tagIds = (core: Core): string[] =>
    core.view().toolheads[0].units[0].bays[1].actions.filter((a) => a.target === 'tag').map((a) => a.id);
  let p = await setup({ tc: LOAD, commands: RETRY, fault });
  for (const core of [p.c, p.ts]) {
    const th = core.view().toolheads[0];
    assert.equal(th.message.text, 'No progress; retry the load');   // the lane fault owns the row
    assert.deepEqual(tagIds(core), ['clear_errors']);
    assert.deepEqual(ids(items(core)[0]), ['retry', 'unload', 'clear_errors']);
  }
  bothAgree(p, 'lane fault with a toolchange fault');
  // an older host: the lane fault keeps its own
  p = await setup({ tc: LOAD, fault });
  for (const core of [p.c, p.ts]) assert.deepEqual(tagIds(core), ['retry_load', 'clear_errors']);
  bothAgree(p, 'lane fault, no retry command');
});

test('an unload that failed before its plan names no tool and no step', async () => {
  const p = await setup({ tc: { kind: 'unload', group: null, step: null, reason: 'timed out unloading spool' }, commands: RETRY });
  for (const core of [p.c, p.ts]) {
    const th = core.view().toolheads[0];
    assert.equal(th.message.text, 'Unloading failed: timed out unloading spool');
    assert.equal(items(core)[0].title, 'Unloading failed');
  }
  bothAgree(p, 'unload');
});

test('the fault going away leaves nothing behind', async () => {
  const p = await setup({ tc: LOAD, commands: RETRY });
  for (const core of [p.c, p.ts]) {
    core.apply('oams_manager', null, { lanes_by_fps: { fps1: { pressure: 0.5, set_point: 0.5 } } });
    const th = core.view().toolheads[0];
    assert.equal(th.alert, null);
    assert.equal(th.message.tone, 'neutral');
  }
  bothAgree(p, 'cleared');
});

test('every language says the same in both cores', async () => {
  for (const lang of langCodes()) {
    for (const tc of [LOAD, { ...LOAD, step: null }, { kind: 'unload' as const, group: null, step: 'cut', reason: 'Stuck' }]) {
      const p = await setup({ tc, commands: RETRY }, lang);
      bothAgree(p, `${lang} ${tc.kind} ${tc.step}`);
    }
  }
  const p = await bothCores();
  setLanguageBoth(p, 'en');
});

// 2026-11-12: one clear, and Abandon for a failed panel load.
test('a host that advertises abandon: the panel fault offers Retry, Unload and Abandon, not Clear errors', async () => {
  const p = await setup({ tc: LOAD, commands: { ...RETRY, abandon: 'OAMSM_ABANDON' } });
  for (const core of [p.c, p.ts]) {
    const [item] = items(core);
    assert.deepEqual(ids(item), ['retry', 'unload', 'abandon']);
    const abandon = item.actions[2];
    assert.equal(abandon.label, 'Abandon');
    assert.equal(abandon.line, 'fault abandon fps1');
    assert.equal(abandon.enabled, true);
    assert.deepEqual(core.action(abandon.line), { kind: 'gcode', script: 'OAMSM_ABANDON FPS=fps1' });
  }
  bothAgree(p, 'abandon advertised');
});

test('a unit fault offering clear_fault is cleared with Clear errors, once', async () => {
  const p = await setup({
    commands: RETRY,
    fault: { code: 'motion_stalled', text: 'motion_stalled', sev: 'pause', unit: 'oams1', bay: null, act: ['clear_fault', 'clear_errors'] },
  });
  for (const core of [p.c, p.ts]) {
    const unitItems = core.view().toolheads[0].units[0].alert?.items ?? [];
    const acts = unitItems.flatMap((i) => i.actions.map((a) => a.id));
    assert.deepEqual(acts.filter((id) => id === 'clear_errors'), ['clear_errors']);
    assert.equal(acts.includes('clear_fault'), false);
  }
  bothAgree(p, 'clear_fault as Clear errors');
});

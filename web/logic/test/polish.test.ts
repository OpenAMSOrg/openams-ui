// UNIFIED_UI 4g, in both cores: a history entry leaves the toolhead's badge
// after a while (a clock the platform sets), a dark filament gets a light
// outline, and Stop stops the whole sequence (enabled while a plan runs; the
// host's own stop is an RPC).

import assert from 'node:assert/strict';
import { test } from 'node:test';
import { bothCores, firstDiff, resetBoth } from './compare.ts';
import type { Pair } from './compare.ts';
import { build } from './synth.ts';
import type { BaySpec, LaneSpec, Scenario } from './synth.ts';
import type { View, ViewToolhead } from '../types.ts';

const bay = (color: string, over: Partial<BaySpec> = {}): BaySpec =>
  ({ s: 'l', material: 'PLA', color, pct: 80, link: 't', ...over });

function scenario(color: string, lane: Partial<LaneSpec> = {}, job?: number[]): Scenario {
  return {
    units: [{ name: 'oams1', idx: 1, family: 'ams2', lane: 'fps1', bays: [bay(color), { s: 'r' }, { s: 'e' }, { s: 'e' }] }],
    groups: { T0: ['oams1-0'] },
    lanes: [{ id: 'fps1', ext: 'extruder', op: 'LOADED', bay: 'oams1-0', group: 'T0', ...lane }],
    job,
  };
}

async function load(sc: Scenario, clock = 0, stop = false): Promise<Pair> {
  const p = await bothCores();
  resetBoth(p);
  for (const core of [p.c, p.ts]) {
    core.setClock(clock);
    build(core, sc);
    if (stop) core.apply('component_status', null, { spoolman_online: true, edit: true, stop: true, bays: {} });
  }
  return p;
}

const toolhead = (core: { view(): View }): ViewToolhead => core.view().toolheads[0];

function agree(p: Pair, where: string): void {
  const d = firstDiff(p.c.view(), p.ts.view());
  assert.equal(d, null, `${where}: ${d}`);
}

test('a dark filament is outlined, a mid color is not, in both cores', async () => {
  const cases: Array<[string, boolean]> = [
    ['#000000', true], ['#0d0d0d', true], ['#1c1c1c', true], ['#404040', true], ['#484848', false],
    ['#808080', false], ['#d32f2f', false], ['#ffffff', false], ['#000080', true],
  ];
  for (const [color, outline] of cases) {
    const p = await load(scenario(color));
    for (const core of [p.c, p.ts]) {
      const th = toolhead(core);
      assert.equal(th.tool?.color, color);
      assert.equal(th.tool?.outline, outline, `${color} tool`);
      assert.equal(th.activity.rest?.outline, outline, `${color} rest`);
    }
    agree(p, color);
  }
});

test('a history entry leaves the badge after 15 minutes unread, and the check returns', async () => {
  // loading: the message row says the step, so the job shortfall is listed in the badge
  const sc = scenario('#d32f2f', { op: 'LOADING', stage: { plan: ['heat', 'feed'], index: 1, failed: null } }, [900]);
  const MIN = 60000;
  const p = await load(sc, 1000);
  for (const core of [p.c, p.ts]) {
    const th = toolhead(core);
    assert.ok(th.alert, 'the shortfall is in the badge');
    assert.equal(th.alert_clear, false);
  }
  agree(p, 'raised');

  for (const core of [p.c, p.ts]) core.setClock(1000 + 15 * MIN - 1);
  for (const core of [p.c, p.ts]) assert.ok(toolhead(core).alert, 'still there at 14:59');
  agree(p, '14:59');

  for (const core of [p.c, p.ts]) core.setClock(1000 + 15 * MIN);
  for (const core of [p.c, p.ts]) {
    const th = toolhead(core);
    assert.equal(th.alert, null, 'gone at 15:00');
    assert.equal(th.alert_clear, true, 'the check is back');
  }
  agree(p, '15:00');
});

test('the clock wraps like a uint32 in both cores', async () => {
  const sc = scenario('#d32f2f', { op: 'LOADING', stage: { plan: ['heat', 'feed'], index: 1, failed: null } }, [900]);
  const p = await load(sc, 0xFFFFFF00);
  for (const core of [p.c, p.ts]) core.setClock(0x100);
  for (const core of [p.c, p.ts]) assert.ok(toolhead(core).alert);
  agree(p, 'wrapped');
});

const stopOf = (th: ViewToolhead) => th.actions.find((a) => a.id === 'stop')!;

test('Stop is enabled while a plan runs (a heat step), dimmed when nothing does', async () => {
  const heating = scenario('#d32f2f', { op: 'UNLOADED', bay: undefined, stage: { plan: ['heat', 'feed'], index: 0, failed: null } });
  const p = await load(heating);
  for (const core of [p.c, p.ts]) {
    const stop = stopOf(toolhead(core));
    assert.equal(stop.enabled, true, 'enabled in the heat step');
    assert.equal(stop.confirm?.title, 'Stop now?');
    assert.equal(stop.confirm?.text, 'What is running stops after the current move.');
  }
  agree(p, 'heating');

  // the wasm core is one model per page: one scenario at a time
  const failedSc = scenario('#d32f2f', { op: 'UNLOADED', bay: undefined, stage: { plan: ['heat', 'feed'], index: 1, failed: 'feed' } });
  for (const sc of [failedSc, scenario('#d32f2f')]) {
    const q = await load(sc);
    for (const core of [q.c, q.ts]) {
      const stop = stopOf(toolhead(core));
      assert.equal(stop.enabled, false);
      assert.equal(stop.reason, 'Nothing is loading or unloading');
    }
    agree(q, 'dimmed');
  }
});

test('Stop sends the RPC when the host has it, the G-code when it does not', async () => {
  const heating = scenario('#d32f2f', { op: 'UNLOADED', bay: undefined, stage: { plan: ['heat', 'feed'], index: 0, failed: null } });
  const old = await load(heating);
  for (const core of [old.c, old.ts]) {
    assert.deepEqual(core.action('stop fps1'), { kind: 'gcode', script: 'OAMSM_LOAD_FILAMENT_CANCEL FPS=fps1' });
  }
  const host = await load(heating, 0, true);
  for (const core of [host.c, host.ts]) {
    const r = core.action('stop fps1');
    assert.equal(r.kind, 'rpc');
    if (r.kind === 'rpc') {
      assert.equal(r.method, 'server.openams.stop');
      assert.deepEqual(r.params, { lane: 'fps1' });
    }
  }
  // nothing running: refused in both
  const idle = await load(scenario('#d32f2f'), 0, true);
  for (const core of [idle.c, idle.ts]) {
    const r = core.action('stop fps1');
    assert.equal(r.kind, 'error');
  }
});

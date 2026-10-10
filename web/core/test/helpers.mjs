// Shared by the node tests: the recorded recorded fixtures and how the simulator
// replays them (platform/sim/main.c, replay_apply_via_core()).
import { readFileSync, readdirSync } from 'node:fs';

export const repoRoot = new URL('../../../', import.meta.url);
export const vamsDir = new URL('tests/fixtures/recorded/', repoRoot);

export function readJson(url) {
  return JSON.parse(readFileSync(url, 'utf8'));
}

/** Names of the recorded fixtures (each has <name>.json and <name>.view.json). */
export function fixtureNames() {
  return readdirSync(vamsDir)
    .filter((f) => f.endsWith('.json') && !f.endsWith('.view.json'))
    .map((f) => f.slice(0, -'.json'.length))
    .sort();
}

export function loadFixture(name) {
  return readJson(new URL(name + '.json', vamsDir));
}

export function loadGolden(name) {
  return readJson(new URL(name + '.view.json', vamsDir));
}

/** The core.apply() arguments of one fixture event: recorded's "c2_status" is the
 *  oams_manager object; everything else keeps its kind and unit. */
export function eventArgs(ev) {
  if (ev.kind === 'c2_status') return ['oams_manager', null, ev.payload];
  return [ev.kind, ev.unit ?? null, ev.payload];
}

/** Apply every event of @p fixture in file order (the events are already
 *  ordered by time). */
export function replay(core, fixture) {
  for (const ev of fixture.events) {
    if (!ev.payload) continue;
    core.apply(...eventArgs(ev));
  }
}

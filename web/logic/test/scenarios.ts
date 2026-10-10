// The state sheet's scenarios: synthetic Moonraker objects (synth.ts) that the
// real core turns into views.
import type { BaySpec, Scenario, UnitSpec } from './synth';

const BLACK = '#1c1c1c';
const spool = (material: string, color: string, pct: number, over: Partial<BaySpec> = {}): BaySpec =>
  ({ s: 'r', material, color, pct, link: 'm', ...over });
const empty: BaySpec = { s: 'e' };

/** Two FPS cards with two units each: every tile state and tag tone, spares
 *  numbered across a group, a unit alert, a dryer, an offline unit. */
export function tileStates(): Scenario {
  const oams1: UnitSpec = { name: 'oams1', idx: 1, family: 'ams2', lane: 'fps1', bays: [
    spool('ASA', BLACK, 100, { s: 'l', link: 't' }),       // T0, loaded (bare infinity)
    spool('ASA', '#d32f2f', 46),                           // T3
    spool('ASA', BLACK, 80),                               // T0, first spare (infinity 1)
    empty,
  ] };
  const oams2: UnitSpec = { name: 'oams2', idx: 2, family: 'ams2', lane: 'fps1', bays: [
    { s: 'r' },                                            // unidentified: unknown
    spool('ASA', BLACK, 55),                               // T0, second spare (infinity 2), in the other unit
    spool('PLA', '#f0f0ea', 42, { s: 'x' }),               // error
    spool('ASA', '#2f8cf0', 33, { s: 'p' }),               // positioning
  ] };
  const oams3: UnitSpec = { name: 'oams3', idx: 3, family: 'ams2', lane: 'fps2', dryer: { state: 'hold', target: 55, min: 180 }, bays: [
    spool('PC', '#5a5e64', 46, { s: 'l', link: 't' }),     // T6, loaded
    spool('ASA', '#e6b422', 12, { grams: 120 }),           // low
    spool('ASA', '#8e44ad', 77, { pending: true }),        // pending confirmation
    spool('PLA', '#f0f0ea', 100),
  ] };
  const oams4: UnitSpec = { name: 'oams4', idx: 4, family: 'ams2', lane: 'fps2', online: false, env: null, bays: [
    spool('ASA', '#d32f2f', 61), empty, empty, { s: 'r' },
  ] };
  return {
    units: [oams1, oams2, oams3, oams4],
    groups: { T0: ['oams1-0', 'oams1-2', 'oams2-1'], T3: ['oams1-1'], T6: ['oams3-0'], T7: ['oams3-1'], T8: ['oams3-2'], T9: ['oams3-3'], T2: ['oams4-0'] },
    lanes: [
      { id: 'fps1', ext: 'extruder', op: 'LOADED', bay: 'oams1-0', group: 'T0',
        fault: { code: 'feeder_stall', text: 'The feeder stalled in Spool 3', sev: 'stop', unit: 'oams2', bay: 2, act: ['retry_load', 'clear_fault'] } },
      { id: 'fps2', ext: 'dragon_burner', op: 'LOADED', bay: 'oams3-0', group: 'T6' },
    ],
  };
}

const stepPlan = ['cut', 'retract', 'feed', 'purge'];

/** One FPS with two units, for the per-state cards. */
function card(): Scenario {
  return {
    units: [
      { name: 'oams1', idx: 1, family: 'ams2', lane: 'fps1', bays: [
        spool('ASA', BLACK, 88, { s: 'l', link: 't' }), spool('ASA', '#d32f2f', 46), empty, spool('PLA', '#2f8cf0', 66) ] },
      { name: 'oams2', idx: 2, family: 'ams_ht', lane: 'fps1', bays: [ spool('ASA', '#3db84c', 71) ] },
    ],
    groups: { T0: ['oams1-0'], T1: ['oams1-1'], T2: ['oams2-0'] },
    lanes: [{ id: 'fps1', ext: 'extruder', op: 'LOADED', bay: 'oams1-0', group: 'T0' }],
  };
}

function withCard(fn: (s: Scenario) => void): Scenario {
  const s = card();
  fn(s);
  return s;
}

export const cards: { caption: string; key?: string; scenario: Scenario }[] = [
  { caption: 'Idle, filament loaded', scenario: card() },
  { caption: 'Idle, nothing loaded', scenario: withCard((s) => {
    s.lanes[0] = { id: 'fps1', ext: 'extruder', op: 'UNLOADED' };
    s.units[0].bays[0] = spool('ASA', BLACK, 88, { link: 't' });
  }) },
  { caption: 'Loading: the stepper and the neutral message', key: 'loading', scenario: withCard((s) => {
    s.lanes[0] = { id: 'fps1', ext: 'extruder', op: 'LOADING', bay: 'oams1-1', pressure: 0.74,
      stage: { plan: stepPlan, index: 2, failed: null } };
    s.units[0].bays[0] = spool('ASA', BLACK, 88, { link: 't' });
  }) },
  { caption: 'Runout: one message line, the runout tag', scenario: withCard((s) => {
    s.groups.T0 = ['oams1-0', 'oams2-0'];
    s.groups.T2 = [];
    s.lanes[0].runout = { from: 'oams1-0', to: 'oams2-0' };
    s.units[0].bays[0] = spool('ASA', BLACK, 0, { s: 'l', link: 't', grams: 0 });
  }) },
  { caption: 'Runout with no spare left', scenario: withCard((s) => {
    s.lanes[0].runout = { from: 'oams1-0' };
    s.units[0].bays[0] = spool('ASA', BLACK, 0, { s: 'l', link: 't', grams: 0 });
  }) },
  { caption: 'Load failed: the stepper, an error message and the FPS alert', scenario: withCard((s) => {
    s.lanes[0] = { id: 'fps1', ext: 'extruder', op: 'UNLOADED', pressure: 0.93,
      stage: { plan: stepPlan, index: 2, failed: 'feed' },
      fault: { code: 'load_failed', text: 'Load failed: the filament did not reach the extruder', sev: 'pause', act: ['resume', 'clear_errors'] } };
    s.units[0].bays[0] = spool('ASA', BLACK, 88, { link: 't' });
  }) },
  { caption: 'Loading with labels and a detail on the running step', scenario: withCard((s) => {
    s.lanes[0] = { id: 'fps1', ext: 'extruder', op: 'LOADING', bay: 'oams1-1', pressure: 0.74,
      stage: {
        plan: ['brushing', 'heat', 'home', 'clean', 'park'],
        index: 1,
        failed: null,
        detail: '250 °C',
        labels: { brushing: 'Brushing the nozzle', heat: 'Melting the tip' },
      } };
    s.units[0].bays[0] = spool('ASA', BLACK, 88, { link: 't' });
  }) },
  { caption: 'Notice (a shortfall for the job)', scenario: withCard((s) => {
    s.lanes[0] = { id: 'fps1', ext: 'extruder', op: 'LOADED', bay: 'oams1-1', group: 'T1' };
    s.units[0].bays[0] = spool('ASA', BLACK, 88, { link: 't' });
    s.units[0].bays[1] = spool('ASA', '#d32f2f', 8, { s: 'l', grams: 80 });
    s.job = [0, 120];
  }) },
  { caption: 'Drying, and the unit alert (dryer fault, no adapter)', scenario: withCard((s) => {
    s.units[0].dryer = { state: 'fault', target: 65, min: 0, adapter: false };
    s.units[1].dryer = { state: 'preheat', target: 65, min: 240 };
  }) },
  { caption: 'An AMS 2 Pro and an AMS HT (one lane), the HT drying', key: 'ht', scenario: withCard((s) => {
    s.units[1].dryer = { state: 'preheat', target: 65, min: 240 };
  }) },
  { caption: 'Tile rows: a black spool, an empty bay, an offline unit, a Positioning tag', key: 'tiles', scenario: {
    units: [
      { name: 'oams1', idx: 1, family: 'ams2', lane: 'fps1', bays: [
        spool('ASA', '#0d0d0d', 88, { s: 'l', link: 't' }), empty, spool('PLA', '#2f8cf0', 33, { s: 'p' }), { s: 'r' } ] },
      { name: 'oams2', idx: 2, family: 'ams2', lane: 'fps1', online: false, bays: [
        spool('ASA', '#0d0d0d', 61), spool('ASA', '#d32f2f', 46), empty, spool('PLA', '#f0f0ea', 90) ] },
    ],
    groups: { T0: ['oams1-0'], T1: ['oams2-0'], T2: ['oams2-1'], T3: ['oams2-3'] },
    lanes: [{ id: 'fps1', ext: 'extruder', op: 'LOADED', bay: 'oams1-0', group: 'T0' }],
  } },
  { caption: 'Both units offline', scenario: withCard((s) => {
    s.units.forEach((u) => { u.online = false; u.env = null; });
  }) },
];

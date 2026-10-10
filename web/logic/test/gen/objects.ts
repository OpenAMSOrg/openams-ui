// The synthetic Moonraker objects the gallery's synth.ts used to build, kept as
// plain data: a scenario becomes a list of apply() calls the test feeds to the
// core. Copied from the retired web/panel gallery,
// so the shapes and the call order are the ones the state sheet was reviewed
// with.
import type { Core } from '../../../core/index.js';

export interface BaySpec {
  /** empty, ready, loaded, error, positioning */
  s: 'e' | 'r' | 'l' | 'x' | 'p';
  material?: string;
  color?: string;          // "#rrggbb"
  pct?: number;
  grams?: number;
  /** the spool's own initial weight, as the component's summary now carries it */
  initialG?: number;
  /** its pressure advance, 0-2; the edit form starts from it when it is there */
  flowK?: number;
  /** a spool link: from an RFID tag ("t") or set by hand ("m"); none = unidentified */
  link?: 't' | 'm';
  pending?: boolean;
}

export interface UnitSpec {
  name: string;
  idx: number;
  family: 'ams1' | 'ams2' | 'ams_ht';
  lane: string;
  bays: BaySpec[];
  online?: boolean;
  env?: { t: number; rh: number } | null;
  dryer?: { state: 'off' | 'preheat' | 'hold' | 'cooldown' | 'fault'; target?: number; min?: number; adapter?: boolean } | null;
}

export interface FaultSpec {
  code: string;
  text: string;
  sev: 'stop' | 'pause' | 'info';
  unit?: string;
  bay?: number;
  act?: string[];
  count?: number;
}

export interface LaneSpec {
  id: string;
  ext: string;
  op: 'LOADED' | 'LOADING' | 'UNLOADED' | 'UNLOADING';
  /** "unit-bay" reference of the loaded (or loading) bay */
  bay?: string;
  group?: string;
  stage?: { plan: string[]; index: number | null; failed: string | null };
  runout?: { from: string; to?: string };
  fault?: FaultSpec;
  pressure?: number;
}

export interface Scenario {
  units: UnitSpec[];
  lanes: LaneSpec[];
  /** group name -> member references, in fallback order */
  groups: Record<string, string[]>;
  /** filament_weights of the job, per tool */
  job?: number[];
}

/** One apply(kind, name, obj) call. */
export type CoreEvent = { kind: string; name: string | null; obj: unknown };

const settings = {
  read_tag_on_insertion: true, read_tags_at_startup: true, auto_reload_on_runout: true,
  auto_create_spoolman_spools: true, apply_pa_on_load: false,
};

function laneObject(l: LaneSpec) {
  return {
    id: l.id, ext: l.ext, op: l.op, group: l.group ?? null, bay: l.bay ?? null, stage: l.stage
      ? [l.stage.plan, l.stage.index, l.stage.failed] : null,
    runout: l.runout ? 'reloading' : 'idle', from: l.runout?.from ?? null, to: l.runout?.to ?? null, ms: l.op === 'LOADED',
  };
}

function unitObject(sc: Scenario, u: UnitSpec, groups: unknown[]) {
  const lane = sc.lanes.find((l) => l.id === u.lane)!;
  const f = lane.fault;
  return {
    v: 1, unit: u.name, idx: u.idx, family: u.family, serial: '', online: u.online ?? true, ready: true, cal: 'c',
    lane: laneObject(lane),
    bays: u.bays.map((b, i) => ({
      id: u.idx * 4 + i, s: b.s, cal: 'c', a: '', sp: b.link ? { id: u.idx * 10 + i, src: b.link } : null,
    })),
    groups,
    env: u.env === undefined ? { t: 24, rh: 31 } : u.env,
    dryer: u.dryer === null ? null : {
      state: u.dryer?.state ?? 'off', target: u.dryer?.target ?? 0, min: u.dryer?.min ?? 0, heat: 0, fan: 0,
      fault: u.dryer?.state === 'fault' ? 'heater' : 'none', adapter: u.dryer?.adapter ?? true },
    fault: f ? { code: f.code, text: f.text, sev: f.sev, unit: f.unit ?? null, bay: f.bay ?? null, act: f.act ?? [] } : null,
    nf: f ? (f.count ?? 1) : 0,
    act: u.family === 'ams_ht' ? ['dryer_start', 'dryer_stop', 'clear_fault'] : ['dryer_start', 'dryer_stop', 'clear_fault', 'rfid_scan'],
  };
}

function statusObject(sc: Scenario) {
  const status: Record<string, unknown> = {};
  sc.units.forEach((u) => u.bays.forEach((b, i) => {
    status[`${u.name}-${i}`] = {
      pending_confirmation: !!b.pending,
      // synth.ts left the key out when a bay has no spool; an explicit null is
      // what takes a spool away again, since apply() merges into what the core
      // already holds. The core then drops the bay's material and empties the
      // tile (it keeps the last remaining_pct it saw). `initial_g` and
      // `flow_k` are optional too, and left out when the scenario has none.
      spool: b.material || b.color ? {
        material: b.material ?? '', vendor: 'Polymaker', color_hex: (b.color ?? '#000000').slice(1),
        remaining_g: b.grams ?? Math.round((b.pct ?? 50) * 10), remaining_pct: b.pct ?? 50,
        ...(b.initialG === undefined ? {} : { initial_g: b.initialG }),
        ...(b.flowK === undefined ? {} : { flow_k: b.flowK }) } : null,
    };
  }));
  return { spoolman_online: true, edit: true, bays: status };
}

/**
 * Every apply() call synth.ts's build() makes for @p sc, in the same order and
 * including the two passes over the units (groups and lane references name bays
 * of units a first pass has not created yet) -- but without the reset, so a
 * caller can feed steps of a sequence back to back.
 */
export function events(sc: Scenario): CoreEvent[] {
  const evs: CoreEvent[] = [{
    kind: 'oams_manager', name: null, obj: {
      ready: true, settings,
      lanes_by_fps: Object.fromEntries(sc.lanes.map((l) => [l.id, { pressure: l.pressure ?? 0.5, set_point: 0.5 }])),
    },
  }];
  const groups = Object.entries(sc.groups).map(([n, b]) => ({ n, b, s: 'r' }));
  for (let pass = 0; pass < 2; pass++) {
    for (const u of sc.units) {
      evs.push({ kind: 'openams_ui', name: u.name, obj: unitObject(sc, u, groups) });
    }
  }
  evs.push({ kind: 'component_status', name: null, obj: statusObject(sc) });
  // The names the editor offers: a repeat, a name in another case, one too long to fit, one that is not a vendor.
  evs.push({
    kind: 'vendor_list', name: null,
    obj: [{ id: 1, name: 'Polymaker' }, { id: 2, name: 'Bambu Lab' }, { id: 3, name: 'polymaker' }, { id: 4, name: 'x'.repeat(40) },
      { id: 5 }, { id: 6, name: 'eSUN' }, { id: 7, name: 'Generic' }],
  });
  if (sc.job) evs.push({ kind: 'metadata', name: null, obj: { filename: 'job.gcode', filament_weights: sc.job } });
  return evs;
}

/** Apply every event to @p core, in order. */
export function applyAll(core: Core, evs: CoreEvent[]): void {
  for (const e of evs) core.apply(e.kind as Parameters<Core['apply']>[0], e.name, e.obj);
}

// Synthetic Moonraker objects for the state sheet, shaped like the recorded
// fixtures (tests/fixtures/recorded/*.json). The sheet feeds them through the real
// core (apply()), never hand-written view JSON, so the rules of the real
// model hold by construction: one loaded spool per FPS, spares numbered
// consecutively within a group, "Spool N" labels (docs/design/UNIFIED_UI.md 4b).
import type { Core, View } from '@logic';

export interface BaySpec {
  /** empty, ready, loaded, error, positioning */
  s: 'e' | 'r' | 'l' | 'x' | 'p';
  material?: string;
  color?: string;          // "#rrggbb"
  pct?: number;
  grams?: number;
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
  /** The oams_manager reading; null leaves the lane out, so the toolhead has none. */
  pressure?: number | null;
  /** The regulator's normal band; the default is [0.3, 0.7], null reports none. */
  band?: [number, number] | null;
}

export interface Scenario {
  units: UnitSpec[];
  lanes: LaneSpec[];
  /** group name -> member references, in fallback order */
  groups: Record<string, string[]>;
  /** filament_weights of the job, per tool */
  job?: number[];
  /** The host has its own sequence stop (`stop` in the component's status): Stop is an RPC. */
  stop?: boolean;
}

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

/** Apply @p sc to @p core (resetting it first) and return the resulting view. */
export function build(core: Core, sc: Scenario): View {
  core.reset(false);
  core.apply('oams_manager', null, {
    ready: true, settings,
    lanes_by_fps: Object.fromEntries(sc.lanes.filter((l) => l.pressure !== null)
      .map((l) => [l.id, { pressure: l.pressure ?? 0.5, set_point: 0.5, pressure_band: l.band === undefined ? [0.3, 0.7] : l.band }])),
  });
  const groups = Object.entries(sc.groups).map(([n, b]) => ({ n, b, s: 'r' }));
  const status: Record<string, unknown> = {};
  sc.units.forEach((u) => u.bays.forEach((b, i) => {
    status[`${u.name}-${i}`] = {
      pending_confirmation: !!b.pending,
      ...(b.material || b.color ? { spool: {
        material: b.material ?? '', vendor: 'Polymaker', color_hex: (b.color ?? '#000000').slice(1),
        remaining_g: b.grams ?? Math.round((b.pct ?? 50) * 10), remaining_pct: b.pct ?? 50 } } : {}),
    };
  }));
  // Units are applied twice: groups and lane references name bays of units that
  // a first pass has not created yet.
  for (let pass = 0; pass < 2; pass++) {
    for (const u of sc.units) {
      const lane = sc.lanes.find((l) => l.id === u.lane)!;
      const f = lane.fault;
      core.apply('openams_ui', u.name, {
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
      });
    }
  }
  core.apply('component_status', null, { spoolman_online: true, edit: true, stop: sc.stop === true, bays: status });
  core.apply('vendor_list', null, [{ id: 1, name: 'Polymaker' }, { id: 2, name: 'Bambu Lab' }, { id: 3, name: 'Prusament' }]);
  if (sc.job) core.apply('metadata', null, { filename: 'job.gcode', filament_weights: sc.job });
  return core.view();
}

// Scenarios that obey the real rules (one loaded spool per FPS, spare numbers
// consecutive within a group) drawn from a seeded rng, and sequences of such
// scenarios on one fixed topology.
import { chance, int, pick, sample } from './rng.ts';
import type { BaySpec, FaultSpec, LaneSpec, Scenario } from './objects.ts';

export const MATERIALS = ['ASA', 'PLA', 'PETG', 'PC', 'ABS', 'TPU', 'PA', 'PVB'] as const;
export const COLORS = ['#000000', '#d32f2f', '#2f8cf0', '#3db84c', '#e6b422', '#8e4ad0', '#f0f0ea', '#5a5e64'] as const;
/** The load steps the core shows on the stepper. */
export const STEP_PLAN = ['cut', 'retract', 'feed', 'purge'];
/** The unload steps. The core takes the stage but shows no stepper for one. */
export const UNLOAD_PLAN = ['Retract from extruder', 'Pull back to the bay', 'Park'];
/** The actions a fault may offer. */
export const ACTIONS = ['retry_load', 'resume', 'clear_errors'] as const;
const FAULT_TEXTS = [
  'The feeder stalled', 'The filament did not reach the extruder', 'The lane bus is down',
  'The bay did not report back', 'The toolhead is not ready',
] as const;
const CODES = ['feeder_stall', 'load_failed', 'lane_bus', 'bay_lost', 'toolhead_busy'] as const;
const DRYERS = ['off', 'preheat', 'hold', 'cooldown', 'fault'] as const;
type Op = 'LOADED' | 'LOADING' | 'UNLOADED' | 'UNLOADING';

/** One unit of the fixed shape a sequence keeps. */
export interface TopoUnit {
  name: string;
  idx: number;
  family: 'ams1' | 'ams2' | 'ams_ht';
  lane: string;
  /** How many bays the unit has; which of them hold a spool moves step to step. */
  bays: number;
}

/** One FPS of the fixed shape a sequence keeps. */
export interface TopoLane {
  id: string;
  ext: string;
  units: string[];
  /** "unit-bay" of the bay this lane loads from. */
  primary: string;
  /** The tool group @p primary belongs to, and its other members (fallbacks). */
  group: string;
  spares: string[];
  op: Op;
}

export interface Topo {
  lanes: TopoLane[];
  units: TopoUnit[];
  groups: Record<string, string[]>;
  job?: number[];
}

/** Every "unit-bay" of @p u, in bay order. */
function refs(u: TopoUnit): string[] {
  return Array.from({ length: u.bays }, (_v, i) => `${u.name}-${i}`);
}

function laneOf(t: Topo, id: string): TopoLane {
  return t.lanes.find((l) => l.id === id)!;
}

/** Draw the shape a sequence keeps: lanes, units, bay counts, tools. */
export function drawTopology(rng: () => number): Topo {
  const units: TopoUnit[] = [];
  const lanes: TopoLane[] = [];
  const groups: Record<string, string[]> = {};
  const nLanes = int(rng, 1, 3);
  let idx = 0;
  let tool = 0;
  for (let li = 0; li < nLanes; li++) {
    const id = `fps${li + 1}`;
    const laneUnits: TopoUnit[] = [];
    for (let n = int(rng, 1, 2); n > 0; n--) {
      idx += 1;
      const family: TopoUnit['family'] = chance(rng, 0.25) ? 'ams_ht' : chance(rng, 0.2) ? 'ams1' : 'ams2';
      const u: TopoUnit = { name: `oams${idx}`, idx, family, lane: id, bays: family === 'ams_ht' ? 1 : 4 };
      laneUnits.push(u);
      units.push(u);
    }
    const all = laneUnits.flatMap(refs);
    const primary = pick(rng, all);
    const spares = chance(rng, 0.75) ? sample(rng, all.filter((r) => r !== primary), int(rng, 1, 2)) : [];
    const group = `T${tool++}`;
    groups[group] = [primary, ...spares];
    lanes.push({
      id, ext: li === 0 ? 'extruder' : (chance(rng, 0.5) ? 'dragon_burner' : 'extruder'),
      units: laneUnits.map((u) => u.name), primary, group, spares,
      op: pick(rng, ['LOADED', 'LOADED', 'LOADING', 'UNLOADING', 'UNLOADED'] as Op[]),
    });
  }
  // Bays left over: a tool of their own, or no tool at all.
  for (const r of units.flatMap(refs).filter((r) => !inAnyGroup(groups, r))) {
    if (chance(rng, 0.4)) groups[`T${tool++}`] = [r];
  }
  const topo: Topo = { lanes, units, groups };
  if (chance(rng, 0.4)) topo.job = lanes.map(() => int(rng, 0, 600));
  return topo;
}

function inAnyGroup(groups: Record<string, string[]>, ref: string): boolean {
  return Object.values(groups).some((b) => b.includes(ref));
}

/** A percentage that mostly drifts down, as a job prints. */
function drawPct(rng: () => number, before: number | undefined): number {
  if (before === undefined) return chance(rng, 0.2) ? int(rng, 0, 14) : int(rng, 0, 100);
  const d = chance(rng, 0.75) ? -int(rng, 0, 12) : int(rng, 0, 6);
  return Math.max(0, Math.min(100, before + d));
}

/**
 * The fault on @p l: kept from the last step most of the time, so a step either
 * lets it sit, raises it or clears it. A fault that names a unit and a bay goes
 * with that bay in the error state, which is what the core tags.
 */
function drawFault(rng: () => number, topo: Topo, l: TopoLane, prev: LaneSpec | undefined) {
  const was = prev?.fault;
  if (!chance(rng, 0.15)) return undefined;
  const sev = was && chance(rng, 0.7) ? was.sev : pick(rng, ['stop', 'pause', 'info'] as const);
  const faulted = was && chance(rng, 0.7) ? was : undefined;
  const on = topo.units.filter((u) => u.lane === l.id && u.bays > 1);
  const at = !on.length ? undefined
    : faulted?.unit ? on.find((u) => u.name === faulted.unit) ?? pick(rng, on)
      : (chance(rng, 0.6) ? pick(rng, on) : undefined);
  const bay = at ? pick(rng, Array.from({ length: at.bays }, (_v, i) => i).filter((i) => `${at.name}-${i}` !== l.primary)) : undefined;
  return {
    code: faulted?.code ?? pick(rng, CODES),
    text: faulted?.text ?? pick(rng, FAULT_TEXTS),
    sev,
    ...(at ? { unit: at.name, bay } : {}),
    act: sample(rng, ACTIONS, int(rng, 1, 3)),
    ...(chance(rng, 0.3) ? { count: int(rng, 1, 3) } : {}),
  };
}

/**
 * A bay's state, given what the lane and the fault need. @p has says whether
 * the bay holds a spool at all: a bay that holds none is empty or unidentified.
 */
function bayState(rng: () => number, o: { primary: boolean; op: Op; has: boolean; broken: boolean }): BaySpec['s'] {
  if (o.primary) {
    if (o.op === 'LOADED' || o.op === 'UNLOADING') return 'l';
    if (o.op === 'LOADING') return chance(rng, 0.7) ? 'p' : 'r';
    return chance(rng, 0.8) ? 'r' : 'p';
  }
  if (o.broken) return 'x';
  if (!o.has) return chance(rng, 0.7) ? 'e' : 'r';
  return pick(rng, ['r', 'r', 'r', 'r', 'p', 'p', 'x'] as const);
}

/** The state a unit's optional fields hold: kept from @p before, or drawn. */
function sticky<T>(rng: () => number, had: boolean, before: T, redraw: number, draw: () => T): T {
  return !had || chance(rng, redraw) ? draw() : before;
}

/**
 * One scenario on @p topo. With @p prev (the last step) the drawn parts stay put
 * or move a little, so consecutive steps are one print apart; without it every
 * part is drawn. A bay can gain or lose its spool between steps: an empty bay
 * that had one is given `spool: null`, which is what takes it away again.
 */
export function flesh(topo: Topo, rng: () => number, prev?: Scenario): Scenario {
  const sc: Scenario = { units: [], lanes: [], groups: topo.groups };
  if (topo.job) sc.job = topo.job;
  const broken = new Set<string>();
  const vol = new Map<string, { fault?: FaultSpec; runout?: { from: string; to?: string } }>();
  for (const l of topo.lanes) {
    const was = prev?.lanes.find((x) => x.id === l.id);
    const fault = drawFault(rng, topo, l, was);
    if (fault?.unit && fault.bay !== undefined) broken.add(`${fault.unit}-${fault.bay}`);
    const runout = l.op === 'LOADED' && (was?.runout ? chance(rng, 0.6) : chance(rng, 0.2))
      ? { from: l.primary, ...(l.spares.length && chance(rng, 0.8) ? { to: pick(rng, l.spares) } : {}) }
      : undefined;
    vol.set(l.id, { ...(fault ? { fault } : {}), ...(runout ? { runout } : {}) });
  }
  for (const u of topo.units) {
    const l = laneOf(topo, u.lane);
    const was = prev?.units.find((x) => x.name === u.name);
    const runout = vol.get(l.id)?.runout;
    sc.units.push({
      name: u.name, idx: u.idx, family: u.family, lane: u.lane,
      bays: Array.from({ length: u.bays }, (_v, i) => {
        const ref = `${u.name}-${i}`;
        const before = was?.bays[i];
        const primary = ref === l.primary;
        // The bay a lane names, and a bay the fault points at, keep a spool.
        const isBroken = broken.has(ref);
        const has = primary || isBroken ? true
          : sticky(rng, !!before, before?.material !== undefined, 0.12, () => chance(rng, 0.55));
        const lost = before?.material !== undefined && !has;
        const s = lost ? 'e' : bayState(rng, { primary, op: l.op, has, broken: isBroken });
        if (s === 'e') return { s };
        const link = before?.link ?? (chance(rng, 0.6) ? pick(rng, ['t', 'm'] as const) : undefined);
        const pct = runout && ref === runout.from ? 0 : drawPct(rng, before?.material ? before.pct : undefined);
        return {
          s, material: before?.material ?? pick(rng, MATERIALS), color: before?.color ?? pick(rng, COLORS),
          ...(link ? { link } : {}), pct,
          grams: runout && ref === runout.from ? 0 : (before?.grams ?? Math.round(pct * 10)),
          // half the spools publish their own size and pressure advance, and a
          // bay that loses its spool loses them with it
          ...(chance(rng, 0.5)
            ? { initialG: sticky(rng, before?.initialG !== undefined, before?.initialG ?? 0, 0.1, () => int(rng, 250, 4000)) }
            : {}),
          ...(chance(rng, 0.5)
            ? { flowK: sticky(rng, before?.flowK !== undefined, before?.flowK ?? 0, 0.1, () => int(rng, 0, 2000) / 1000) }
            : {}),
          ...(chance(rng, 0.08) ? { pending: true } : {}),
        };
      }),
      online: sticky(rng, !!was, was?.online ?? true, 0.15, () => !chance(rng, 0.07)),
      env: sticky(rng, !!was, was?.env ?? null, 0.15,
        () => (chance(rng, 0.15) ? null : { t: int(rng, 18, 30), rh: int(rng, 25, 60) })),
      dryer: sticky(rng, !!was, was?.dryer ?? { state: 'off' as const }, 0.2, () => {
        const state = pick(rng, ['off', 'preheat', 'hold', 'cooldown', 'fault'] as const);
        if (state === 'off') return { state };
        return {
          state, target: int(rng, 45, 65), min: int(rng, 0, 480),
          ...(chance(rng, 0.25) ? { adapter: false } : {}),
        };
      }),
    });
  }
  for (const l of topo.lanes) {
    const was = prev?.lanes.find((x) => x.id === l.id);
    const v = vol.get(l.id)!;
    const plan = l.op === 'UNLOADING' ? UNLOAD_PLAN : l.op === 'LOADING' ? STEP_PLAN : null;
    sc.lanes.push({
      id: l.id, ext: l.ext, op: l.op,
      ...(l.op === 'UNLOADED' ? {} : { bay: l.primary, group: l.group }),
      ...(plan ? {
        stage: {
          plan: [...plan],
          index: sticky(rng, !!was?.stage, was?.stage?.index ?? 0, 0.6, () => int(rng, 0, plan.length - 1)),
          failed: null,
        },
      } : {}),
      ...(v.runout ? { runout: v.runout } : {}),
      ...(v.fault ? { fault: v.fault } : {}),
      pressure: was?.pressure ?? (chance(rng, 0.3) ? int(rng, 1, 9) / 10 : 0.5),
    });
  }
  return sc;
}

/** A fresh scenario: every part drawn. */
export function randomScenario(rng: () => number): Scenario {
  return flesh(drawTopology(rng), rng);
}

/** @p steps scenarios on one fixed topology, each a little different from the last. */
export function randomSequence(rng: () => number, steps: number): Scenario[] {
  const topo = drawTopology(rng);
  const seq: Scenario[] = [flesh(topo, rng)];
  for (let n = 1; n < steps; n++) seq.push(changed(seq[n - 1], flesh(topo, rng, seq[n - 1])));
  return seq;
}

/** A step that is never the step before it: one percentage less if nothing moved. */
function changed(before: Scenario, next: Scenario): Scenario {
  if (JSON.stringify(before) !== JSON.stringify(next)) return next;
  for (const u of next.units) {
    for (const b of u.bays) {
      if (b.pct === undefined || b.pct === 0) continue;
      b.pct -= 1;
      b.grams = Math.max(0, (b.grams ?? b.pct * 10) - 10);
      return next;
    }
  }
  return next;
}

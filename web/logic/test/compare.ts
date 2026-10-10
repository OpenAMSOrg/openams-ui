// The differential harness: the C core (its wasm build, the oracle) and the
// TypeScript logic side by side, fed the same objects, compared everywhere a
// user could see a difference. Not a test file itself: diff.test.ts uses it,
// and the random-sequence suite (gen.test.ts) calls compareSequence().

import { readdirSync, readFileSync } from 'node:fs';
import { loadCore } from '../../core/index.js';
import { LANG_CODES } from '../strings.ts';
import type { Core, View, ViewAction } from '../types.ts';
import { createLogic } from '../index.ts';

export const repoRoot = new URL('../../../', import.meta.url);
export const vamsDir = new URL('tests/fixtures/recorded/', repoRoot);

/** One object handed to a core: what `Core.apply()` takes. */
export interface Step {
  kind: string;
  name: string | null;
  obj: unknown;
}

// ------------------------------------------------------------ both cores

export interface Pair {
  c: Core;
  ts: Core;
}

/** The C core (wasm, one instance per page) and a fresh TypeScript core. */
export async function bothCores(): Promise<Pair> {
  const c = (await loadCore()) as unknown as Core;
  return { c, ts: createLogic() };
}

export function resetBoth(p: Pair, keepAlerts = false): void {
  p.c.reset(keepAlerts);
  p.ts.reset(keepAlerts);
}

/** Every language built into this image, from the TypeScript tables (the C
 *  has the same list, generated from the same files). */
export const langCodes = (): string[] => LANG_CODES.map(String);

/** Put both cores into the same language. The choice is module state on both
 *  sides, so the pair is only comparable one language at a time; the callers
 *  run the whole suite once per language. Returns what both answered. */
export function setLanguageBoth(p: Pair, code: string): boolean {
  const rc = p.c.setLanguage(code);
  const rt = p.ts.setLanguage(code);
  if (rc !== rt) throw new Error(`setLanguage(${code}): C ${rc} vs TS ${rt}`);
  return rc;
}

// ------------------------------------------------------------------ diffing

const show = (v: unknown): string => {
  const text = JSON.stringify(v);
  return text === undefined ? 'undefined' : text.length > 120 ? `${text.slice(0, 117)}...` : text;
};

/**
 * The first difference between the C answer `c` and the TypeScript answer
 * `ts`, as a path ("toolheads[1].message.text: C "..." vs TS "...""), or null
 * when they are deeply equal. Object key order does not matter.
 */
export function firstDiff(c: unknown, ts: unknown, path = ''): string | null {
  if (Object.is(c, ts)) return null;
  const at = path || '(root)';
  if (typeof c !== 'object' || typeof ts !== 'object' || c === null || ts === null) {
    return `${at}: C ${show(c)} vs TS ${show(ts)}`;
  }
  if (Array.isArray(c) !== Array.isArray(ts)) return `${at}: C ${show(c)} vs TS ${show(ts)}`;
  if (Array.isArray(c) && Array.isArray(ts)) {
    if (c.length !== ts.length) return `${at}.length: C ${c.length} vs TS ${ts.length}`;
    for (let i = 0; i < c.length; i++) {
      const d = firstDiff(c[i], ts[i], `${path}[${i}]`);
      if (d) return d;
    }
    return null;
  }
  const oc = c as Record<string, unknown>;
  const ot = ts as Record<string, unknown>;
  for (const key of new Set([...Object.keys(oc), ...Object.keys(ot)])) {
    const sub = path ? `${path}.${key}` : key;
    if (!(key in oc)) return `${sub}: C (absent) vs TS ${show(ot[key])}`;
    if (!(key in ot)) return `${sub}: C ${show(oc[key])} vs TS (absent)`;
    const d = firstDiff(oc[key], ot[key], sub);
    if (d) return d;
  }
  return null;
}

// ------------------------------------------------------------------ actions

/** Every action of a view, wherever it sits (toolheads, units, tiles, alert
 *  items, the settings). */
export function allActions(v: View): ViewAction[] {
  const out: ViewAction[] = [];
  const alertActions = (g: { items: { actions: ViewAction[] }[] } | null): void => {
    for (const item of g?.items ?? []) out.push(...item.actions);
  };
  for (const th of v.toolheads) {
    out.push(...th.actions);
    alertActions(th.alert);
    for (const u of th.units) {
      out.push(...u.actions);
      alertActions(u.alert);
      for (const t of u.bays) out.push(...t.actions);
    }
  }
  for (const u of v.unassigned_units) {
    out.push(...u.actions);
    for (const t of u.bays) out.push(...t.actions);
  }
  for (const a of v.alerts) out.push(...a.actions);
  for (const s of v.settings) {
    if (s.action_on) out.push(s.action_on);
    if (s.action_off) out.push(s.action_off);
  }
  return out;
}

/** A form for an action: the first option of a select field, else the
 *  field's own default. */
export function formFor(a: ViewAction): Record<string, string | number | boolean> | null {
  if (!a.form) return null;
  return Object.fromEntries(a.form.fields.map((f) => [f.id, f.options?.[0]?.value ?? f.value]));
}

/** Lines that are not in any view: odd spellings, missing values, the local
 *  vocabulary, and every verb with a made-up argument. */
export const EXTRA_LINES = [
  '', 'nonsense', 'load', 'load nothing', 'unload nothing', 'stop', 'stop drying', 'stop fps9',
  'reread oams99', 'calibrate oams99', 'dry oams1 start 50C 4h', 'dry oams1 start 50C', 'dry oams1 stop',
  'dry oams1 target 50', 'dry nobody start 50C 4h', 'setting read-on-insertion on', 'setting read_tag_on_insertion off',
  'setting apply_pa_on_load maybe', 'setting bogus on', 'create group T9', 'group create T8', 'link oams11 7',
  'link oams11', 'assign spool oams11 7', 'assign T1 oams12', 'assign T1 nothing', 'unassign oams11', 'unassign oams13',
  'unlink oams11', 'fault clear_errors', 'fault clear_fault oams1', 'fault clear_fault nobody', 'fault retry_load T1',
  'fault resume', 'resume', 'pause and load oams11', 'pause and load oams12', 'spool list', 'spool confirm oams11 250',
  'spool confirm oams11', 'spool confirm nothing 5', 'job metadata a.gcode', 'job metadata ', 'dry {unit} start {target}C {hours}h',
  'load {slot}', 'load {', 'load }{', 'x {a}{b} y',
  'vendor list', 'spool edit', 'spool edit oams99', 'spool edit oams11', 'spool edit oams11 pa=0.05',
  'spool edit oams11 pa=2.5', 'spool edit oams11 pa=-1', 'spool edit oams11 pa=1e-2', 'spool edit oams11 pa=.5',
  'spool edit oams11 color=255', 'spool edit oams11 color=-1', 'spool edit oams11 color=16777216',
  'spool edit oams11 material=1 vendor=1', 'spool edit oams11 material=99', 'spool edit oams11 vendor=99',
  'spool edit oams11 remaining=500 initial=400', 'spool edit oams11 remaining=500', 'spool edit oams11 initial=0',
  'spool edit oams11 remaining=-5', 'spool edit oams11 remaining=200000', 'spool edit oams11 bogus=1',
  'spool edit oams11 material=', 'spool edit oams11 =5', 'spool edit oams11 material=x', 'spool edit oams11 material=1 material=2',
  'spool edit oams12 material=10', 'spool edit oams12 vendor=3 color=10 remaining=100 initial=900 pa=0.04',
  'spool edit oams13 material=2', 'spool edit oams14 color=1',
  // an unidentified bay's Load: the edit and then the load (H5)
  'spool edit oams12 material=-1 then load', 'spool edit oams12 material=2 then load',
  'spool edit oams12 then load', 'spool edit oams11 material=2 then load', 'spool edit oams13 material=2 then load',
  'spool edit oams12 material=2 then', 'spool edit oams12 material=2 then unload',
  'spool edit oams12 material=-1', 'spool edit oams12 then load material=2',
];

/** An action's answer as the C core gives it. The wasm bridge answers an RPC
 *  with its method and params alone, so a `then` the TypeScript side adds (the
 *  load a spool edit releases) is the one field it cannot yet say; the C side
 *  of it is tested in tests/test_actions_map.c. */
function dropThen<T>(c: T, ts: T): T {
  const r = ts as { then?: string };
  if (r && typeof r === 'object' && r.then !== undefined && (c as { then?: string }).then === undefined) {
    const { then: _then, ...rest } = r;
    return rest as T;
  }
  return ts;
}

/** The first difference between the two cores' answers for `lines`, and for
 *  every action of `view` (with its form), or null. */
export function compareActions(p: Pair, view: View, where: string): string | null {
  for (const a of allActions(view)) {
    const form = formFor(a);
    const cr = p.c.action(a.line, form);
    const d = firstDiff(cr, dropThen(cr, p.ts.action(a.line, form)), `action(${JSON.stringify(a.line)})`);
    if (d) return `${where}: ${d}`;
    // Without a form, a placeholder line must fail the same way in both.
    const cb = p.c.action(a.line);
    const bare = firstDiff(cb, dropThen(cb, p.ts.action(a.line)), `action(${JSON.stringify(a.line)}, no form)`);
    if (bare) return `${where}: ${bare}`;
  }
  for (const line of EXTRA_LINES) {
    const cx = p.c.action(line);
    const d = firstDiff(cx, dropThen(cx, p.ts.action(line)), `action(${JSON.stringify(line)})`);
    if (d) return `${where}: ${d}`;
  }
  return null;
}

// ------------------------------------------------------------------ steps

/** Apply one object to both cores; the first difference in what `apply()`
 *  returned or in the resulting view, or null. Actions are compared too when
 *  `withActions`. */
export function applyBoth(p: Pair, s: Step, where: string, withActions = false): string | null {
  const rc = p.c.apply(s.kind, s.name, s.obj);
  const rt = p.ts.apply(s.kind, s.name, s.obj);
  if (rc !== rt) return `${where}: apply(${s.kind}) returned C ${rc} vs TS ${rt}`;
  const vc = p.c.view();
  const d = firstDiff(vc, p.ts.view());
  if (d) return `${where}: ${d}`;
  return withActions ? compareActions(p, vc, where) : null;
}

/**
 * The hook for random sequences: reset both cores, then apply each step's
 * objects in order without resetting between steps, comparing what `apply()`
 * returns and the whole view after every object, and every action after every
 * step. Returns the first mismatch as a path, or null.
 */
export function compareSequence(steps: Step[][], p?: Pair): Promise<string | null> {
  return (async () => {
    const pair = p ?? await bothCores();
    resetBoth(pair);
    for (let i = 0; i < steps.length; i++) {
      for (let j = 0; j < steps[i].length; j++) {
        const d = applyBoth(pair, steps[i][j], `step ${i} object ${j}`, false);
        if (d) return d;
      }
      const d = compareActions(pair, pair.c.view(), `after step ${i}`);
      if (d) return d;
    }
    return null;
  })();
}

// ---------------------------------------------------------------- fixtures

export interface FixtureEvent {
  kind: string;
  unit?: string | null;
  payload?: unknown;
}

export function fixtureNames(): string[] {
  return readdirSync(vamsDir)
    .filter((f) => f.endsWith('.json') && !f.endsWith('.view.json'))
    .map((f) => f.slice(0, -'.json'.length))
    .sort();
}

/** A recorded fixture's events as core objects: recorded's `c2_status` is the
 *  `oams_manager` object; the others keep their kind and unit. */
export function fixtureSteps(name: string): Step[] {
  const doc = JSON.parse(readFileSync(new URL(`${name}.json`, vamsDir), 'utf8')) as { events: FixtureEvent[] };
  return doc.events.filter((e) => e.payload).map((e) => (
    e.kind === 'c2_status'
      ? { kind: 'oams_manager', name: null, obj: e.payload }
      : { kind: e.kind, name: e.unit ?? null, obj: e.payload }));
}

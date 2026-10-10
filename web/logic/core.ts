// The core's web API (src/core/core.c and platform/core-wasm/core_wasm.c, in
// web/core/index.js's shape): objects in, a view tree and action results out.
//
//   const core = createLogic();
//   core.apply('openams_ui', 'oams1', statusObject);   // a whole object or a diff
//   const view = core.view();                          // the panel tree
//   core.action('unload fps1');                        // { kind: 'gcode', script }
//   const stop = core.subscribe((view) => render(view)); // at most once per frame
//
// It owns one copy of every object it is fed (per unit for openams_ui) and
// merges a diff into it key by key, then maps the merged copy: a whole object
// and a diff are both correct.

import { coreAction } from './actions.ts';
import { cut, jsonEqual } from './cstr.ts';
import {
  actionResult, ActionKind, applyComponentStatus, applyLanes, applyMetadata, applyOpenamsUi,
  applyPrintStats, applySettings, applySpoolList, applyVendorList, drain, MAX_UNITS, newModel, refreshThis,
} from './model.ts';
import type { Model } from './model.ts';
import { setLanguage } from './strings.ts';
import { buildView } from './view.ts';
import type { ActionResult, Core, ObjectKind, View } from './types.ts';

/** One openams_ui copy per unit, plus one each for the singleton kinds. */
const MAX_ENTRIES = MAX_UNITS + 7;
// Every kind must fit whole (the C's CORE_KIND_LEN): a kind cut short is never
// found again, so each apply would take a new entry until the table is full.
const KIND_LEN = 24;
const NAME_LEN = 48;   // CORE_NAME_LEN

type Json = Record<string, unknown>;

interface Entry {
  kind: string;
  name: string;
  copy: unknown;
}

const isObject = (v: unknown): v is Json => typeof v === 'object' && v !== null && !Array.isArray(v);

/** A whole number at `key`, or null when it is not one. */
const jnum = (v: unknown): number | null => (typeof v === 'number' && Number.isInteger(v) ? v : null);

/** Add or replace every key of `update` on `dst`; whether anything changed.
 *  A status diff arrives several times a second, so keys are compared one by
 *  one instead of replacing the whole object. */
function mergeInto(dst: Json, update: Json): boolean {
  let changed = false;
  for (const [key, value] of Object.entries(update)) {
    if (key in dst && jsonEqual(dst[key], value)) continue;
    dst[key] = value;
    changed = true;
  }
  return changed;
}

/** The TypeScript core: the same API and, held by tests, the same answers as
 *  the C core's WebAssembly build. */
export class OpenamsLogic implements Core {
  private model: Model = newModel();
  private entries: Entry[] = [];
  private listeners = new Set<(view: View) => void>();
  private flushPending = false;
  private fixedClock: number | null = null;
  private tick: ReturnType<typeof setInterval> | null = null;

  /** Set the model's clock to @p ms (tests do, to stay deterministic), or null
   *  to follow Date.now() again. The clock stamps alerts as they are raised
   *  and decides when a read one leaves the toolhead's badge. */
  setClock(ms: number | null): void {
    this.fixedClock = ms === null ? null : ms >>> 0;
    this.model.nowMs = this.fixedClock ?? (Date.now() >>> 0);
  }

  private stamp(): void {
    this.model.nowMs = this.fixedClock ?? (Date.now() >>> 0);
  }

  /** The entry for (kind, name), created when new; null when the table is
   *  full. Stored kind and name are cut like the C's buffers, and looked up by
   *  the cut name too, so a name too long to fit is still found again. */
  private entry(kind: string, name: string): Entry | null {
    const key = cut(name, NAME_LEN);
    const found = this.entries.find((e) => e.kind === kind && e.name === key);
    if (found) return found;
    if (this.entries.length >= MAX_ENTRIES) return null;
    const created: Entry = { kind: cut(kind, KIND_LEN), name: key, copy: null };
    this.entries.push(created);
    return created;
  }

  /** Merge `obj` into the owned copy of (kind, name) and map the result.
   *  Returns whether the copy's content changed. */
  apply(kind: ObjectKind | string, name: string | null | undefined, obj: unknown): boolean {
    // Objects cross the boundary as JSON, as they do into the wasm module.
    this.stamp();
    const json = JSON.stringify(obj);
    if (json === undefined) throw new TypeError('openams-core: apply() needs a JSON value');
    const value: unknown = JSON.parse(json);
    const changed = this.applyJson(kind, name ?? '', value);
    // Alerts the mapping queued join the history, the way the wasm shim drains them.
    drain(this.model);
    this.requestFlush();
    return changed;
  }

  private applyJson(kind: string, name: string, obj: unknown): boolean {
    const m = this.model;

    // The host's answer to an action the display sent. It is not a status, so
    // it owns no copy and goes straight to the model.
    if (kind === 'action_result') {
      if (!isObject(obj)) return false;
      const k = jnum(obj.kind);
      if (k === null || k < ActionKind.OTHER || k > ActionKind.UNLOAD) return false;
      actionResult(m, k as ActionKind, obj.ok === true, typeof obj.message === 'string' ? obj.message : '');
      return true;
    }

    // spool_list, vendor_list and metadata are whole results, so the owned copy is replaced.
    if (kind === 'spool_list' || kind === 'vendor_list' || kind === 'metadata') {
      if (kind === 'metadata' ? !isObject(obj) : !Array.isArray(obj)) return false;
      const e = this.entry(kind, name);
      if (!e) return false;
      const changed = !(e.copy !== null && jsonEqual(e.copy, obj));
      e.copy = obj;
      if (kind === 'spool_list') applySpoolList(m, obj);
      else if (kind === 'vendor_list') applyVendorList(m, obj);
      else applyMetadata(m, obj);
      refreshThis(m);
      return changed;
    }

    if (!['openams_ui', 'oams_manager', 'component_status', 'print_stats', 'toolhead'].includes(kind)) return false;
    if (!isObject(obj)) return false;

    const e = this.entry(kind, name);
    if (!e) return false;
    if (e.copy === null) e.copy = {};
    const copy = e.copy as Json;
    const changed = mergeInto(copy, obj);

    if (kind === 'openams_ui') {
      applyOpenamsUi(m, copy);
    } else if (kind === 'oams_manager') {
      if (isObject(copy.settings)) applySettings(m, copy.settings);
      if (isObject(copy.lanes_by_fps)) applyLanes(m, copy.lanes_by_fps);
    } else if (kind === 'component_status') {
      applyComponentStatus(m, copy);
    } else if (kind === 'print_stats') {
      applyPrintStats(m, copy);
    }
    // `toolhead` (the active extruder) has no consumer yet: its copy is only kept.

    refreshThis(m);
    return changed;
  }

  /** Drop every owned copy and reset the model, keeping the alert history
   *  when `keepAlerts`. Call on a printer reset and on a socket reconnect. */
  reset(keepAlerts?: boolean): void {
    const saved = keepAlerts ? this.model.alerts : [];
    this.entries = [];
    this.model = newModel();
    this.model.alerts = saved;
    this.requestFlush();
  }

  /** The current panel tree. */
  view(): View {
    this.stamp();
    return buildView(this.model);
  }

  /** Resolve an action line (a form fills its `{field}` placeholders). */
  action(line: string, form?: Record<string, string | number | boolean> | null): ActionResult {
    return coreAction(this.model, String(line), form ?? null);
  }

  /** Show the view's text in `code` ("en", "qps"). False when no such
   *  language is built in; the current one is kept. The language is shared
   *  module state (the wasm core has one model per module too), so a caller
   *  that wants two languages at once needs two pages. */
  setLanguage(code: string): boolean {
    return setLanguage(code);
  }

  /** Call `listener` with the current view after any apply() or reset(), at
   *  most once per animation frame (a microtask where there is none). There is
   *  no call on subscribe: read view() for the first paint. Returns the
   *  unsubscribe function. */
  subscribe(listener: (view: View) => void): () => void {
    if (typeof listener !== 'function') throw new TypeError('openams-core: subscribe() needs a function');
    this.listeners.add(listener);
    // An entry ages out of a badge with no new data: look again now and then.
    if (this.tick === null && typeof setInterval === 'function') {
      this.tick = setInterval(() => this.flush(), 15000);
      (this.tick as { unref?: () => void }).unref?.();
    }
    return () => {
      this.listeners.delete(listener);
      if (this.listeners.size === 0 && this.tick !== null) { clearInterval(this.tick); this.tick = null; }
    };
  }

  private requestFlush(): void {
    if (this.flushPending || this.listeners.size === 0) return;
    this.flushPending = true;
    const flush = (): void => this.flush();
    if (typeof requestAnimationFrame === 'function') requestAnimationFrame(flush);
    else queueMicrotask(flush);
  }

  private flush(): void {
    this.flushPending = false;
    if (this.listeners.size === 0) return;
    const view = this.view();
    for (const listener of Array.from(this.listeners)) {
      try {
        listener(view);
      } catch (err) {
        console.error('openams-core: a subscriber threw', err);
      }
    }
  }
}

/** A fresh core (the wasm build is a singleton; the logic is plain state). */
export function createLogic(): OpenamsLogic {
  return new OpenamsLogic();
}

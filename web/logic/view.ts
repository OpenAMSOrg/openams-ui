// The view builders (src/view/view.c and view_json.c): the one place that turns
// the model into content. Both renderers draw only what these return; a
// renderer never reads the model and never composes text of its own
// (docs/design/UNIFIED_UI.md 2).
//
// The C fills fixed structs and serializes them; this builds the same JSON
// shapes directly. The text limits of those structs are kept (every string is
// cut to its field's size), and the differential tests hold the two identical.

import { cjsonNumber, cut, f32, fmtFixed } from './cstr.ts';
import { actionsCheck, dryReason } from './actions.ts';
import {
  Busy, DryState, InfoSource, CalState, SlotState, Sev, Variant, MAX_TOOLS, MATERIALS, editDefaults,
  editVendorCount, editVendorName, GROUP_CHOICE_NEW, GROUP_CHOICE_NONE, groupEditReason, groupNextSpare,
  groupOnUnitLane, slotGroup, slotId, toolShortfall, alertBadgeExpired,
} from './model.ts';
import type { Alert, Model, Toolhead, Unit } from './model.ts';
import { AlertKind } from './model.ts';
import {
  copy, fmtDryerActive, fmtEnv, fmtExtruderTitle, fmtGrams, fmtPct, fmtPressure, fmtRunout, fmtRunoutNoSpare,
  fmtLowFilament, fmtScale, fmtShortfall, fmtSpoolError, fmtSpoolLabel, fmtSpoolOption, fmtToolSpool, fmtUnitBay, fmtUnitSpool,
  format, language, languages, SETTING_KEYS, str,
} from './strings.ts';
import type {
  ActionStyle, Ink, Severity, Tone, View, ViewAction, ViewField, ViewFieldOption, ViewAlert, ViewAlertGroup, ViewGroup,
  ViewGroupMember, ViewSetting, ViewTile, ViewToolhead, ViewUnit,
} from './types.ts';

// The C's text field sizes (src/view/view.h), so text is cut where the C cuts
// it. Only the fields that hold *composed* text have a size left: a fixed
// string is a pointer into a const table in C, so the TypeScript builders
// hand the table's text over whole and never cut it. The differential tests
// hold the two identical.
const ID = 40;   // VIEW_ID_LEN (MMU_SLOT_ID_LEN)
const LABEL = 16;
const MATERIAL = 24;
const GRAMS = 20;
const PCT = 16;
const TITLE = 32;
const SUBLABEL = 32;
const MESSAGE = 176;
const PRESSURE_TEXT = 12;
const STEP_DETAIL = 128;        // VIEW_STEP_DETAIL_LEN in src/view/view.h: "{step} · {detail}"
const REST_PATH = 128;          // VIEW_REST_PATH_LEN: "{tool} · {spool} · {material}"
const SCALE = 8;
const UNIT_ID = 32;   // VIEW_UNIT_ID_LEN (MMU_UNIT_NAME_LEN)
const ENV_TEXT = 48;
const DRYER_TEXT = 56;
const ALERT_CODE = 40;
const ALERT_UNIT = 8;
const ALERT_SLOT = 32;
const ALERT_TEXT = 144;       // VIEW_ALERT_TEXT_LEN: a composed alert's sentence
const SPOOL_NAME = 64;        // the {spool} of a low-filament sentence
const RUNOUT_TARGET = LABEL * 3;
const SETTING_KEY = 32;
const ACTION_ID = 20;
const ACTION_LINE = 168;
const OPTION_LABEL = 72;

const MAX_ACTIONS = 10;   // VIEW_ACTIONS_MAX in src/view/view.h
const MAX_ACTIONS_PER_ALERT = 2;
const MAX_ALERTS_PER_GROUP = 6;
const MAX_OPTIONS = 160;  // every select of one action list shares the pool, as in C
const MAX_ALERTS = 8;
const MAX_PENDING_SPOOLS = 8;
const MAX_UNASSIGNED = 6;
const MAX_SETTINGS = 6;
const LANGUAGE_SETTING_ROW = MAX_SETTINGS - 1;

// -------------------------------------------------------------- geometry

/** "light" text (on a dark filament) or "dark" text, by relative luminance
 *  0.2126 R + 0.7152 G + 0.0722 B > 160 (design/tokens.json "ink_threshold"),
 *  kept in integers like the C. */
function inkFor(rgb: number): Ink {
  const r = (rgb >> 16) & 0xff;
  const g = (rgb >> 8) & 0xff;
  const b = rgb & 0xff;
  return 2126 * r + 7152 * g + 722 * b > 1600000 ? 'dark' : 'light';
}

/** Whether a filament color is too close to the dark surface to read, so the
 *  renderer draws a light 1 px outline around it (outline_for() in view.c):
 *  integer luminance with a gamma of 2, below 0.065 (about 1.5:1 against the
 *  lightest surface). */
const OUTLINE_LUM_MAX = 42266250;
function outlineFor(rgb: number): boolean {
  const r = (rgb >> 16) & 0xff;
  const g = (rgb >> 8) & 0xff;
  const b = rgb & 0xff;
  return 2126 * r * r + 7152 * g * g + 722 * b * b < OUTLINE_LUM_MAX;
}

const mixChannel = (fg: number, bg: number, pct: number): number =>
  Math.min(255, Math.max(0, Math.floor((fg * pct + bg * (100 - pct) + 50) / 100)));

function mixRgb(fg: number, bg: number, pct: number): number {
  const r = mixChannel((fg >> 16) & 0xff, (bg >> 16) & 0xff, pct);
  const g = mixChannel((fg >> 8) & 0xff, (bg >> 8) & 0xff, pct);
  const b = mixChannel(fg & 0xff, bg & 0xff, pct);
  return (r << 16) | (g << 8) | b;
}

/** The two stops of a tile's vertical gradient (design/tokens.json "tile"): a
 *  bright tile (dark text) mixes 78% / 42% of the filament, a dark tile 96% / 74%. */
function gradientFor(rgb: number, ink: Ink): [number, number] {
  return ink === 'dark'
    ? [mixRgb(rgb, 0x141414, 78), mixRgb(rgb, 0x1a1a1a, 42)]
    : [mixRgb(rgb, 0x000000, 96), mixRgb(rgb, 0x1a1a1a, 74)];
}

const hex = (rgb: number): string => `#${(rgb & 0xffffff).toString(16).padStart(6, '0')}`;

// ------------------------------------------------------------------ groups

/** Where a tile sits among its group's spares (docs/design/UNIFIED_UI.md 4,
 *  "Spare order"): the loaded tile is drawn as a bare mark when the group has a
 *  usable spare; the others count the non-loaded members in fallback order. */
function computeSpare(m: Model, gi: number, unit: number, slot: number): ViewTile['spare'] {
  const g = m.groups[gi];
  const isLoaded = m.units[unit].slots[slot].state === SlotState.LOADED;

  // A group with one member has no spare: no badge on its one member either way.
  if (g.members.length <= 1) return null;

  let nonLoaded = 0;
  for (const mem of g.members) {
    const s = m.units[mem.unit]?.slots[mem.slot];
    if (!s || s.state === SlotState.LOADED) continue;
    nonLoaded++;
  }

  if (isLoaded) {
    return groupNextSpare(m, gi) ? { position: null, count: nonLoaded } : null;
  }

  let rank = 0;
  for (const mem of g.members) {
    const s = m.units[mem.unit]?.slots[mem.slot];
    if (!s || s.state === SlotState.LOADED) continue;
    rank++;
    if (mem.unit === unit && mem.slot === slot) return { position: rank, count: nonLoaded };
  }
  return null;
}

/** The slicer tool index a group named "T<n>" feeds; -1 when it is not that shape. */
function groupToolIndex(name: string): number {
  if (name[0] !== 'T') return -1;
  const m = /^\s*([+-]?\d+)/.exec(name.slice(1));
  if (!m) return -1;
  const n = Number(m[1]);
  return n < 0 || n >= MAX_TOOLS ? -1 : n;
}

// ----------------------------------------------------------------- actions

/** The actions of one widget, built in order, at most MAX_ACTIONS of them, with
 *  the select-option pool the assign-spool field points into. */
class ActionList {
  items: ViewAction[] = [];

  add(id: string, label: string, line: string, style: ActionStyle): ViewAction | null {
    if (this.items.length >= MAX_ACTIONS) return null;
    const a: ViewAction = {
      id: cut(id, ACTION_ID), label, line: cut(line, ACTION_LINE), enabled: false,
      reason: '', style, target: 'tile', confirm: null, form: null,
    };
    this.items.push(a);
    return a;
  }
}

/** Fill `a`'s enabled and reason from the one predicate (actionsCheck): the
 *  view copies its answer and derives nothing of its own. */
function check(a: ViewAction, line: string, m: Model): void {
  const r = actionsCheck(line, m);
  a.enabled = r.enabled;
  a.reason = r.enabled ? '' : r.why;   // borrowed in C: the table's words, or the host's, never cut
}

/** A plain action: the line is built once, sent as it stands and checked as it stands. */
function simple(list: ActionList, m: Model, id: string, label: string, style: ActionStyle, line: string): ViewAction | null {
  const text = cut(line, ACTION_LINE);
  const a = list.add(id, label, text, style);
  if (a) check(a, text, m);
  return a;
}

function confirm(a: ViewAction, title: string, text: string, ok: string): void {
  a.confirm = { title, text, ok_label: ok };
}

// -------------------------------------------------------------------- tile

/** A tile's tag: text, tone and the sentence that explains it. */
type Tag = NonNullable<ViewTile['tag']>;

/** A tile's own state word, and the tag that goes with it.
 *
 *  State, in the order a fault or a motion in progress outranks a plain
 *  material reading: error > runout > loading/unloading > positioning > the
 *  raw slot state. */
function tileStateAndTag(m: Model, unitIdx: number, bay: number): { state: ViewTile['state']; tag: Tag | null } {
  const u = m.units[unitIdx];
  const s = u.slots[bay];
  const th = u.toolhead >= 0 && u.toolhead < m.toolheads.length ? m.toolheads[u.toolhead] : undefined;
  const mkTag = (text: string, tone: Tone): Tag => ({ text, tone, detail: '', code: '' });
  let state: ViewTile['state'];
  let tag: Tag | null = null;

  if (s.state === SlotState.ERROR) {
    state = 'error';
    tag = mkTag(str('TAG_ERROR'), 'error');
  } else if (th && th.runoutActive && th.runoutFromUnit === unitIdx && th.runoutFromSlot === bay) {
    state = 'runout';
    tag = mkTag(str('TAG_RUNOUT'), 'info');
  } else if (th && th.busy !== Busy.NONE && th.busyUnit === unitIdx && th.busySlot === bay) {
    const unloading = th.busy === Busy.UNLOAD;
    state = unloading ? 'unloading' : 'loading';
    tag = mkTag(str(unloading ? 'TAG_UNLOADING' : 'TAG_LOADING'), 'info');
  } else if (s.positioning) {
    state = 'positioning';
    tag = mkTag(str('TAG_POSITIONING'), 'info');
  } else {
    const empty = s.state === SlotState.EMPTY;
    const unknown = !empty && s.infoSource === InfoSource.UNKNOWN;
    // Loaded outranks "unknown": something is actively feeding from this bay,
    // which matters more than whether its material was ever identified.
    if (s.state === SlotState.LOADED) state = 'loaded';
    else if (empty) { state = 'empty'; tag = mkTag(str('TAG_NOT_INSERTED'), 'neutral'); }
    else if (unknown) state = 'unknown';
    else state = 'ready';
  }

  // Nothing more urgent to say and a spool waits for the user's confirmation.
  if (!tag && s.pendingConfirmation) tag = mkTag(str('TAG_CONFIRM'), 'info');

  if (tag) {
    const detailId = {
      error: 'TAGD_ERROR', runout: 'TAGD_RUNOUT', loading: 'TAGD_LOADING', unloading: 'TAGD_UNLOADING',
      positioning: 'TAGD_POSITIONING', empty: 'TAGD_NOT_INSERTED',
    }[state as string] ?? 'TAGD_CONFIRM';
    tag.detail = str(detailId as 'TAGD_CONFIRM');   // a table string, borrowed in C
    // An error on the bay the lane fault names says what the host said, and
    // carries the host's code as its own field.
    if (state === 'error' && th && th.hasError && th.errorUnit === unitIdx && th.errorSlot === bay) {
      tag.detail = th.errorText || th.errorCode;     // the host's own words, borrowed in C
      tag.code = cut(th.errorCode, ALERT_CODE);
    }
  }
  return { state, tag };
}

/** A bay nobody has identified: ready, but nothing says what is in it, which is
 *  what the tile's own state calls "unknown" (the tile labelled "?"). Loading
 *  it asks for the spool first, so what reaches the host is an answer the user
 *  chose (PRINCIPLES.md 13). */
function bayIsUnknown(m: Model, unitIdx: number, bay: number): boolean {
  return tileStateAndTag(m, unitIdx, bay).state === 'unknown';
}

/** The tile wears the "Confirm" tag: a spool the host linked waits for the
 *  user's word, and nothing more urgent is on the tag (a fault, a runout, a
 *  motion, an empty bay) - tileStateAndTag()'s own rule (bay_awaits_confirm()
 *  in view.c). */
function bayAwaitsConfirm(m: Model, unitIdx: number, bay: number): boolean {
  if (!m.units[unitIdx].slots[bay].pendingConfirmation) return false;
  const { state } = tileStateAndTag(m, unitIdx, bay);
  return state === 'loaded' || state === 'ready' || state === 'unknown';
}

function buildTile(m: Model, unitIdx: number, bay: number): ViewTile {
  const u = m.units[unitIdx];
  const s = u.slots[bay];
  const gi = slotGroup(m, unitIdx, bay);

  // The label is the tool, or "?" for a bay in no group; the sublabel is always
  // the bay's "Spool N".
  const tool = gi >= 0 ? cut(m.groups[gi].name, LABEL) : null;
  const label = tool ?? str('UNKNOWN_MARK');
  const spare = gi >= 0 ? computeSpare(m, gi, unitIdx, bay) : null;

  const { state, tag } = tileStateAndTag(m, unitIdx, bay);

  // One disabled look: an empty or unidentified bay, or a unit that is offline.
  const dim = state === 'empty' || state === 'unknown' || !u.connected;
  // The hatched look marks "nothing to use here": not inserted, or disabled; a
  // real spool (black, unknown, error, ...) is never hatched.
  const hatched = state === 'empty' || !u.connected;

  // No known color: no color, no gradient, and the ink of the neutral surface the
  // renderer draws instead (PRINCIPLES 1). A known 0x000000 is black.
  const ink = s.colorKnown ? inkFor(s.color) : 'light';
  const gradient = s.colorKnown ? gradientFor(s.color, ink) : null;

  // "name" has no field of its own yet: it mirrors the material. An unknown
  // one is "": only the ring shows "?".
  const material = cut(s.state === SlotState.EMPTY ? '' : s.material, MATERIAL);

  return {
    slot_id: cut(slotId(m, unitIdx, bay), ID),
    bay,
    tool,
    label: cut(label, LABEL),
    sublabel: fmtSpoolLabel(bay, SUBLABEL),
    spare,
    state,
    tag,
    color: s.colorKnown ? hex(s.color) : null,
    ink,
    gradient_top: gradient ? hex(gradient[0]) : null,
    gradient_bottom: gradient ? hex(gradient[1]) : null,
    color_name: s.colorKnown ? colorName(s.color) : '',
    dim,
    hatched,
    name: material,
    material,
    brand: cut(s.brand, MATERIAL),
    grams_text: fmtGrams(s.remainingG, GRAMS),
    pct: s.remainingPct,
    pct_text: fmtPct(s.remainingPct, PCT),
    low: s.remainingPct >= 0 && s.remainingPct < 15,
    rfid: s.infoSource === InfoSource.TAG,
    pending_confirmation: s.pendingConfirmation,
    calibrated: s.calState === CalState.CONFIGURED,
    actions: tileActions(m, unitIdx, bay),
  };
}

// The colors the spool editor offers by name: the same 18 the display's palette
// sheet draws (src/view/view.c k_palette). The nearest entry names any other
// color, so a tag read or an off-shade still gets a sensible name.
const PALETTE: [number, Parameters<typeof str>[0]][] = [
  [0xffffff, 'COLOR_WHITE'], [0xa6a9aa, 'COLOR_SILVER'], [0x5f6368, 'COLOR_GRAY'], [0x000000, 'COLOR_BLACK'],
  [0x9d432c, 'COLOR_BROWN'], [0xe4bd68, 'COLOR_GOLD'], [0xc12e1f, 'COLOR_RED'], [0x9d2235, 'COLOR_MAROON'],
  [0xff6a13, 'COLOR_ORANGE'], [0xf4ee2a, 'COLOR_YELLOW'], [0x00ae42, 'COLOR_GREEN'],
  [0x1f5c3a, 'COLOR_FOREST_GREEN'], [0x00b1b7, 'COLOR_TURQUOISE'], [0x0086d6, 'COLOR_CYAN'],
  [0x0a2989, 'COLOR_BLUE'], [0x5e43b7, 'COLOR_PURPLE'], [0xec008c, 'COLOR_MAGENTA'], [0xf55a74, 'COLOR_PINK'],
];

/** The colors the editor offers by name, in order. */
export const paletteCount = (): number => PALETTE.length;
export const paletteColor = (i: number): number => PALETTE[i]?.[0] ?? 0;
export const paletteName = (i: number): string => (PALETTE[i] ? str(PALETTE[i][1]) : '');

/** The palette name nearest to `rgb` (squared RGB distance). */
export function colorName(rgb: number): string {
  let best = 0;
  let bestD = -1;
  const r = (rgb >> 16) & 0xff;
  const g = (rgb >> 8) & 0xff;
  const b = rgb & 0xff;
  PALETTE.forEach(([c], i) => {
    const d = (r - ((c >> 16) & 0xff)) ** 2 + (g - ((c >> 8) & 0xff)) ** 2 + (b - (c & 0xff)) ** 2;
    if (bestD < 0 || d < bestD) {
      bestD = d;
      best = i;
    }
  });
  return paletteName(best);
}

/** A number as the C prints it (the form fields are doubles), shown as cJSON shows a double. */
const fnum = (x: number): number => cjsonNumber(x);

/** "Edit spool": material, color, vendor, both weights and the pressure advance
 *  of the bay's spool, each starting from what the model knows (or a default,
 *  editDefaults). Two actions ask for them - "Edit spool", and "Load" on a bay
 *  nobody has identified - so they are built once here and both take them
 *  whole (PRINCIPLES.md 13: one task, one place to do it).
 *
 *  `chooseMaterial` puts the one choice only the user can make at the head of
 *  the material list: the line spells it -1, the mapper refuses that until the
 *  field has been answered, and the field says so. */
function editFields(m: Model, unitIdx: number, bay: number, chooseMaterial: boolean): ViewField[] {
  const s = m.units[unitIdx].slots[bay];
  const d = editDefaults(m, unitIdx, bay);
  const fields: ViewField[] = [];
  const pool: number[] = [];   // the options taken so far: the shared pool's size
  const select = (
    fid: string, label: string, value: number, required: boolean,
    options: { value: number; label: string }[],
  ): void => {
    const kept: { value: number; label: string }[] = [];
    for (const o of options) {
      if (pool.length >= MAX_OPTIONS) break;
      pool.push(o.value);
      kept.push({ value: o.value, label: cut(o.label, OPTION_LABEL) });
    }
    fields.push({
      id: fid, label, kind: 'select', value: fnum(value), min: 0, max: 0, step: 0, unit: '',
      options: kept, required,
    });
  };
  const number = (fid: string, label: string, value: number, min: number, max: number, step: number, unit: string): void => {
    fields.push({
      id: fid, label, kind: 'number', value: fnum(value), min: fnum(min), max: fnum(max),
      step: fnum(step), unit, required: false,
    });
  };

  select('material', str('EDIT_MATERIAL'), chooseMaterial ? -1 : d.material, chooseMaterial, [
    ...(chooseMaterial ? [{ value: -1, label: str('MATERIAL_CHOOSE') }] : []),
    ...MATERIALS.map((name, i) => ({ value: i, label: name })),
    ...(d.material === MATERIALS.length ? [{ value: MATERIALS.length, label: s.material }] : []),
  ]);

  const colors = [
    ...(d.color < 0 ? [{ value: -1, label: str('EDIT_COLOR_UNSET') }] : []),
    ...PALETTE.map(([rgb], i) => ({ value: rgb, label: paletteName(i) })),
  ];
  if (d.color >= 0 && !PALETTE.some(([rgb]) => rgb === d.color)) {
    colors.push({ value: d.color, label: `#${d.color.toString(16).toUpperCase().padStart(6, '0')}` });
  }
  select('color', str('EDIT_COLOR'), d.color, false, colors);

  const n = editVendorCount(m);
  const vendors = Array.from({ length: n }, (_, i) => ({ value: i, label: editVendorName(m, i) }));
  if (d.vendor === n) vendors.push({ value: n, label: s.brand });
  select('vendor', str('EDIT_VENDOR'), d.vendor, false, vendors);

  number('remaining', str('FIELD_REMAINING_LABEL'), d.remainingG, 0, 10000, 1, str('FIELD_WEIGHT_UNIT'));
  number('initial', str('FIELD_INITIAL_LABEL'), d.initialG, 1, 10000, 1, str('FIELD_WEIGHT_UNIT'));
  number('pa', str('EDIT_PA'), d.paX1000 / 1000, 0, 2, 0.001, '');
  return fields;
}

/** The edit line with every field at what the form starts from: the predicate's
 *  dry run of the action (it never reads as a change, see actionsCheck). */
function editResolved(id: string, m: Model, unitIdx: number, bay: number,
                      material: number | null, thenLoad: boolean): string {
  const d = editDefaults(m, unitIdx, bay);
  const pa = `${Math.trunc(d.paX1000 / 1000)}.${String(d.paX1000 % 1000).padStart(3, '0')}`;

  return cut(
    `spool edit ${id} material=${material ?? d.material} color=${d.color} `
    + `vendor=${d.vendor} remaining=${d.remainingG} initial=${d.initialG} pa=${pa}`
    + (thenLoad ? ' then load' : ''),
    ACTION_LINE,
  );
}

/** "Edit spool": the editor's fields on the bay's own spool. The one line
 *  carries every field; the mapper sends only the ones the user changed
 *  (actions.ts, `spool edit`). Dimmed, with its reason, while the host cannot
 *  take it: no edit endpoint, Spoolman offline or an empty bay. */
function addEditAction(list: ActionList, m: Model, unitIdx: number, bay: number, id: string): void {
  const a = list.add(
    'edit_spool', str('ACTION_EDIT_SPOOL'),
    `spool edit ${id} material={material} color={color} vendor={vendor} remaining={remaining} initial={initial} pa={pa}`,
    'normal',
  );
  if (!a) return;
  /* The one form with a submit button: the row that sends it says "Save edit"
   * (the action it is here for is "Edit spool"), and a submission that changed
   * nothing is the host's own refusal to answer. The other forms carry neither
   * and their renderers behave as before. */
  a.form = { fields: editFields(m, unitIdx, bay, false), submit_label: str('ACTION_SAVE_EDIT'), require_change: true };
  check(a, editResolved(id, m, unitIdx, bay, null, false), m);
}

/** "Load" on a bay nobody has identified: the same spool editor, one choice
 *  further. The form asks who is before it asks for anything else - Klipper's
 *  preload table is worth nothing to a bay whose material is still "?" - and
 *  the line that leaves it saves that answer and loads in one go, so the load
 *  the user came for happens instead of being handed back to them. */
function addSaveAndLoadAction(list: ActionList, m: Model, unitIdx: number, bay: number, id: string): void {
  const a = list.add(
    'load', str('ACTION_LOAD'),
    'spool edit ' + id
      + ' material={material} color={color} vendor={vendor} remaining={remaining} initial={initial} pa={pa} then load',
    'normal',
  );
  if (!a) return;
  /* The row that sends it says what it will do, not what it is called: a
   * "Load" row whose dialog offers "Save and load". */
  a.form = { fields: editFields(m, unitIdx, bay, true), submit_label: str('SAVE_AND_LOAD'), require_change: true };
  // The row is dimmed only while the edit cannot be taken at all (no endpoint,
  // Spoolman offline), with the reason Edit spool gives. The unanswered material
  // is not that: it is the dialog's own required field, and dimming the row for
  // it would hide the dialog that asks. So the dry run is the editor's, with a
  // material that is an answer.
  check(a, editResolved(id, m, unitIdx, bay, editDefaults(m, unitIdx, bay).material === 0 ? 1 : 0, true), m);
  // ... and then the load itself has its own rules (a bay in no tool, a busy
  // lane): what would refuse the plain Load refuses this one, in its words, so
  // no spool is saved for a load that cannot follow it
  if (a.enabled) check(a, `load ${id}`, m);
}

/** "Confirm spool", the "Confirm" tag's own action (UNIFIED_UI.md 7,
 *  SpoolConfirm): how much is left on the spool the host linked, starting from
 *  a full one (its initial weight, the device's "Yes, full") and adjustable
 *  before it is sent as `spool confirm <slot> <grams>`. Enabled and reason are
 *  the one predicate's. */
function addConfirmAction(list: ActionList, m: Model, unitIdx: number, bay: number, id: string): void {
  const initial = editDefaults(m, unitIdx, bay).initialG;
  const a = list.add('confirm_spool', str('ACTION_CONFIRM_SPOOL'), `spool confirm ${id} {grams}`, 'primary');
  if (!a) return;
  a.target = 'tag';
  a.form = {
    fields: [{
      id: 'grams', label: str('FIELD_REMAINING_LABEL'), kind: 'number', value: fnum(initial), min: 0,
      max: 10000, step: 1, unit: str('FIELD_WEIGHT_UNIT'), required: false,
    }],
    // The button says what it does; confirming the full spool as it stands is
    // the common answer, so the form asks for no change.
    submit_label: str('BTN_CONFIRM'), require_change: false,
  };
  check(a, cut(`spool confirm ${id} ${initial}`, ACTION_LINE), m);
}

/** The choice the form opens on: the bay's own group, so submitting it
 *  unchanged moves nothing. A bay in no group has none to name, and "No group"
 *  is the one choice the mapper refuses on it - there is nothing to unassign -
 *  so it takes the first group of its lane that is not the one loaded in the
 *  toolhead, or "New group" when the lane holds none. "No group" and the loaded
 *  group stay in the list: a user who picks them hears the host's own refusal. */
function groupDefaultChoice(m: Model, unitIdx: number, bay: number): number {
  const gi = slotGroup(m, unitIdx, bay);
  if (gi >= 0) return gi;
  for (let g = 0; g < m.groups.length; g++) {
    if (!groupOnUnitLane(m, unitIdx, g)) continue;
    if (m.loaded && g === m.currentGroup) continue;
    return g;
  }
  return GROUP_CHOICE_NEW;
}

/** The "Change group..." list of a bay: every group on this unit's lane, by
 *  name, then "No group" and "New group" (the next free "T<n>"). The default is
 *  the bay's own group, so opening the form and submitting it unchanged moves
 *  nothing. The line carries the choice as its value, and the mapper turns each
 *  choice into the host command(s) it means. */
function addChangeGroupAction(list: ActionList, m: Model, unitIdx: number, bay: number, id: string): void {
  const choice = groupDefaultChoice(m, unitIdx, bay);
  const options: { value: number; label: string }[] = [];
  for (let g = 0; g < m.groups.length; g++) {
    if (!groupOnUnitLane(m, unitIdx, g)) continue;
    options.push({ value: g, label: copy(m.groups[g].name, OPTION_LABEL) });
  }
  options.push({ value: GROUP_CHOICE_NONE, label: str('MAP_NO_GROUP') });
  options.push({ value: GROUP_CHOICE_NEW, label: str('GP_NEW') });

  const a = list.add('change_group', str('HS_CHANGE_GROUP'), cut(`change group {group} ${id}`, ACTION_LINE), 'normal');
  if (!a) return;
  a.form = {
    fields: [{
      id: 'group', label: str('FIELD_GROUP_LABEL'), kind: 'select',
      value: cjsonNumber(choice), min: 0, max: 0, step: 0, unit: '', options, required: false,
    }],
    submit_label: str('ACTION_CHANGE_GROUP'), require_change: true,
  };
  // Dry-run the line the form opens on, not the bay's own group alone: the
  // destination has rules of its own (the group loaded in the toolhead), so
  // this is what the one predicate says about the change as it stands - one
  // string per refusal rule, dimmed and never hidden (PRINCIPLES.md 1).
  check(a, cut(`change group ${choice} ${id}`, ACTION_LINE), m);
}

/** The group's own actions: the "Delete group", dimmed with the group's own
 *  reason when the host would refuse it. */
function groupActions(m: Model, gi: number): ViewAction[] {
  const list = new ActionList();
  simple(list, m, 'delete_group', str('ACTION_DELETE_GROUP'), 'danger', `delete group ${m.groups[gi].name}`);
  return list.items;
}

/** The tile's actions, a fixed set in a fixed order; enabled and reason come
 *  from the one predicate, which dims what does not apply now. */
function tileActions(m: Model, unitIdx: number, bay: number): ViewAction[] {
  const u = m.units[unitIdx];
  const s = u.slots[bay];
  const id = cut(slotId(m, unitIdx, bay), ID);
  const list = new ActionList();

  // A bay nobody has identified asks for its spool before it is loaded.
  if (bayIsUnknown(m, unitIdx, bay)) addSaveAndLoadAction(list, m, unitIdx, bay, id);
  else simple(list, m, 'load', str('ACTION_LOAD'), 'normal', `load ${id}`);
  // The bay's own unload line: the mapper resolves it to the unit's lane, and
  // only a loaded bay passes its predicate.
  const unload = simple(list, m, 'unload', str('ACTION_UNLOAD'), 'normal', `unload ${id}`);
  if (unload && m.printing) confirm(unload, str('CONFIRM_UNLOAD_TITLE'), str('CONFIRM_UNLOAD_TEXT'), str('CONFIRM_UNLOAD_OK'));

  addEditAction(list, m, unitIdx, bay, id);

  // Assign a Spoolman spool from the list, or refresh the list first.
  if (m.spools.some((sp) => sp.id !== 0)) {
    const line = cut(`link ${id} {spool}`, ACTION_LINE);
    const a = list.add('assign_spool', str('ACTION_ASSIGN_SPOOL'), line, 'normal');
    if (a) {
      const options: ViewFieldOption[] = [];
      let value = 0;
      for (const sp of m.spools) {
        if (options.length >= MAX_OPTIONS) break;
        if (sp.id === 0) continue;
        options.push({ value: sp.id, label: fmtSpoolOption(sp.vendor, sp.material, sp.remainingG, OPTION_LABEL), color: sp.colorKnown ? hex(sp.color) : null });
        // The bay's current link (else the first spool) is the default.
        if (options.length === 1 || sp.id === s.spoolId) value = sp.id;
      }
      a.form = {
        fields: [{
          id: 'spool', label: str('FIELD_SPOOL_LABEL'), kind: 'select', value: cjsonNumber(value), min: 0, max: 0,
          step: 0, unit: '', options, required: false,
        }],
        submit_label: null, require_change: false,
      };
      check(a, cut(`link ${id} ${Math.trunc(value)}`, ACTION_LINE), m);
    }
  } else {
    simple(list, m, 'spool_list_refresh', str('ACTION_REFRESH_SPOOLS'), 'normal', 'spool list');
  }

  simple(list, m, 'unlink', str('ACTION_UNLINK'), 'normal', `unlink ${id}`);
  addChangeGroupAction(list, m, unitIdx, bay, id);
  // A unit with no reader can never re-read a tag: no such action.
  if (u.rfid) simple(list, m, 'reread', str('ACTION_REREAD'), 'normal', `reread ${id}`);
  const cal = simple(list, m, 'calibrate', str('ACTION_CALIBRATE'), 'normal', `calibrate ${id}`);
  if (cal) confirm(cal, str('CONFIRM_CALIBRATE_TITLE'), str('CONFIRM_CALIBRATE_TEXT'), str('CONFIRM_CALIBRATE_OK'));

  // Load and unload act on the tile body; the rest edit the spool (the ring).
  for (const a of list.items) a.target = a.id === 'load' || a.id === 'unload' ? 'tile' : 'ring';

  // The "Confirm" tag's details carry the confirmation itself: the tag says a
  // spool waits for the user, and its popover is where they answer.
  if (bayAwaitsConfirm(m, unitIdx, bay)) addConfirmAction(list, m, unitIdx, bay, id);

  // The tag's details: the lane fault that names this unit and bay is shown by
  // this tile, so its recovery actions live here, under the tag.
  const th = u.toolhead >= 0 && u.toolhead < m.toolheads.length ? m.toolheads[u.toolhead] : undefined;
  if (th && th.hasError && th.errorUnit === unitIdx && th.errorSlot === bay) {
    for (const fa of faultActions(m, th)) {
      if (list.items.length >= MAX_ACTIONS) break;
      list.items.push({ ...fa, target: 'tag' });
    }
  }
  return list.items;
}

// -------------------------------------------------------------------- unit

/** The defaults of the drying form: the unit's own last target if it has one,
 *  else 55 C, clamped to [45, the variant's ceiling]; 12 hours, or the time
 *  actually remaining when a cycle is already running. */
function dryDefaults(u: Unit): { target: number; hours: number } {
  const tmax = u.variant === Variant.AMS_HT ? 85 : 65;
  let t = u.dryTargetC >= 45 ? u.dryTargetC : 55;
  if (t > tmax) t = tmax;
  if (t < 45) t = 45;
  let hours = 12;
  if (u.dryState !== DryState.IDLE && u.dryRemainingMin > 0) {
    hours = Math.min(24, Math.max(1, Math.trunc((u.dryRemainingMin + 59) / 60)));
  }
  return { target: t, hours };
}

function unitActions(m: Model, unitIdx: number): ViewAction[] {
  const u = m.units[unitIdx];
  const list = new ActionList();
  if (!u.canDry) return list.items;

  const { target, hours } = dryDefaults(u);
  const tmax = u.variant === Variant.AMS_HT ? 85 : 65;
  const a = list.add('dry_start', str('ACTION_DRY_START'), `dry ${u.name} start {target}C {hours}h`, 'normal');
  if (a) {
    a.form = {
      fields: [
        { id: 'target', label: str('FIELD_TARGET_LABEL'), kind: 'number', value: target, min: 45, max: tmax,
          step: 5, unit: str('FIELD_TARGET_UNIT'), required: false },
        { id: 'hours', label: str('FIELD_HOURS_LABEL'), kind: 'number', value: hours, min: 1, max: 24,
          step: 1, unit: str('FIELD_HOURS_UNIT'), required: false },
      ],
      submit_label: null, require_change: false,
    };
    if (dryReason(u)) {
      a.enabled = false;
      a.reason = dryReason(u);   // the host's own words, borrowed in C
    } else {
      check(a, cut(`dry ${u.name} start ${target}C ${hours}h`, ACTION_LINE), m);
    }
  }
  simple(list, m, 'dry_stop', str('ACTION_DRY_STOP'), 'normal', `dry ${u.name} stop`);
  return list.items;
}

const DRYER_TONE = (st: number): 'heat' | 'cool' | 'fault' | 'off' =>
  st === DryState.HEATING || st === DryState.DEHUMIDIFYING ? 'heat'
    : st === DryState.COOLING ? 'cool' : st === DryState.ERROR ? 'fault' : 'off';

const VARIANT_NAMES = ['AMS', 'AMS 2 Pro', 'AMS HT', 'Lane MMU'];
const variantName = (v: number): string => VARIANT_NAMES[v] ?? 'AMS';

// The one place a unit's title comes from: the product name the host's family
// named the unit ("BoxTurtle"), or its variant's name when the family carries
// none.
const unitTitle = (u: Unit): string => u.title || variantName(u.variant);

function buildUnit(m: Model, unitIdx: number): ViewUnit {
  const u = m.units[unitIdx];
  const unitAlert = scanAlerts(m, unitIdx, -1);

  // A missing source is unknown, never a possibly wrong value: an offline unit
  // shows "Offline" in place of its reading, and has no dryer pill.
  const env = u.connected && (u.hasHumidityPct || u.hasTemp)
    ? { text: fmtEnv(u.hasHumidityPct, u.humidityPct, u.hasTemp, u.tempC, ENV_TEXT) } : null;

  let dryer: ViewUnit['dryer'] = null;
  if (u.canDry && u.connected) {
    const tone = DRYER_TONE(u.dryState);
    // Only a host that reported the supply can say it is missing: the AMS HT
    // has no monitor, so its adapter is unknown, not absent.
    let text = '';
    if (tone === 'heat') text = fmtDryerActive(u.dryTargetC, u.dryRemainingMin, DRYER_TEXT);
    else if (tone === 'cool') text = copy(str('DRYER_COOLING'), DRYER_TEXT);
    // A dryer fault is an item of the unit's alert, not a pill.
    dryer = { text, tone, adapter_missing: u.hasPowerAdapter && !u.powerAdapter };
  }

  return {
    id: cut(u.name, UNIT_ID),
    title: unitTitle(u),
    subtitle: cut(u.name, UNIT_ID),
    online: u.connected,
    status_text: u.connected ? '' : str('UNIT_OFFLINE'),
    env,
    dryer,
    alert: unitAlert,
    alert_clear: unitScopeClear(m, unitIdx, unitAlert),
    // serial and firmware: no real-host field carries these yet, so they are
    // left blank rather than invented.
    info: { serial: '', firmware: '', family: unitTitle(u) },
    actions: unitActions(m, unitIdx),
    bays: u.slots.map((_, bay) => buildTile(m, unitIdx, bay)),
    groups: buildGroups(m, unitIdx),
    ungrouped_bays: ungroupedBays(m, unitIdx),
  };
}

// ------------------------------------------------------------------ groups

/** The groups that hold a bay of `unitIdx`, in the model's own order. */
function groupsForUnit(m: Model, unitIdx: number): number[] {
  const out: number[] = [];
  for (let g = 0; g < m.groups.length; g++) {
    if (m.groups[g].members.some((mem) => mem.unit === unitIdx)) out.push(g);
  }
  return out;
}

/** One member row: this unit's bay named like its tile and carrying its color,
 *  material and state, another unit's named with its unit and dimmed with no
 *  state - a backup this unit may fall back to, never a thing to manage here. */
function buildGroupMember(m: Model, unitIdx: number, unit: number, slot: number, spare: boolean): ViewGroupMember {
  const mine = unit === unitIdx;
  const empty: ViewGroupMember = {
    slot_id: '', bay: slot, mine, dim: !mine, spare, label: '',
    color: null, material: '', brand: '', state: '', state_tone: 'neutral',
  };
  const u = m.units[unit];
  if (!u || slot < 0 || slot >= u.slots.length) return empty;

  empty.slot_id = slotId(m, unit, slot);
  empty.label = mine
    ? fmtSpoolLabel(slot, SUBLABEL)
    : fmtUnitSpool(u.name, slot, SUBLABEL);
  const tile = buildTile(m, unit, slot);
  empty.color = tile.color;

  empty.material = copy(tile.material, MATERIAL);
  empty.brand = copy(tile.brand, MATERIAL);
  switch (tile.state) {
    case 'loaded':
      empty.state = str('GROUP_LOADED');
      empty.state_tone = 'neutral';
      break;
    case 'error':
      empty.state = str('GROUP_ERROR');
      empty.state_tone = 'error';
      break;
    case 'empty':
      empty.state = str('GROUP_EMPTY');
      empty.state_tone = 'neutral';
      break;
    default:
      empty.state = str('GROUP_READY');
      empty.state_tone = 'neutral';
      break;
  }
  return empty;
}

function buildGroups(m: Model, unitIdx: number): ViewGroup[] {
  return groupsForUnit(m, unitIdx).map((gi) => {
    const g = m.groups[gi];
    const spare = groupNextSpare(m, gi);
    const reason = groupEditReason(m, gi);
    return {
      name: copy(g.name, LABEL),
      editable: !reason,
      reason,
      members: g.members.map((mem) =>
        buildGroupMember(m, unitIdx, mem.unit, mem.slot, !!spare && spare.unit === mem.unit && spare.slot === mem.slot)),
      actions: groupActions(m, gi),
    };
  });
}

/** This unit's bays that belong to no group, in bay order. */
function ungroupedBays(m: Model, unitIdx: number): ViewGroupMember[] {
  const u = m.units[unitIdx];
  const out: ViewGroupMember[] = [];
  if (!u) return out;
  for (let bay = 0; bay < u.slots.length; bay++) {
    if (slotGroup(m, unitIdx, bay) >= 0) continue;
    out.push(buildGroupMember(m, unitIdx, unitIdx, bay, false));
  }
  return out;
}

// ------------------------------------------------------------------ alerts

const SEVERITY: Severity[] = ['info', 'pause', 'stop'];

const alertTitle = (sev: number): string =>
  sev === Sev.STOP ? str('ALERT_TITLE_STOP') : sev === Sev.PAUSE ? str('ALERT_TITLE_PAUSE') : str('ALERT_TITLE_INFO');

const faultWeight = (th: Toolhead): number => (th.errorCount > 0 ? th.errorCount : 1);

/** Whether the lane fault has any action to offer (without building them). */
function faultHasActions(m: Model, th: Toolhead): boolean {
  return th.errorAct.some((tok) => {
    if (tok === 'resume' || tok === 'clear_errors') return true;
    if (tok === 'clear_fault') return th.errorUnit >= 0;
    if (tok === 'retry_load') {
      return (th.currentGroup >= 0 && th.currentGroup < m.groups.length)
        || (th.errorUnit >= 0 && slotGroup(m, th.errorUnit, th.errorSlot) >= 0);
    }
    return false;
  });
}

/** The actions a lane fault's support tokens offer, in the host's order (the
 *  fault's first two tokens): retry, resume, clear errors, and the unit-scoped
 *  clear fault when the fault names a unit. */
function faultActions(m: Model, th: Toolhead): ViewAction[] {
  const list = new ActionList();
  for (const tok of th.errorAct) {
    if (tok === 'retry_load') {
      let group: string | null = null;
      if (th.currentGroup >= 0 && th.currentGroup < m.groups.length) group = m.groups[th.currentGroup].name;
      else if (th.errorUnit >= 0) {
        const gi = slotGroup(m, th.errorUnit, th.errorSlot);
        if (gi >= 0) group = m.groups[gi].name;
      }
      if (group === null) continue;
      simple(list, m, 'retry_load', str('ACTION_RETRY_LOAD'), 'primary', `fault retry_load ${group}`);
    } else if (tok === 'resume') {
      simple(list, m, 'resume', str('ACTION_RESUME'), 'primary', 'fault resume');
    } else if (tok === 'clear_errors') {
      simple(list, m, 'clear_errors', str('ACTION_CLEAR_ERRORS'), 'danger', 'fault clear_errors');
    } else if (tok === 'clear_fault' && th.errorUnit >= 0 && th.errorUnit < m.units.length) {
      const a = simple(list, m, 'clear_fault', str('ACTION_CLEAR_FAULT'), 'danger', `fault clear_fault ${m.units[th.errorUnit].name}`);
      if (a) confirm(a, str('CONFIRM_CLEAR_FAULT_TITLE'), str('CONFIRM_CLEAR_FAULT_TEXT'), str('CONFIRM_CLEAR_FAULT_OK'));
    }
  }
  return list.items;
}

/** One lane fault as an alert item, with the actions its tokens offer. */
function faultItem(m: Model, th: Toolhead): ViewAlert {
  let unit: string | null = null;
  let slot: string | null = null;
  if (th.errorUnit >= 0 && th.errorUnit < m.units.length) {
    unit = cut(m.units[th.errorUnit].name, ALERT_UNIT);
    if (th.errorSlot >= 0) slot = cut(slotId(m, th.errorUnit, th.errorSlot), ALERT_SLOT);
  }
  return {
    severity: SEVERITY[th.errorSeverity],
    code: cut(th.errorCode, ALERT_CODE),
    title: alertTitle(th.errorSeverity),
    text: th.errorText || th.errorCode,   // borrowed in C
    unit,
    slot_id: slot,
    when_text: '',
    unread: true,
    actions: faultActions(m, th).slice(0, MAX_ACTIONS_PER_ALERT),
  };
}

/** An alert's sentence: the host's own words as they are, or - for the two the
 *  core raises itself, kept as numbers - the current language's template
 *  filled in (alert_text() in view.c). */
function alertText(m: Model, a: Alert): string {
  if (a.kind === AlertKind.LOW_FILAMENT) {
    const name = a.unit >= 0 && a.unit < m.units.length ? m.units[a.unit].name : '';
    return fmtLowFilament(fmtUnitSpool(name, a.arg[0], SPOOL_NAME), a.arg[1], ALERT_TEXT);
  }
  if (a.kind === AlertKind.SHORTFALL) return fmtShortfall(a.arg[0], a.arg[1], a.arg[2], ALERT_TEXT);
  return a.text;
}

/** An alert-history entry as an item (it carries no actions of its own). */
function historyItem(m: Model, a: Alert): ViewAlert {
  return {
    severity: SEVERITY[a.severity],
    code: '',
    title: alertTitle(a.severity),
    text: alertText(m, a),
    unit: a.unit >= 0 && a.unit < m.units.length ? cut(m.units[a.unit].name, ALERT_UNIT) : null,
    slot_id: null,
    when_text: '',
    unread: a.unread,
    actions: [],
  };
}

/**
 * The alert group of one unit (`unitIdx` >= 0) or one toolhead: its count,
 * worst severity, tone and items, or null when it has none. Each alert lives on
 * exactly one level (UNIFIED_UI 4b), and a badge lists only what is not already
 * shown elsewhere, or carries actions (4c):
 * - the live lane fault: the unit it names, else its toolhead; not when it
 *   names a unit and a bay (that tile shows it, with the fault's actions), and
 *   not when it offers no action and is the message row's own text;
 * - the dryer fault and the missing power adapter: the unit;
 * - a toolhead's history entry (the job shortfall): unless the message row
 *   already shows the same text. Low filament (a unit's entry) is the tile's
 *   low ring, and a lane fault's own history entry is never shown again.
 */
/** Whether nothing in a unit's scope alerts, shown or not (unit_scope_clear() in view.c). */
function unitScopeClear(m: Model, unitIdx: number, group: ViewAlertGroup | null): boolean {
  const u = m.units[unitIdx];
  const th = m.toolheads[u.toolhead];
  if (group) return false;
  if (th && th.hasError && th.errorUnit === unitIdx) return false;
  if (th && th.runoutActive && th.runoutFromUnit === unitIdx) return false;
  if (u.slots.some((s) => s.state === SlotState.ERROR)) return false;
  return !m.alerts.some((a) => !a.laneFault && a.unread && a.unit === unitIdx && !alertBadgeExpired(m, a));
}

/** The same for a toolhead (toolhead_scope_clear() in view.c). */
function toolheadScopeClear(m: Model, thIdx: number, th: Toolhead, group: ViewAlertGroup | null,
  message: ViewToolhead['message']): boolean {
  if (group || th.hasError || th.runoutActive) return false;
  if (message.tone !== 'neutral') return false;
  return !m.alerts.some((a) => !a.laneFault && a.unread && a.unit < 0 && a.toolhead === thIdx
    && !alertBadgeExpired(m, a));
}

function scanAlerts(m: Model, unitIdx: number, thIdx: number): ViewAlertGroup | null {
  const u = unitIdx >= 0 ? m.units[unitIdx] : undefined;
  const th = u ? m.toolheads[u.toolhead] : m.toolheads[thIdx];
  let count = 0;
  let sev: number = Sev.INFO;
  const items: ViewAlert[] = [];
  const add = (weight: number, severity: number, item: () => ViewAlert): void => {
    if (items.length < MAX_ALERTS_PER_GROUP) items.push(item());
    count += weight;
    if (severity > sev) sev = severity;
  };

  if (th && th.hasError && (unitIdx >= 0 ? th.errorUnit === unitIdx : th.errorUnit < 0)
    && !(th.errorUnit >= 0 && th.errorSlot >= 0)) {
    // No action: only listed when the message row shows something else.
    if (faultHasActions(m, th) || buildMessage(m, th).text !== (th.errorText || th.errorCode)) {
      add(faultWeight(th), th.errorSeverity, () => faultItem(m, th));
    }
  }
  if (u && u.canDry && u.dryState === DryState.ERROR) {
    add(1, Sev.PAUSE, () => ({
      severity: 'pause', code: '', title: alertTitle(Sev.PAUSE), text: str('ALERT_DRYER'),
      unit: cut(u.name, ALERT_UNIT), slot_id: null, when_text: '', unread: false, actions: [],
    }));
  }
  if (u && u.canDry && u.hasPowerAdapter && !u.powerAdapter) {
    add(1, Sev.INFO, () => ({
      severity: 'info', code: '', title: alertTitle(Sev.INFO), text: str('ALERT_ADAPTER'),
      unit: cut(u.name, ALERT_UNIT), slot_id: null, when_text: '', unread: false, actions: [],
    }));
  }
  for (const a of m.alerts) {
    if (a.laneFault) continue;
    if (unitIdx >= 0 ? a.unit !== unitIdx : !(a.unit < 0 && a.toolhead === thIdx)) continue;
    if (alertBadgeExpired(m, a)) continue;                       // aged out of the badge (4g); still in the history
    if (unitIdx >= 0) continue;                                  // low filament: the tile's ring says it
    if (th && buildMessage(m, th).text === alertText(m, a)) continue;   // the message row says it
    add(1, a.severity, () => historyItem(m, a));
  }

  if (count === 0) return null;
  return {
    severity: SEVERITY[sev],
    count,
    // The two tones of the message row: red for a fault or a pause, else yellow.
    tone: sev >= Sev.PAUSE ? 'error' : 'info',
    items,
  };
}

// --------------------------------------------------------------- toolhead

/** The tool name of a runout's slot ("T0"); "?" when its bay is in no group. */
function runoutTool(m: Model, unit: number, slot: number): { name: string; gi: number } {
  const gi = unit >= 0 && slot >= 0 ? slotGroup(m, unit, slot) : -1;
  return { name: gi >= 0 ? m.groups[gi].name : str('UNKNOWN_MARK'), gi };
}

/** The runout's one line, and whether it is an error: "T0 ran out; switching
 *  to oams1 Spool 2" (the target is the same tool) or "... to T1 (oams2 Spool 2)",
 *  "T0 ran out" while the target is not known, and "T0 ran out; no spare spool
 *  is left" (an error) when no target is named and the group has none. */
function runoutText(m: Model, th: Toolhead): { text: string; error: boolean } {
  const from = runoutTool(m, th.runoutFromUnit, th.runoutFromSlot);
  if (th.runoutFromUnit < 0 && th.runoutNote) return { text: copy(th.runoutNote, MESSAGE), error: false };   // the host's own words
  if (th.runoutFromUnit < 0) return { text: copy(str('STATUS_RUNOUT'), MESSAGE), error: false };

  let to = '';
  if (th.runoutToUnit >= 0 && th.runoutToUnit < m.units.length && th.runoutToSlot >= 0) {
    const toTool = runoutTool(m, th.runoutToUnit, th.runoutToSlot);
    const toUnit = m.units[th.runoutToUnit].name;
    to = toTool.gi >= 0 && toTool.gi === from.gi
      ? fmtUnitSpool(toUnit, th.runoutToSlot, RUNOUT_TARGET)
      : fmtToolSpool(toTool.name, toUnit, th.runoutToSlot, RUNOUT_TARGET);
  }
  // No target and nothing left to fall back to: say so plainly.
  if (!to && from.gi >= 0 && !groupNextSpare(m, from.gi)) {
    return { text: fmtRunoutNoSpare(from.name, MESSAGE), error: true };
  }
  return { text: fmtRunout(from.name, to, MESSAGE), error: false };
}

/** One message line: an error > a runout > a notice > the plain status. */
function buildMessage(m: Model, th: Toolhead): ViewToolhead['message'] {
  if (th.hasError) {
    return { text: cut(th.errorText || th.errorCode, MESSAGE), tone: 'error' };
  }
  if (th.runoutActive) {
    const r = runoutText(m, th);
    return { text: r.text, tone: r.error ? 'error' : 'info' };
  }
  if (th.currentGroup >= 0 && th.currentGroup < m.groups.length) {
    const tool = groupToolIndex(m.groups[th.currentGroup].name);
    const short = tool >= 0 ? toolShortfall(m, tool) : null;
    if (short) return { text: fmtShortfall(tool, short.need, short.have, MESSAGE), tone: 'info' };
  }
  // A tile in error that no lane fault covers: the tag says where, this row says
  // what (PRINCIPLES.md 10). A load or unload in progress keeps its step message.
  if (th.busy === Busy.NONE) {
    const thIdx = m.toolheads.indexOf(th);
    for (const un of m.units) {
      if (un.toolhead !== thIdx || !un.connected) continue;
      for (let si = 0; si < un.slots.length; si++) {
        if (un.slots[si].state !== SlotState.ERROR) continue;
        return { text: fmtSpoolError(fmtUnitSpool(un.name, si, MESSAGE), MESSAGE), tone: 'error' };
      }
    }
  }
  const status = th.busy === Busy.LOAD ? 'STATUS_LOADING' : th.busy === Busy.UNLOAD ? 'STATUS_UNLOADING'
    : th.loaded ? 'STATUS_LOADED' : 'STATUS_NO_FILAMENT';
  return { text: copy(str(status), MESSAGE), tone: 'neutral' };
}

function toolheadActions(m: Model, th: Toolhead): ViewAction[] {
  const list = new ActionList();
  // Unload and Stop, always, in this order; the predicate dims the one that does not apply.
  const unload = simple(list, m, 'unload', str('ACTION_UNLOAD'), 'normal', `unload ${th.id}`);
  if (unload && m.printing) confirm(unload, str('CONFIRM_UNLOAD_TITLE'), str('CONFIRM_UNLOAD_TEXT'), str('CONFIRM_UNLOAD_OK'));
  const stop = simple(list, m, 'stop', str('ACTION_STOP'), 'normal', `stop ${th.id}`);
  if (stop) confirm(stop, str('CONFIRM_STOP_TITLE'), str('CONFIRM_STOP_TEXT'), str('CONFIRM_STOP_OK'));
  return list.items;
}

/** The display label of one H2 stage step name, in the current language; a
 *  name newer than this list, or one that is already a sentence, shows as
 *  itself (stage_label() in view.c). A step the host gave a label of its own
 *  arrives here already spelled out (H3), so it falls through to that last
 *  case: the user's words, never translated. */
function stageLabel(name: string): string {
  switch (name) {
    case 'heat': return str('STEP_HEAT');
    case 'cut': return str('STEP_CUT');
    case 'retract': return str('STEP_RETRACT');
    case 'feed': return str('STEP_FEED');
    case 'purge': return str('STEP_PURGE');
    case 'grab': return str('STEP_GRAB');
    case 'calibrate': return str('STEP_CALIBRATE');
    case 'home': return str('STEP_HOME');
    case 'clean': return str('STEP_CLEAN');
    default: return name;
  }
}

/** The plan as the stepper draws it: every step's display text, and the
 *  running one carrying its own detail beside it ("Heat the nozzle · 250 °C",
 *  H3). The composition is the view's, so every renderer draws activity.steps
 *  as it always has. */
function stepLabels(th: Toolhead): string[] {
  const steps = th.steps.slice(0, 8).map(stageLabel);
  if (th.stepDetail && th.stepCurrent >= 0 && th.stepCurrent < steps.length) {
    steps[th.stepCurrent] = format('STEP_DETAIL', { step: steps[th.stepCurrent], detail: th.stepDetail }, STEP_DETAIL);
  }
  return steps;
}

/** The tools a toolhead could load right now: the group names, in group order,
 *  with at least one ready bay on one of its connected units. Whole names
 *  only: a name that no longer fits stops the list (ready_tools() in view.c). */
function readyTools(m: Model, thIdx: number): string {
  const sep = str('REST_JOIN');
  let out = '';
  for (const g of m.groups) {
    const ready = g.members.some((mem) => {
      const un = m.units[mem.unit];
      return un !== undefined && un.toolhead === thIdx && un.connected
        && mem.slot >= 0 && mem.slot < un.slots.length && un.slots[mem.slot].state === SlotState.READY;
    });
    if (!ready) continue;
    const add = out ? sep + g.name : g.name;
    if (new TextEncoder().encode(out + add).length >= 96) break;   // the C buffer's size, in bytes
    out += add;
  }
  return out;
}

/** Nothing is loaded: what could be ("Ready: T0 · T1"), or the next step when
 *  no bay is ready (build_rest_empty() in view.c). */
function restEmpty(m: Model, thIdx: number): string {
  const tools = readyTools(m, thIdx);
  return tools ? cut(format('REST_READY', { tools }, REST_PATH), REST_PATH) : str('REST_INSERT');
}

/** What the stepper's slot rests on while no plan runs (PRINCIPLES.md 2, "A
 *  reserved slot has a resting state"): the toolhead's own path. A bay feeding
 *  it names the tool, the bay and the material and carries the filament's own
 *  color - the one the hotend icon tints with; nothing from a bay rests as
 *  REST_READY or REST_INSERT with no color. An external spool (loadedExt >= 0) has no bay to
 *  name, so its path rests empty too, as in build_activity_rest() in view.c. */
function activityRest(m: Model, th: Toolhead, thIdx: number): ViewToolhead['activity']['rest'] {
  if (!th.loaded || th.loadedExt >= 0 || th.loadedUnit < 0 || th.loadedUnit >= m.units.length) {
    return { label: restEmpty(m, thIdx), color: null, outline: false, loaded: false };
  }
  const unit = m.units[th.loadedUnit];
  if (th.loadedSlot < 0 || th.loadedSlot >= unit.slots.length) {
    return { label: restEmpty(m, thIdx), color: null, outline: false, loaded: false };
  }
  const slot = unit.slots[th.loadedSlot];
  // The tool label is the one the hotend icon overlays: the group's name, or
  // "?" for a bay in no group.
  const gi = slotGroup(m, th.loadedUnit, th.loadedSlot);
  const tool = gi >= 0 ? m.groups[gi].name : str('UNKNOWN_MARK');
  const spool = fmtUnitBay(unit.name, th.loadedSlot, SUBLABEL);
  const label = slot.material
    ? format('REST_PATH', { tool, spool, material: slot.material }, REST_PATH)
    : format('REST_PATH_NO_MATERIAL', { tool, spool }, REST_PATH);
  return {
    label: cut(label, REST_PATH),
    color: slot.colorKnown ? hex(slot.color) : null,
    outline: slot.colorKnown && outlineFor(slot.color),
    loaded: true,
  };
}

function buildToolhead(m: Model, index: number): ViewToolhead {
  const th = m.toolheads[index];
  const thAlert = scanAlerts(m, -1, index);
  const message = buildMessage(m, th);
  const activityKind: ViewToolhead['activity']['kind'] =
    th.hasError ? 'error' : th.runoutActive ? 'runout' : th.busy === Busy.LOAD ? 'loading'
      : th.busy === Busy.UNLOAD ? 'unloading' : 'idle';

  // The loaded tool: the hotend icon's overlay and tint only.
  let tool: ViewToolhead['tool'] = null;
  if (th.loaded) {
    const gi = th.loadedUnit >= 0 ? slotGroup(m, th.loadedUnit, th.loadedSlot) : -1;
    let color = 0;
    let known = false;
    if (th.loadedExt < 0 && th.loadedUnit >= 0 && th.loadedUnit < m.units.length) {
      const slot = m.units[th.loadedUnit].slots[th.loadedSlot];
      if (slot && slot.colorKnown) {
        color = slot.color;
        known = true;
      }
    }
    tool = {
      label: cut(gi >= 0 ? m.groups[gi].name : str('UNKNOWN_MARK'), LABEL),
      color: known ? hex(color) : null,
      ink: inkFor(color),
      outline: known && outlineFor(color),
    };
  }

  const pressure: ViewToolhead['pressure'] = th.pressure >= 0
    ? {
      value: cjsonNumber(th.pressure), set_point: cjsonNumber(th.setPoint),
      band: th.bandLow >= 0 && th.bandHigh > th.bandLow && th.bandHigh <= 1
        ? [cjsonNumber(th.bandLow), cjsonNumber(th.bandHigh)] : null,
      label: str('LABEL_PRESSURE'),
      text: fmtPressure(th.pressure, PRESSURE_TEXT),
      scale: [fmtScale(0, SCALE), fmtScale(th.setPoint, SCALE), fmtScale(1, SCALE)],
    }
    : null;

  return {
    id: cut(th.id, ID),
    // Toolhead = extruder: the title is the extruder's display name, the FPS id
    // (as configured) sits under it.
    title: th.extruder ? fmtExtruderTitle(th.extruder, TITLE) : copy(str('TOOLHEAD_DEFAULT_TITLE'), TITLE),
    subtitle: th.id,
    tool,
    pressure,
    activity: {
      kind: activityKind,
      steps: stepLabels(th),
      index: th.stepCurrent,
      failed: th.stepFailed,
      // No plan running, so the slot rests on the path instead. The two are
      // never both present.
      rest: th.steps.length === 0 ? activityRest(m, th, index) : null,
    },
    message,
    alert: thAlert,
    alert_clear: toolheadScopeClear(m, index, th, thAlert, message),
    actions: toolheadActions(m, th),
    units: m.units.flatMap((u, i) => (u.toolhead === index ? [buildUnit(m, i)] : [])),
  };
}

// ------------------------------------------------------------------- panel

function buildSetting(m: Model, i: number): ViewSetting {
  const values = [
    m.settings.readTagOnInsertion, m.settings.readTagsAtStartup, m.settings.autoReloadOnRunout,
    m.settings.autoCreateSpoolmanSpools, m.settings.applyPaOnLoad,
  ];
  const key = SETTING_KEYS[i];
  const label = str(`SETTING_${i}_TITLE` as 'SETTING_0_TITLE');
  const note = str(`SETTING_${i}_NOTE` as 'SETTING_0_NOTE');
  // The last row is the language, not an on/off switch: a list of the languages
  // built in, each by its own name, with the current one selected. It sends
  // no action; the host calls setLanguage() with the code it picked.
  if (i === LANGUAGE_SETTING_ROW) {
    return {
      key: cut(key, SETTING_KEY),
      label,
      note,
      value: false,
      action_on: null,
      action_off: null,
      // The pseudo-locale is for tests and layout review: available through
      // setLanguage(), never in the list.
      options: languages().filter((l) => l.code !== 'qps').map((l) => ({ code: l.code, label: l.label })),
      selected: languages().filter((l) => l.code !== 'qps').findIndex((l) => l.code === language()),
    };
  }
  const toggle = (id: string, text: string, line: string): ViewAction => {
    const a: ViewAction = {
      id, label: text, line: cut(line, ACTION_LINE), enabled: false, reason: '',
      style: 'normal', target: 'tile', confirm: null, form: null,
    };
    check(a, cut(line, ACTION_LINE), m);
    return a;
  };
  return {
    key: cut(key, SETTING_KEY),
    label,
    note,
    value: values[i],
    action_on: toggle('setting_on', str('ACTION_ON'), `setting ${key} on`),
    action_off: toggle('setting_off', str('ACTION_OFF'), `setting ${key} off`),
    options: [],
    selected: 0,
  };
}

/** The whole panel tree (view_json()): what a renderer draws. */
export function buildView(m: Model): View {
  // The panel's own alerts: the history entries that belong to no unit and no
  // toolhead (and are not a lane fault's), for the host's bar.
  const panelAlerts: Alert[] = [];
  for (const a of m.alerts) {
    if (panelAlerts.length >= MAX_ALERTS) break;
    if (a.unit >= 0 || a.toolhead >= 0 || a.laneFault) continue;
    panelAlerts.push(a);
  }

  let notice: View['notice'] = null;
  if (m.units.length === 0) notice = { text: str('PANEL_NO_UNITS') };
  else if (!m.units.some((u) => u.connected)) notice = { text: str('PANEL_HOST_OFFLINE') };
  else if (!m.settings.known) notice = { text: str('PANEL_NOT_READY') };

  const pending: string[] = [];
  for (let i = 0; i < m.units.length && pending.length < MAX_PENDING_SPOOLS; i++) {
    for (let j = 0; j < m.units[i].slots.length && pending.length < MAX_PENDING_SPOOLS; j++) {
      if (m.units[i].slots[j].pendingConfirmation) pending.push(cut(slotId(m, i, j), ID));
    }
  }

  const unassigned: number[] = [];
  m.units.forEach((u, i) => { if (u.toolhead < 0 && unassigned.length < MAX_UNASSIGNED) unassigned.push(i); });

  return {
    toolheads: m.toolheads.map((_, i) => buildToolhead(m, i)),
    unassigned_units: unassigned.map((i) => buildUnit(m, i)),
    alerts: panelAlerts.map((a) => historyItem(m, a)),
    alert_count: panelAlerts.length,
    unread_count: panelAlerts.filter((a) => a.unread).length,
    notice,
    // The empty list's own words: a list with nothing in it has no load under
    // way anywhere to report on, so it says so rather than showing a blank.
    nothing_to_load: m.busyUnit < 0 && !m.loaded ? { text: str('NOTICE_NOTHING_TO_LOAD') } : null,
    labels: {
      alerts: str('LABEL_ALERTS'),
      settings: str('LABEL_SETTINGS'),
      fault: str('LABEL_FAULT'),
      pressure: str('LABEL_PRESSURE'),
      no_alerts: str('LABEL_NO_ALERTS'),
      close: str('LABEL_CLOSE'),
      cancel: str('LABEL_CANCEL'),
      no_response: str('LABEL_NO_RESPONSE'),
      edit_failed: str('LABEL_EDIT_FAILED'),
      action_refused: str('ACTION_REFUSED'),
      groups: str('SCREEN_GROUPS'),
      no_group: str('MAP_NO_GROUP'),
      low: str('TILE_LOW'),
    },
    spoolman: { online: m.spoolmanOnline, pending },
    settings: SETTING_KEYS.map((_, i) => buildSetting(m, i)),
  };
}


// f32 and fmtFixed are re-exported for the tests that build expected numbers.
export { f32, fmtFixed };

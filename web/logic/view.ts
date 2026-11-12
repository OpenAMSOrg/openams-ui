// The view builders (src/view/view.c and view_json.c): the one place that turns
// the model into content. Both renderers draw only what these return; a
// renderer never reads the model and never composes text of its own
// (docs/design/UNIFIED_UI.md 2).
//
// The C fills fixed structs and serializes them; this builds the same JSON
// shapes directly. The text limits of those structs are kept (every string is
// cut to its field's size), and the differential tests hold the two identical.

import { cjsonNumber, cut, f32, fmtFixed } from './cstr.ts';
import { actionsCheck, dryReason, mayRotate } from './actions.ts';
import {
  Busy, DryState, InfoSource, CalState, SlotState, Sev, TagRead, TagReason, Variant, MAX_TOOLS, MATERIALS, editDefaults,
  editVendorCount, editVendorName, GROUP_CHOICE_NEW, GROUP_CHOICE_NONE, groupEditReason, groupNextSpare,
  groupOnUnitLane, laneIdOk, slotGroup, slotId, toolShortfall, alertBadgeExpired,
} from './model.ts';
import type { Alert, Model, Slot, Toolhead, Unit } from './model.ts';
import { AlertKind } from './model.ts';
import {
  copy, fmtDryerActive, fmtEnv, fmtExtruderTitle, fmtGrams, fmtPct, fmtPressure, fmtRunout, fmtRunoutNoSpare,
  fmtLowFilament, fmtScale, fmtShortfall, fmtSpoolError, fmtSpoolLabel, fmtSpoolOption, fmtToolSpool, fmtUnitBay, fmtUnitSpool,
  format, language, languages, SETTING_KEYS, str,
} from './strings.ts';
import type {
  ActionStyle, Ink, Severity, Tone, View, ViewAction, ViewField, ViewFieldOption, ViewAlert, ViewAlertGroup, ViewGroup,
  ViewGroupMember, ViewSetting, ViewTagRead, ViewTile, ViewToolhead, ViewUnit,
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
const ALERT_TEXT = 192;       // VIEW_ALERT_TEXT_LEN: a composed alert's sentence or title
const SPOOL_NAME = 64;        // the {spool} of a low-filament sentence
const RUNOUT_TARGET = LABEL * 3;
const SETTING_KEY = 32;
const ACTION_ID = 20;
const ACTION_LINE = 168;
const OPTION_LABEL = 72;
const TAG_READ_TEXT = 256;    // VIEW_TAG_READ_TEXT_LEN: "This tag is also in slot {slot}. Load this spool ..."
const TAG_MATERIAL = 16;      // VIEW_TAG_MATERIAL_LEN (MMU_TAG_MATERIAL_LEN)

const MAX_ACTIONS = 12;   // VIEW_ACTIONS_MAX in src/view/view.h
const MAX_ACTIONS_PER_ALERT = 3;   // a failed toolchange: retry, unload, clear errors
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

  const tile: ViewTile = {
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
  // Left out when there is nothing to say, so a unit that decodes its own tags
  // (or an older host) gives the tile it always gave.
  const read = tileTagRead(m, unitIdx, bay);
  if (read) tile.tag_read = read;
  return tile;
}

// ---------------------------------------------------------------- tag read

/** The unit and bay whose host slot id is `id` (its `bays[i].id`), or null
 *  (unit_of_global_id() in view.c). */
function unitOfGlobalId(m: Model, id: number): { unit: number; bay: number } | null {
  for (let i = 0; i < m.units.length; i++) {
    const slots = m.units[i].slots;
    for (let j = 0; j < slots.length; j++) if (slots[j].globalId === id) return { unit: i, bay: j };
  }
  return null;
}

/** A conflict's note, by its reason. claimed_twice names the other bay the way
 *  the toolhead's resting path does ("oams1:2"); a slot id no bay of this model
 *  has is the host's number, and none at all is "?". */
function tagConflictText(m: Model, s: Slot): string {
  switch (s.tagReason) {
    case TagReason.TWO_MOVING:
    case TagReason.AMBIGUOUS_PAIR: return str('TAGR_TWO_SPOOLS');
    case TagReason.CHANGED_IN_PLACE: return str('TAGR_CHANGED');
    case TagReason.NO_TAG_AFTER_TURN: return str('TAGR_NO_TAG');
    case TagReason.CLAIMED_TWICE: {
      let slot: string;
      const other = s.tagOther >= 0 ? unitOfGlobalId(m, s.tagOther) : null;
      if (s.tagOther < 0) slot = copy(str('UNKNOWN_MARK'), SUBLABEL);
      else if (other) slot = fmtUnitBay(m.units[other.unit].name, other.bay, SUBLABEL);
      else slot = copy(String(s.tagOther), SUBLABEL);
      return format('TAGR_CLAIMED_TWICE', { slot }, TAG_READ_TEXT);
    }
    default: return str('TAGR_CONFLICT');   // a reason this build does not know
  }
}

/** What the host's tag attribution says about a bay (view_tile_tag_read() in
 *  view.c), or null when there is nothing to say: no attribution on this unit,
 *  or the tag is attributed (the RFID mark and the link say it). */
function tileTagRead(m: Model, unitIdx: number, bay: number): ViewTagRead | null {
  const s = m.units[unitIdx].slots[bay];
  switch (s.tagRead) {
    // a normal state, said plainly: no warning color
    case TagRead.UNKNOWN: return { state: 'unknown', tone: 'neutral', text: str('TAGR_UNKNOWN'), candidates: [] };
    case TagRead.CANDIDATE: return {
      state: 'candidate', tone: 'neutral', text: str('TAGR_CANDIDATE'),
      candidates: s.tagCandidates.map((k) => ({
        material: cut(k.material, TAG_MATERIAL),
        color: k.color >= 0 ? hex(k.color) : null,
        color_name: k.color >= 0 ? colorName(k.color) : '',
      })),
    };
    // the amber information look: a read that disagrees never blocks a print
    case TagRead.CONFLICT: return { state: 'conflict', tone: 'info', text: tagConflictText(m, s), candidates: [] };
    default: return null;
  }
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

// A color is named by the palette entry nearest to it as the eye sees it:
// CIE94 in CIELAB (D65), lightness at half weight, the palette entry as the
// reference. The same tables and steps as view_color_name() in the C (no
// Math.cbrt or Math.pow), so both cores name every color alike.

// sRGB 0..255 to linear light (IEC 61966-2-1).
const SRGB_LIN: readonly number[] = [
  0.0, 0.0003035269835488375, 0.000607053967097675, 0.0009105809506465125,
  0.00121410793419535, 0.0015176349177441874, 0.001821161901293025, 0.0021246888848418626,
  0.0024282158683907, 0.0027317428519395373, 0.003035269835488375, 0.003346535763899161,
  0.003676507324047436, 0.004024717018496307, 0.004391442037410293, 0.004776953480693729,
  0.005181516702338386, 0.005605391624202723, 0.006048833022857054, 0.006512090792594475,
  0.006995410187265387, 0.007499032043226175, 0.008023192985384994, 0.008568125618069307,
  0.009134058702220787, 0.00972121732023785, 0.010329823029626936, 0.010960094006488246,
  0.011612245179743885, 0.012286488356915872, 0.012983032342173012, 0.013702083047289686,
  0.014443843596092545, 0.01520851442291271, 0.01599629336550963, 0.016807375752887384,
  0.017641954488384078, 0.018500220128379697, 0.019382360956935723, 0.0202885630566524,
  0.021219010376003555, 0.02217388479338738, 0.02315336617811041, 0.024157632448504756,
  0.02518685962736163, 0.026241221894849898, 0.027320891639074894, 0.028426039504420793,
  0.0295568344378088, 0.030713443732993635, 0.03189603307301153, 0.033104766570885055,
  0.03433980680868217, 0.03560131487502034, 0.03688945040110004, 0.0382043715953465,
  0.03954623527673284, 0.04091519690685319, 0.042311410620809675, 0.043735029256973465,
  0.04518620438567554, 0.046665086336880095, 0.04817182422688942, 0.04970656598412723,
  0.05126945837404324, 0.052860647023180246, 0.05448027644244237, 0.05612849004960009,
  0.05780543019106723, 0.0595112381629812, 0.06124605423161761, 0.06301001765316767,
  0.06480326669290577, 0.06662593864377289, 0.06847816984440017, 0.07036009569659588,
  0.07227185068231748, 0.07421356838014963, 0.07618538148130785, 0.07818742180518633,
  0.08021982031446832, 0.0822827071298148, 0.08437621154414882, 0.08650046203654976,
  0.08865558628577294, 0.09084171118340768, 0.09305896284668745, 0.0953074666309647,
  0.09758734714186246, 0.09989872824711389, 0.10224173308810132, 0.10461648409110419,
  0.10702310297826761, 0.10946171077829933, 0.1119324278369056, 0.11443537382697373,
  0.11697066775851084, 0.11953842798834562, 0.12213877222960187, 0.12477181756095049,
  0.12743768043564743, 0.1301364766903643, 0.13286832155381798, 0.13563332965520566,
  0.13843161503245183, 0.14126329114027164, 0.14412847085805777, 0.14702726649759498,
  0.14995978981060856, 0.15292615199615017, 0.1559264637078274, 0.1589608350608804,
  0.162029375639111, 0.1651321945016676, 0.16826940018969075, 0.1714411007328226,
  0.17464740365558504, 0.17788841598362912, 0.18116424424986022, 0.184474994500441,
  0.18782077230067787, 0.19120168274079138, 0.1946178304415758, 0.19806931955994886,
  0.20155625379439707, 0.20507873639031693, 0.20863687014525575, 0.21223075741405523,
  0.21586050011389926, 0.2195261997292692, 0.2232279573168085, 0.22696587351009836,
  0.23074004852434915, 0.23455058216100522, 0.238397573812271, 0.24228112246555486,
  0.24620132670783548, 0.25015828472995344, 0.25415209433082675, 0.2581828529215958,
  0.26225065752969623, 0.26635560480286247, 0.2704977910130658, 0.27467731206038465,
  0.2788942634768104, 0.2831487404299921, 0.2874408377269175, 0.29177064981753587,
  0.2961382707983211, 0.3005437944157765, 0.3049873140698863, 0.30946892281750854,
  0.31398871337571754, 0.31854677812509186, 0.32314320911295075, 0.3277780980565422,
  0.33245153634617935, 0.33716361504833037, 0.3419144249086609, 0.3467040563550296,
  0.35153259950043936, 0.3564001441459435, 0.3613067797835095, 0.3662525955988395,
  0.3712376804741491, 0.3762621229909065, 0.38132601143253014, 0.386429433787049,
  0.39157247774972326, 0.39675523072562685, 0.4019777798321958, 0.4072402119017367,
  0.41254261348390375, 0.4178850708481375, 0.4232676699860717, 0.4286904966139066,
  0.43415363617474895, 0.4396571738409188, 0.44520119451622786, 0.45078578283822346,
  0.45641102318040466, 0.4620769996544071, 0.467783796112159, 0.47353149614800955,
  0.4793201831008268, 0.4851499400560704, 0.4910208498478356, 0.4969329950608704,
  0.5028864580325687, 0.5088813208549338, 0.5149176653765214, 0.5209955732043543,
  0.5271151257058131, 0.5332764040105052, 0.5394794890121072, 0.5457244613701866,
  0.5520114015120001, 0.5583403896342679, 0.5647115057049292, 0.5711248294648731,
  0.5775804404296506, 0.5840784178911641, 0.5906188409193369, 0.5972017883637634,
  0.6038273388553378, 0.6104955708078648, 0.6172065624196511, 0.6239603916750761,
  0.6307571363461468, 0.6375968739940326, 0.6444796819705821, 0.6514056374198242,
  0.6583748172794485, 0.665387298282272, 0.6724431569576875, 0.6795424696330938,
  0.6866853124353135, 0.6938717612919899, 0.7011018919329731, 0.7083757798916868,
  0.7156935005064807, 0.7230551289219693, 0.7304607400903537, 0.7379104087727308,
  0.7454042095403874, 0.7529422167760779, 0.7605245046752924, 0.768151147247507,
  0.7758222183174236, 0.7835377915261935, 0.7912979403326302, 0.799102738014409,
  0.8069522576692516, 0.8148465722161012, 0.8227857543962835, 0.8307698767746546,
  0.83879901174074, 0.846873231509858, 0.8549926081242338, 0.8631572134541023,
  0.8713671191987972, 0.8796223968878317, 0.8879231178819663, 0.8962693533742664,
  0.9046611743911496, 0.9130986517934192, 0.9215818562772946, 0.9301108583754237,
  0.938685728457888, 0.9473065367331999, 0.9559733532492861, 0.9646862478944651,
  0.9734452903984125, 0.9822505503331171, 0.9911020971138298, 1.0,
];

const PALETTE_LAB: readonly (readonly number[])[] = [
  [100.0, 0.00526049995830391, -0.010408184525267927, 0.011662039483869971, 1.0005247917767741, 1.000174930592258], // White
  [69.02938243087203, -0.866573391349601, -0.8941696937529331, 1.245186284786884, 1.0560333828154098, 1.0186777942718033], // Silver
  [41.77650138060353, -0.45470687083229433, -3.3706606946920736, 3.401192710965397, 1.1530536719934428, 1.051017890664481], // Gray
  [0.0, 0.0, 0.0, 0.0, 1.0, 1.0], // Black
  [40.18732294290569, 35.98198740453745, 32.01388679116317, 48.16214659935439, 3.1672965969709472, 1.7224321989903157], // Brown
  [78.3967597491752, 3.8601421451386897, 47.71332833654991, 47.86922182709984, 3.1541149822194927, 1.7180383274064976], // Gold
  [43.34565239957829, 56.752747120545955, 44.307541762455095, 72.00022612992504, 4.240010175846626, 2.0800033919488756], // Red
  [35.14124878943107, 50.47919446278032, 20.463023756354804, 54.46911432054254, 3.451110144424414, 1.8170367148081379], // Maroon
  [63.02347245980374, 53.394890918283366, 68.31391246078134, 86.70527672451473, 4.901737452603163, 2.300579150867721], // Orange
  [91.93077843728416, -17.369252037272464, 84.81112172853828, 86.57145768200529, 4.895715595690238, 2.2985718652300795], // Yellow
  [62.22419628081141, -60.41090870479626, 43.96606699921269, 74.71608219064011, 4.362223698578805, 2.1207412328596016], // Green
  [34.50042634228064, -28.399283975755196, 13.935781878843933, 31.634243264385205, 2.423540946897334, 1.474513648965778], // Forest green
  [65.64200259210575, -34.69768067088047, -13.794798307486467, 37.33932918897478, 2.680269813503865, 1.5600899378346216], // Turquoise
  [53.92631173697171, -0.7622929047542693, -49.343825624615725, 49.34971345149962, 3.220737105317483, 1.7402457017724942], // Cyan
  [21.786693478695305, 29.738147586756412, -55.74355388497929, 63.17991153538675, 3.8430960190924037, 1.9476986730308012], // Blue
  [37.50449947882442, 40.59843136966174, -57.70647657320136, 70.55685698903099, 4.1750585645063945, 2.058352854835465], // Purple
  [51.52635743551946, 81.51508260525952, -8.517292018946264, 81.95884915906451, 4.688148212157903, 2.2293827373859676], // Magenta
  [59.875871575875465, 61.09542411393964, 17.93015403411058, 63.67213889409629, 3.865246250234333, 1.9550820834114444], // Pink
];

// Cube root and square root by a fixed number of Newton steps.
function cbrtNewton(t: number): number {
  let x = 1.0;
  for (let i = 0; i < 24; i++) x = (2.0 * x + t / (x * x)) / 3.0;
  return x;
}

function sqrtNewton(v: number): number {
  if (v <= 0.0) return 0.0;
  let x = v > 1.0 ? v : 1.0;
  for (let i = 0; i < 40; i++) x = 0.5 * (x + v / x);
  return x;
}

const labF = (t: number): number => (t > 0.008856 ? cbrtNewton(t) : 7.787 * t + 16.0 / 116.0);

/** The palette name nearest to `rgb` as the eye sees it (CIE94, kL = 2). */
export function colorName(rgb: number): string {
  const r = SRGB_LIN[(rgb >> 16) & 0xff];
  const g = SRGB_LIN[(rgb >> 8) & 0xff];
  const b = SRGB_LIN[rgb & 0xff];
  const fx = labF((0.4124 * r + 0.3576 * g + 0.1805 * b) / 0.95047);
  const fy = labF(0.2126 * r + 0.7152 * g + 0.0722 * b);
  const fz = labF((0.0193 * r + 0.1192 * g + 0.9505 * b) / 1.08883);
  const L = 116.0 * fy - 16.0;
  const A = 500.0 * (fx - fy);
  const B = 200.0 * (fy - fz);
  const C = sqrtNewton(A * A + B * B);
  let best = 0;
  let bestD = -1.0;
  PALETTE_LAB.forEach((p, i) => {
    const dl = (L - p[0]) / 2.0;
    const dc = C - p[3];
    const da = A - p[1];
    const db = B - p[2];
    let dh2 = da * da + db * db - dc * dc;
    if (dh2 < 0.0) dh2 = 0.0;
    const d = dl * dl + (dc / p[4]) * (dc / p[4]) + dh2 / (p[5] * p[5]);
    if (bestD < 0.0 || d < bestD) {
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
  // The unanswered material is the dialog's own required field, and dimming the
  // row for it would hide the dialog that asks. So the dry run is the editor's,
  // with a material that is an answer.
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

  // A bay nobody has identified asks for its spool before it is loaded, when
  // there is a Spoolman to save the answer to. Spoolman is optional: without it
  // the bay's Load is the plain load.
  if (bayIsUnknown(m, unitIdx, bay) && m.spoolmanEdit && m.spoolmanOnline) {
    addSaveAndLoadAction(list, m, unitIdx, bay, id);
  } else simple(list, m, 'load', str('ACTION_LOAD'), 'normal', `load ${id}`);
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
  // "It is this slot": the tag the bay's reader holds is this bay's. A bay
  // whose tag the host holds as a candidate offers it beside its Re-read, and
  // so does a conflict, which a re-read does not settle.
  if (u.rfid && (s.tagRead === TagRead.CANDIDATE || s.tagRead === TagRead.CONFLICT)) {
    simple(list, m, 'tag_assert', str('ACTION_TAG_ASSERT'), 'normal', `tag_assert ${id}`);
  }
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
  // A unit that turns its spools while drying gets a third field, the choice;
  // the line then ends in `rotate {rotate}` (1 or 0).
  const rotate = mayRotate(u);
  const line = rotate ? `dry ${u.name} start {target}C {hours}h rotate {rotate}` : `dry ${u.name} start {target}C {hours}h`;
  const a = list.add('dry_start', str('ACTION_DRY_START'), line, 'normal');
  if (a) {
    a.form = {
      fields: [
        { id: 'target', label: str('FIELD_TARGET_LABEL'), kind: 'number', value: target, min: 45, max: tmax,
          step: 5, unit: str('FIELD_TARGET_UNIT'), required: false },
        { id: 'hours', label: str('FIELD_HOURS_LABEL'), kind: 'number', value: hours, min: 1, max: 24,
          step: 1, unit: str('FIELD_HOURS_UNIT'), required: false },
        ...(rotate ? [{ id: 'rotate', label: str('SET_ROTATE'), kind: 'toggle' as const, value: 0, min: 0, max: 1,
          step: 1, unit: '', required: false }] : []),
      ],
      submit_label: null, require_change: false,
    };
    if (dryReason(u)) {
      a.enabled = false;
      a.reason = dryReason(u);   // the host's own words, borrowed in C
    } else {
      check(a, cut(`dry ${u.name} start ${target}C ${hours}h${rotate ? ' rotate 0' : ''}`, ACTION_LINE), m);
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

// ------------------------------------------------------ failed toolchanges

/** Whether a sequence runs on the lane: busy loading or unloading, or a stage
 *  plan with a current step that has not failed (lane_runs() in view.c). */
function laneRuns(th: Toolhead): boolean {
  return th.busy !== Busy.NONE
    || (th.steps.length > 0 && th.stepCurrent >= 0 && th.stepCurrent < th.steps.length && th.stepFailed < 0);
}

/** The lane's failed toolchange or unload as the panel shows it (C2 4.2
 *  `toolchange_fault`): not while a sequence runs on the lane, because the
 *  host keeps the fault until a retry has finished, and the retry in progress
 *  shows its own steps and status instead. A plan that was already on the
 *  lane, unfinished, when the fault appeared (tcPlanStale) is an older
 *  sequence's leftover and hides nothing; a lane that is loading or unloading
 *  always does (tc_fault_shown() in view.c). */
const tcFaultShown = (th: Toolhead): boolean =>
  th.tcFault && th.busy === Busy.NONE && (th.tcPlanStale || !laneRuns(th));

/** Whether the failed toolchange has a Retry of its own: only with the word
 *  the host advertises (`commands.retry`), and only for a lane whose id can
 *  go into a script. While it has one, a lane fault's own `retry_load` is not
 *  offered: one "Retry", the one that finishes the whole toolchange - also
 *  while that Retry has been sent and is not offered again
 *  (tc_retry_stands() in view.c). */
const tcRetryStands = (m: Model, th: Toolhead): boolean =>
  m.cmdRetry !== '' && tcFaultShown(th) && laneIdOk(th.id);

/** Whether the panel offers that Retry now: not once it has been sent for
 *  this fault (tcRetrySent), until the fault changes or clears or the host
 *  answers the retry. Retry is sent once (tc_retry_offered() in view.c). */
const tcRetryOffered = (m: Model, th: Toolhead): boolean => tcRetryStands(m, th) && !th.tcRetrySent;

/** What failed, and with `where` the step it failed at when the host named
 *  one: "Loading T1 failed (Heat the nozzle)". The host's reason is not in it:
 *  an alert item borrows that whole as its text, and the message row adds it
 *  (tc_fault_subject() in view.c). */
function tcFaultSubject(th: Toolhead, where: boolean, n: number): string {
  const step = where && th.tcStep ? stageLabel(th.tcStep) : null;
  const args: Record<string, string> = {};
  if (!th.tcUnload) args.tool = th.tcGroup || str('UNKNOWN_MARK');
  if (step !== null) args.step = step;
  const id = th.tcUnload
    ? (step !== null ? 'TOOLCHANGE_UNLOAD_FAILED_AT' : 'TOOLCHANGE_UNLOAD_FAILED')
    : (step !== null ? 'TOOLCHANGE_LOAD_FAILED_AT' : 'TOOLCHANGE_LOAD_FAILED');
  return format(id, args, n);
}

/** The message row's line: what failed and the host's reason, "Loading T1
 *  failed: <reason>". The step is not repeated: the stepper above marks it
 *  (principle 4), and the alert item's title names it. A long reason is cut
 *  in the one-line row; the alert item has it whole (tc_fault_text()). */
const tcFaultText = (th: Toolhead, n: number): string =>
  format('TOOLCHANGE_FAILED_REASON', { failed: tcFaultSubject(th, false, ALERT_TEXT), reason: th.tcReason }, n);

/** The lane's failed toolchange or unload as an alert item: what failed and
 *  where as its title, the host's reason (borrowed whole in C) as its text,
 *  and the host's three ways on (UI_API "Retry a failed load or unload"):
 *  Retry (with the advertised word only), Unload to back it out, and Clear
 *  errors (the host's Reset, which clears the units' faults and keeps this
 *  one). */
function tcFaultItem(m: Model, th: Toolhead): ViewAlert {
  const list = new ActionList();
  if (tcRetryOffered(m, th)) simple(list, m, 'retry', str('ACTION_RETRY_LOAD'), 'primary', `fault retry ${th.id}`);
  // A lane id that cannot go into a script gets neither Retry nor Unload.
  const unload = laneIdOk(th.id)
    ? simple(list, m, 'unload', str('ACTION_UNLOAD'), 'normal', `unload ${th.id}`)
    : null;
  if (unload && m.printing) confirm(unload, str('CONFIRM_UNLOAD_TITLE'), str('CONFIRM_UNLOAD_TEXT'), str('CONFIRM_UNLOAD_OK'));
  simple(list, m, 'clear_errors', str('ACTION_CLEAR_ERRORS'), 'danger', 'fault clear_errors');
  return {
    severity: SEVERITY[Sev.PAUSE],
    code: '',
    title: tcFaultSubject(th, true, ALERT_TEXT),
    text: th.tcReason,
    unit: null,
    slot_id: null,
    when_text: '',
    unread: true,
    actions: list.items.slice(0, MAX_ACTIONS_PER_ALERT),
  };
}

/** Whether the lane fault has any action to offer (without building them). */
function faultHasActions(m: Model, th: Toolhead): boolean {
  return th.errorAct.some((tok) => {
    if (tok === 'resume' || tok === 'clear_errors') return true;
    if (tok === 'clear_fault') return th.errorUnit >= 0;
    if (tok === 'retry_load') {
      if (tcRetryStands(m, th)) return false;
      return (th.currentGroup >= 0 && th.currentGroup < m.groups.length)
        || (th.errorUnit >= 0 && slotGroup(m, th.errorUnit, th.errorSlot) >= 0);
    }
    return false;
  });
}

/** The actions a lane fault's support tokens offer, in the host's order (the
 *  fault's first two tokens): retry, resume, clear errors, and the unit-scoped
 *  clear fault when the fault names a unit. `retry_load` gives way to the
 *  failed toolchange's own Retry while it has one (tcRetryStands()). */
function faultActions(m: Model, th: Toolhead): ViewAction[] {
  const list = new ActionList();
  for (const tok of th.errorAct) {
    if (tok === 'retry_load') {
      if (tcRetryStands(m, th)) continue;    // the toolchange's own Retry stands for it
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
  // The lane's failed toolchange or unload names no unit: its toolhead's, with
  // its recovery actions (principle 4: a load failure keeps its FPS badge,
  // because the badge holds them).
  if (unitIdx < 0 && th && tcFaultShown(th)) add(1, Sev.PAUSE, () => tcFaultItem(m, th));
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

/** One message line: an error (the lane fault, else a failed toolchange) > a
 *  runout > a notice > the plain status. */
function buildMessage(m: Model, th: Toolhead): ViewToolhead['message'] {
  if (th.hasError) {
    return { text: cut(th.errorText || th.errorCode, MESSAGE), tone: 'error' };
  }
  if (tcFaultShown(th)) return { text: tcFaultText(th, MESSAGE), tone: 'error' };
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
  // A plan running with the lane not busy yet (home, heat, cut: before the AMS
  // moves, whichever interface started the flow) is a load or an unload all the
  // same; the toolhead holding filament makes it an unload (view.c).
  const planRunning = th.steps.length > 0 && th.stepCurrent >= 0 && th.stepCurrent < th.steps.length
    && th.stepFailed < 0;
  const activityKind: ViewToolhead['activity']['kind'] =
    th.hasError || tcFaultShown(th) ? 'error' : th.runoutActive ? 'runout' : th.busy === Busy.LOAD ? 'loading'
      : th.busy === Busy.UNLOAD ? 'unloading' : planRunning ? (th.loaded ? 'unloading' : 'loading') : 'idle';

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

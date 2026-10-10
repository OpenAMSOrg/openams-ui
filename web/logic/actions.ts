// Action lines to host commands, and the one predicate that says whether an
// action can run now (src/backend/actions_map.c and the fill/resolve step of
// platform/core-wasm/core_wasm.c). The view dims exactly what actionsCheck()
// refuses, with the same reason, so there is one source for "enabled" and
// "why not" (U1c's rule); the reasons are user sentences from strings.
//
//   a line   "unload fps1", "dry oams1 start {target}C {hours}h", ...
//   a result { kind: 'gcode' | 'rpc' | 'error' | 'local' }  (ActionResult)

import { cut } from './cstr.ts';
import {
  BayAct, Busy, CalState, DryState, editDefaults, editVendorCount, editVendorName, GROUP_CHOICE_NEW, GROUP_CHOICE_NONE,
  groupEditReason, groupNextName, MATERIALS, Sev, SlotState, slotGroup, slotId, UnitAct,
} from './model.ts';
import type { Model, Unit } from './model.ts';
import { format, str } from './strings.ts';
import type { ActionResult } from './types.ts';

// ---------------------------------------------------------------- results

/** What a mapper says about a line: an answer, a refusal with its reason, or
 *  "not mine" (the C's +1, -1 and 0). */
type Mapped =
  | { rc: 1; script: string }
  | { rc: -1; why: string }
  | { rc: 0 };

const NOT_MINE: Mapped = { rc: 0 };
const refuse = (why: string): Mapped => ({ rc: -1, why });

// ---------------------------------------------------------------- scanning

/** The few `sscanf` conversions the action lines use: a word (at most `max`
 *  characters, as `%31s`), an integer (`%d`), and a literal. */
class Scanner {
  pos = 0;
  private readonly text: string;
  constructor(text: string) {
    this.text = text;
  }

  private skipSpace(): void {
    while (this.pos < this.text.length && /\s/.test(this.text[this.pos])) this.pos++;
  }

  /** `%<max>s`: skips space, reads up to `max` non-space characters. */
  word(max: number): string | null {
    this.skipSpace();
    const start = this.pos;
    while (this.pos < this.text.length && !/\s/.test(this.text[this.pos]) && this.pos - start < max) this.pos++;
    return this.pos > start ? this.text.slice(start, this.pos) : null;
  }

  /** `%d`: skips space, reads an optionally signed integer. */
  int(): number | null {
    this.skipSpace();
    const m = /^[+-]?\d+/.exec(this.text.slice(this.pos));
    if (!m) return null;
    this.pos += m[0].length;
    return Number(m[0]);
  }

  /** A literal in the format; a space before it lets whitespace precede it. */
  literal(lit: string, skipSpaceFirst: boolean): boolean {
    if (skipSpaceFirst) this.skipSpace();
    if (!this.text.startsWith(lit, this.pos)) return false;
    this.pos += lit.length;
    return true;
  }

  atEnd(): boolean {
    return this.pos === this.text.length;
  }
}

/** The text after the leading `verb ` of `line`, or null when the line is not
 *  that verb followed by a space. */
function rest(line: string, verb: string): string | null {
  return line.startsWith(`${verb} `) ? line.slice(verb.length + 1) : null;
}

// ---------------------------------------------------------------- lookups

/** The (unit, slot) whose display id ("oams11") is `id`. */
function slotFind(m: Model, id: string): { unit: number; slot: number } | null {
  for (let u = 0; u < m.units.length; u++) {
    for (let s = 0; s < m.units[u].slots.length; s++) {
      if (slotId(m, u, s) === id) return { unit: u, slot: s };
    }
  }
  return null;
}

const unitFind = (m: Model, name: string): number => m.units.findIndex((u) => u.name === name);

/** The group index named `name`, or -1. */
const groupFind = (m: Model, name: string): number => m.groups.findIndex((g) => g.name === name);

/** The FPS lane the host gave `unit`, or null. */
const unitLane = (m: Model, unit: number): string | null => m.units[unit]?.lane || null;

/** Whether a sequence runs on a toolhead: its lane is busy loading or
 *  unloading, or its stage plan has a current step that has not failed
 *  (sequence_runs() in actions_map.c). */
function sequenceRuns(busy: number, stepCount: number, stepCurrent: number, stepFailed: number): boolean {
  return busy !== Busy.NONE || (stepCount > 0 && stepCurrent >= 0 && stepCurrent < stepCount && stepFailed < 0);
}

/** The lane a `stop` line stops (stop_resolve() in actions_map.c). */
function stopResolve(line: string, m: Model):
  { rc: 1; lane: string } | { rc: -1; why: string } | { rc: 0 } {
  if (line === 'stop') {
    const unit = m.busyUnit >= 0 ? m.busyUnit : m.thisUnit;
    const own = m.units[m.thisUnit];
    const th = own && own.toolhead >= 0 ? m.toolheads[own.toolhead] : undefined;
    const runs = m.busy !== Busy.NONE || (th ? sequenceRuns(Busy.NONE, th.steps.length, th.stepCurrent, th.stepFailed) : false);
    if (!runs) return { rc: -1, why: str('REASON_NOTHING_BUSY') };
    const lane = unitLane(m, unit);
    if (!lane) return { rc: -1, why: str('REASON_LANE_UNKNOWN') };
    return { rc: 1, lane };
  }
  const arg = rest(line, 'stop');
  if (arg !== null) {
    const idx = toolheadFind(m, arg);
    if (idx < 0) return { rc: 0 };
    const t = m.toolheads[idx];
    if (!sequenceRuns(t.busy, t.steps.length, t.stepCurrent, t.stepFailed)) {
      return { rc: -1, why: str('REASON_NOTHING_BUSY') };
    }
    return { rc: 1, lane: t.id };
  }
  return { rc: 0 };
}

/** The toolhead whose id (the FPS lane) is `id`, or -1. */
const toolheadFind = (m: Model, id: string): number => m.toolheads.findIndex((t) => t.id === id);

const unitHostIdx = (m: Model, unit: number): number => m.units[unit]?.hostIdx ?? -1;

/** Whether the toolhead of `unit` is busy loading or unloading this bay. */
function busyHere(m: Model, unit: number, slot: number): boolean {
  const th = m.toolheads[m.units[unit].toolhead];
  return th !== undefined && th.busy !== Busy.NONE && th.busyUnit === unit && th.busySlot === slot;
}

/** What holds a unit's motion, asked before any state predicate, in this
 *  order: the unit is offline; a stop fault is latched on its lane (the host's
 *  own words); the unit is online with no stop fault and the host says it is
 *  not `ready`, a unit that cannot move filament at all. A unit that is ready
 *  is never called unsupported. Returns the reason, or null. */
function notReady(m: Model, unit: number): string | null {
  const u = m.units[unit];
  const th = m.toolheads[u.toolhead];
  if (!u.connected) return str('HS_OFFLINE');
  if (th !== undefined && th.hasError && th.errorSeverity === Sev.STOP) return th.errorText || str('LABEL_FAULT');
  return u.readyKnown && !u.ready ? str('REASON_NOT_SUPPORTED') : null;
}

/** What the host says this bay can do, applied after the model's own state
 *  predicates so the more specific reason ("already loaded") still wins: a bay
 *  list that lacks the action's letter withholds it. `act` is one BayAct bit.
 *  The host's list is what is allowed now, so a letter can be missing because
 *  of the bay's state and not for lack of the ability: calibrate is the one
 *  action the display does not already refuse by state, so it says the state
 *  when there is one, and "not supported" is for a bay the host would
 *  otherwise let through. Returns the reason, or null when the action may go. */
function bayWithheld(m: Model, unit: number, slot: number, act: number): string | null {
  const u = m.units[unit];
  const s = u.slots[slot];
  if (!s.actsKnown || (s.acts & act) !== 0) return null;
  if (act === BayAct.LOAD) {
    const th = m.toolheads[u.toolhead];
    const laneIdle = th === undefined || (th.busy === Busy.NONE && !th.loaded);
    if (s.state === SlotState.READY && laneIdle && s.calState === CalState.UNCALIBRATED
        && (s.acts & BayAct.CALIBRATE) !== 0) return str('REASON_CALIBRATE_FIRST');
  }
  if (act === BayAct.CALIBRATE) {
    const th = m.toolheads[u.toolhead];
    if (busyHere(m, unit, slot)) return str('REASON_BUSY_HERE');
    if (s.state === SlotState.EMPTY) return str('REASON_BAY_EMPTY');
    if (s.state === SlotState.ERROR) return str('REASON_BAY_ERROR');
    if (u.slots.some((x) => x.positioning)) return str('REASON_POSITIONING');
    if (s.state === SlotState.LOADED) return str('REASON_ALREADY_LOADED');
    if (th !== undefined && (th.busy !== Busy.NONE || th.loaded)) return str('REASON_GROUP_BUSY');
  }
  return str('REASON_NOT_NOW');
}

/** A unit action `act` lists, or does not (UnitAct bit). */
const unitMay = (m: Model, unit: number, act: number): boolean => {
  const u = m.units[unit];
  return !u.actKnown || (u.acts & act) !== 0;
};

/** Why a unit cannot dry, in the current language: '' when it can. A unit whose
 *  `act` lacks `dryer_start` is worded here, live, so a language change reaches
 *  it; any other reason is the host's own words (cannotDryReason).
 *  actions_dry_reason() in the C. */
export function dryReason(u: Unit): string {
  if (u.dryUnlisted) return str(u.hasPowerAdapter && !u.powerAdapter ? 'ALERT_ADAPTER' : 'REASON_NOT_SUPPORTED');
  return u.cannotDryReason;
}

// ----------------------------------------------------------------- writer

/** The host commands being built: one per line, and a command that does not
 *  fit the buffer refuses the line rather than truncating it. */
class Writer {
  private text = '';
  private readonly capacity: number;
  over = false;
  constructor(capacity: number) {
    this.capacity = capacity;
  }

  /** Append one command; a newline joins it to what came before. */
  cmd(command: string): void {
    if (this.over) return;
    const tmp = cut(command, 128);   // char tmp[128] in the C
    const add = (this.text ? '\n' : '') + tmp;
    if (this.text.length + add.length + 1 > this.capacity) {
      this.over = true;
      return;
    }
    this.text += add;
  }

  get script(): string {
    return this.text;
  }
}

// --------------------------------------------------------------- the table

/** `load <slot>`: OPENAMS_LOAD wants the group by name and the bay by the
 *  host's own id, so a bay outside a group, or one the host has not identified,
 *  cannot be addressed. What the bay's state allows is decided here too. */
function loadGcode(m: Model, id: string): { rc: 1; cmd: string } | { rc: -1; why: string } | null {
  const found = slotFind(m, id);
  if (!found) return null;
  const slot = m.units[found.unit].slots[found.slot];

  const idle = notReady(m, found.unit);
  if (idle !== null) return { rc: -1, why: idle };
  if (busyHere(m, found.unit, found.slot)) return { rc: -1, why: str('REASON_BUSY_HERE') };
  switch (slot.state) {
    case SlotState.LOADED: return { rc: -1, why: str('REASON_ALREADY_LOADED') };
    case SlotState.ERROR: return { rc: -1, why: str('REASON_BAY_ERROR') };
    case SlotState.EMPTY: return { rc: -1, why: str('REASON_BAY_EMPTY') };
    default: break;
  }
  if (slot.positioning) return { rc: -1, why: str('REASON_POSITIONING') };

  const gi = slotGroup(m, found.unit, found.slot);
  if (gi < 0) return { rc: -1, why: str('REASON_NOT_GROUPED') };
  if (slot.globalId < 0) return { rc: -1, why: str('REASON_NO_HOST_ID') };
  const withheld = bayWithheld(m, found.unit, found.slot, BayAct.LOAD);
  if (withheld !== null) return { rc: -1, why: withheld };

  return { rc: 1, cmd: cut(`OPENAMS_LOAD GROUP=${m.groups[gi].name} SLOT=${slot.globalId}`, 64) };
}

/** `setting <key> on|off`: the name the host knows `key` by. The five real
 *  keys map to themselves; the two legacy display spellings are translated;
 *  any other key the display keeps to itself (null). */
function optionName(key: string): string | null {
  const names: Record<string, string> = {
    'read-on-insertion': 'read_tag_on_insertion',
    'read-on-startup': 'read_tags_at_startup',
    read_tag_on_insertion: 'read_tag_on_insertion',
    read_tags_at_startup: 'read_tags_at_startup',
    auto_reload_on_runout: 'auto_reload_on_runout',
    auto_create_spoolman_spools: 'auto_create_spoolman_spools',
    apply_pa_on_load: 'apply_pa_on_load',
  };
  return Object.hasOwn(names, key) ? names[key] : null;
}

function dispatch(w: Writer, line: string, m: Model): Mapped {
  let arg: string | null;

  // `pause and load <slot>`: pause first, then the same load the home tile
  // sends, in one buffer so a load the model cannot address refuses the whole
  // line instead of leaving a bare PAUSE behind.
  if ((arg = rest(line, 'pause and load')) !== null) {
    const r = loadGcode(m, arg);
    if (!r) return NOT_MINE;
    if (r.rc !== 1) return refuse(r.why);
    w.cmd('PAUSE');
    w.cmd(r.cmd);
    return { rc: 1, script: '' };
  }

  if ((arg = rest(line, 'load')) !== null) {
    const r = loadGcode(m, arg);
    if (!r) return NOT_MINE;
    if (r.rc !== 1) return refuse(r.why);
    w.cmd(r.cmd);
    return { rc: 1, script: '' };
  }

  // `unload <slot>`: the lane, not the bay, is what OPENAMS_UNLOAD takes, and
  // only a bay that is loaded (and not busy) can be unloaded. `unload <toolhead
  // id>` addresses the lane directly when the argument is no bay of the model.
  if ((arg = rest(line, 'unload')) !== null) {
    const found = slotFind(m, arg);
    if (found) {
      const idle = notReady(m, found.unit);
      if (idle !== null) return refuse(idle);
      if (busyHere(m, found.unit, found.slot)) return refuse(str('REASON_BUSY_HERE'));
      if (m.units[found.unit].slots[found.slot].state !== SlotState.LOADED) return refuse(str('REASON_NOT_LOADED'));
      const lane = unitLane(m, found.unit);
      if (!lane) return refuse(str('REASON_LANE_UNKNOWN'));
      const withheld = bayWithheld(m, found.unit, found.slot, BayAct.UNLOAD);
      if (withheld !== null) return refuse(withheld);
      w.cmd(`OPENAMS_UNLOAD FPS=${lane}`);
      return { rc: 1, script: '' };
    }
    const th = toolheadFind(m, arg);
    if (th < 0) return NOT_MINE;
    if (!m.toolheads[th].loaded) return refuse(str('REASON_NO_FILAMENT'));
    // the lane's unload is the loaded bay's unload
    const lt = m.toolheads[th];
    if (lt.loadedUnit >= 0 && lt.loadedUnit < m.units.length
        && lt.loadedSlot >= 0 && lt.loadedSlot < m.units[lt.loadedUnit].slots.length) {
      const reason = notReady(m, lt.loadedUnit) ?? bayWithheld(m, lt.loadedUnit, lt.loadedSlot, BayAct.UNLOAD);
      if (reason !== null) return refuse(reason);
    }
    w.cmd(`OPENAMS_UNLOAD FPS=${m.toolheads[th].id}`);
    return { rc: 1, script: '' };
  }

  // `stop` / `stop <toolhead id>` (UNIFIED_UI 4g): stops whatever sequence runs
  // on the lane. With the host's own stop (the component's `stop`) the line is
  // the RPC server.openams.stop, not G-code.
  {
    const r = stopResolve(line, m);
    if (r.rc === -1) return refuse(r.why);
    if (r.rc === 1) {
      if (m.spoolmanStop) return NOT_MINE;      // actionsRpcMapped() has it
      w.cmd(`OAMSM_LOAD_FILAMENT_CANCEL FPS=${r.lane}`);
      return { rc: 1, script: '' };
    }
  }

  if ((arg = rest(line, 'reread')) !== null) {
    const found = slotFind(m, arg);
    if (!found) return NOT_MINE;
    if (m.units[found.unit].slots[found.slot].state === SlotState.EMPTY) return refuse(str('REASON_BAY_EMPTY'));
    const idx = unitHostIdx(m, found.unit);
    if (idx < 0) return refuse(str('REASON_UNIT_UNKNOWN'));
    if (!unitMay(m, found.unit, UnitAct.RFID_SCAN)) return refuse(str('REASON_NOT_SUPPORTED'));
    w.cmd(`OAMS_RFID_SCAN OAMS=${idx}`);
    return { rc: 1, script: '' };
  }

  // SPOOL= here is the bay index inside OAMS=<host idx>.
  if ((arg = rest(line, 'calibrate')) !== null) {
    const found = slotFind(m, arg);
    if (!found) return NOT_MINE;
    const idle = notReady(m, found.unit);
    if (idle !== null) return refuse(idle);
    const idx = unitHostIdx(m, found.unit);
    if (idx < 0) return refuse(str('REASON_UNIT_UNKNOWN'));
    const withheld = bayWithheld(m, found.unit, found.slot, BayAct.CALIBRATE);
    if (withheld !== null) return refuse(withheld);
    w.cmd(`OAMS_CALIBRATE_PTFE_LENGTH OAMS=${idx} SPOOL=${found.slot}`);
    return { rc: 1, script: '' };
  }

  // `dry <unit> target|hours|preset ...` only edits the pending display values,
  // so only start and stop reach the host.
  if ((arg = rest(line, 'dry')) !== null) {
    let s = new Scanner(arg);
    const a = s.word(39);
    if (a !== null && s.literal('start', true)) {
      const target = s.int();
      if (target !== null && s.literal('C', false)) {
        const hours = s.int();
        if (hours !== null && s.literal('h', false) && s.atEnd()) {
          const unit = unitFind(m, a);
          if (unit < 0) return NOT_MINE;
          const idx = unitHostIdx(m, unit);
          if (idx < 0) return refuse(str('REASON_UNIT_UNKNOWN'));
          if (!unitMay(m, unit, UnitAct.DRYER_START)) return refuse(dryReason(m.units[unit]));
          w.cmd(`OAMS_DRYER_START OAMS=${idx} TARGET=${target} DURATION=${(hours * 3600) | 0}`);
          return { rc: 1, script: '' };
        }
      }
    }

    s = new Scanner(arg);
    const b = s.word(39);
    if (b !== null && s.literal('stop', true) && s.atEnd()) {
      const unit = unitFind(m, b);
      if (unit < 0) return NOT_MINE;
      if (m.units[unit].dryState === DryState.IDLE) return refuse(str('REASON_DRYER_IDLE'));
      const idx = unitHostIdx(m, unit);
      if (idx < 0) return refuse(str('REASON_UNIT_UNKNOWN'));
      if (!unitMay(m, unit, UnitAct.DRYER_STOP)) return refuse(str('REASON_NOT_SUPPORTED'));
      w.cmd(`OAMS_DRYER_STOP OAMS=${idx}`);
      return { rc: 1, script: '' };
    }
    return NOT_MINE;
  }

  if ((arg = rest(line, 'setting')) !== null) {
    const s = new Scanner(arg);
    const key = s.word(39);
    const val = s.word(7);
    if (key === null || val === null) return NOT_MINE;
    let value: number;
    if (val === 'on') value = 1;
    else if (val === 'off') value = 0;
    else return NOT_MINE;
    const name = optionName(key);
    if (!name) return NOT_MINE;   // a key the display keeps to itself
    w.cmd(`OAMSM_SET_OPTION NAME=${name} VALUE=${value}`);
    return { rc: 1, script: '' };
  }

  // Both spellings exist: `create group <name>` and the model's `group create <name>`.
  arg = rest(line, 'create group') ?? rest(line, 'group create');
  if (arg !== null) {
    const name = new Scanner(arg).word(39);
    if (name === null) return NOT_MINE;
    w.cmd(`OAMSM_CREATE_GROUP GROUP=${name}`);
    return { rc: 1, script: '' };
  }

  // `delete group <name>`: the groups list's own action. The name has to be a
  // group the model holds, and the host refuses to delete one its lane is
  // using - so it is the same predicate the view dims the action by.
  arg = rest(line, 'delete group') ?? rest(line, 'group delete');
  if (arg !== null) {
    const name = new Scanner(arg).word(39);
    if (name === null) return NOT_MINE;
    const gi = groupFind(m, name);
    if (gi < 0) return NOT_MINE;
    const whyGroup = groupEditReason(m, gi);
    if (whyGroup) return refuse(whyGroup);
    w.cmd(`OAMSM_DELETE_GROUP GROUP=${name}`);
    return { rc: 1, script: '' };
  }

  // `change group <choice> <slot>`: the display's "Change group..." form
  // submits. The choice is an index into the group's own table, or one of the
  // two sentinels the view writes for the other two rows of the list, so the
  // line stays short and the mapper is the one that knows what the host calls
  // each command.
  arg = rest(line, 'change group');
  if (arg !== null) {
    const s = new Scanner(arg);
    const choice = s.int();
    const id = choice === null ? null : s.word(39);
    if (choice === null || id === null) return NOT_MINE;
    const found = slotFind(m, id);
    if (!found) return NOT_MINE;
    const gi = slotGroup(m, found.unit, found.slot);
    const oams = m.units[found.unit].name;

    // The lane's own rules stop every change first, whatever the choice: a
    // load/unload or a runout in progress, or the bay's group being the one
    // the lane is running.
    const whyDonor = groupEditReason(m, gi);
    if (whyDonor) return refuse(whyDonor);

    if (choice === GROUP_CHOICE_NONE) {
      if (gi < 0) return refuse(str('REASON_NOT_GROUPED'));
      w.cmd(`OAMSM_UNASSIGN_BAY GROUP=${m.groups[gi].name} OAMS=${oams} BAY=${found.slot}`);
      return { rc: 1, script: '' };
    }
    if (choice === GROUP_CHOICE_NEW) {
      // A new group and the bay that fills it, in one script: the host takes
      // them in order, so the bay is never in a group that does not exist.
      const name = groupNextName(m);
      w.cmd(`OAMSM_CREATE_GROUP GROUP=${name}`);
      w.cmd(`OAMSM_ASSIGN_BAY GROUP=${name} OAMS=${oams} BAY=${found.slot}`);
      return { rc: 1, script: '' };
    }
    if (choice < 0 || choice >= m.groups.length) return NOT_MINE;
    // And the destination: changing the loaded group's membership would change
    // what the lane loads next.
    if (choice !== gi) {
      const whyGroup = groupEditReason(m, choice);
      if (whyGroup) return refuse(whyGroup);
    }
    w.cmd(`OAMSM_ASSIGN_BAY GROUP=${m.groups[choice].name} OAMS=${oams} BAY=${found.slot}`);
    return { rc: 1, script: '' };
  }

  // `link <slot> <spool_id>` (and the older `assign spool`): a Spoolman spool
  // link. Checked before `assign` so the two-word prefix does not eat the
  // first token.
  for (const verb of ['link', 'assign spool']) {
    if ((arg = rest(line, verb)) === null) continue;
    const s = new Scanner(arg);
    const id = s.word(39);
    const spool = id === null ? null : s.int();
    if (id === null || spool === null) return NOT_MINE;
    const found = slotFind(m, id);
    if (!found) return NOT_MINE;
    w.cmd(`OAMSM_SET_BAY_SPOOL OAMS=${m.units[found.unit].name} BAY=${found.slot} SPOOL=${spool} SOURCE=manual`);
    return { rc: 1, script: '' };
  }

  if ((arg = rest(line, 'assign')) !== null) {
    const s = new Scanner(arg);
    const group = s.word(39);
    const id = group === null ? null : s.word(39);
    if (group === null || id === null) return NOT_MINE;
    const found = slotFind(m, id);
    if (!found) return NOT_MINE;
    w.cmd(`OAMSM_ASSIGN_BAY GROUP=${group} OAMS=${m.units[found.unit].name} BAY=${found.slot}`);
    return { rc: 1, script: '' };
  }

  if ((arg = rest(line, 'unassign')) !== null) {
    const found = slotFind(m, arg);
    if (!found) return NOT_MINE;
    const gi = slotGroup(m, found.unit, found.slot);
    if (gi < 0) return refuse(str('REASON_NOT_GROUPED'));
    w.cmd(`OAMSM_UNASSIGN_BAY GROUP=${m.groups[gi].name} OAMS=${m.units[found.unit].name} BAY=${found.slot}`);
    return { rc: 1, script: '' };
  }

  if ((arg = rest(line, 'unlink')) !== null) {
    const found = slotFind(m, arg);
    if (!found) return NOT_MINE;
    if (m.units[found.unit].slots[found.slot].spoolId < 0) return refuse(str('REASON_NO_LINK'));
    w.cmd(`OAMSM_CLEAR_BAY_SPOOL OAMS=${m.units[found.unit].name} BAY=${found.slot}`);
    return { rc: 1, script: '' };
  }

  if (line === 'fault clear_errors') {
    w.cmd('OAMSM_CLEAR_ERRORS');
    return { rc: 1, script: '' };
  }

  if ((arg = rest(line, 'fault clear_fault')) !== null) {
    const unit = unitFind(m, arg);
    if (unit < 0) return NOT_MINE;
    const idx = unitHostIdx(m, unit);
    if (idx < 0) return refuse(str('REASON_UNIT_UNKNOWN'));
    if (!unitMay(m, unit, UnitAct.CLEAR_FAULT)) return refuse(str('REASON_NOT_SUPPORTED'));
    w.cmd(`OAMS_CLEAR_FAULT OAMS=${idx}`);
    return { rc: 1, script: '' };
  }

  if ((arg = rest(line, 'fault retry_load')) !== null) {
    const group = new Scanner(arg).word(39);
    if (group === null) return NOT_MINE;
    w.cmd(`OAMSM_LOAD_FILAMENT GROUP=${group}`);
    return { rc: 1, script: '' };
  }

  // `fault resume` and the older bare `resume` from the alert buttons.
  if (line === 'fault resume' || line === 'resume') {
    w.cmd('RESUME');
    return { rc: 1, script: '' };
  }

  return NOT_MINE;
}

/**
 * Map `line` to G-code against the model: the script (rc 1), a refusal with
 * its reason (rc -1: the model cannot address it, or the state does not allow
 * it), or not a G-code line (rc 0). `capacity` is the size of the caller's
 * output buffer (the view's dry run uses 128, core_action 512).
 */
export function actionsMap(line: string, m: Model, capacity = 512): Mapped {
  const w = new Writer(capacity);
  const r = dispatch(w, line, m);
  if (r.rc === 1) {
    return w.over ? refuse(str('REASON_CMD_TOO_LONG')) : { rc: 1, script: w.script };
  }
  return r;
}

/** What the RPC mapper says about a line: a call, a refusal with its reason, or
 *  "not mine" (actions_rpc_why's +1, -1 and 0). `then` is the line the caller
 *  sends once the host has taken the call, and only then. */
type RpcMapped =
  | { rc: 1; method: string; params: Record<string, unknown>; then?: string }
  | { rc: -1; why: string }
  | { rc: 0 };

/** The `key=value` tokens at the head of a `spool edit` line's fields, and the
 *  text that follows them. actions_map.c's edit_token stops at the first token
 *  of another shape, and only `then load` may follow; null for a head that
 *  breaks the shape. */
function editTokens(text: string): { tokens: [string, string][]; tail: string } | null {
  const words = text.split(' ').filter((w) => w.length > 0);
  const shaped = (w: string): boolean => {
    const eq = w.indexOf('=');

    return eq > 0 && eq !== w.length - 1 && eq <= 15 && w.length - eq - 1 <= 23;
  };
  let n = 0;

  while (n < words.length && shaped(words[n])) n++;
  return {
    tokens: words.slice(0, n).map((w) => [w.slice(0, w.indexOf('=')), w.slice(w.indexOf('=') + 1)] as [string, string]),
    tail: words.slice(n).join(' '),
  };
}

const EDIT_INT = /^[+-]?\d+$/;
// The dot introduces the fraction, so the two digit runs cannot trade digits
// (an optional dot between two \d* runs backtracks super-linearly).
const EDIT_NUM = /^([+-]?)(\d*)(?:\.(\d*))?$/;

/** A plain decimal as whole thousandths (edit_milli in C): the fourth decimal
 *  rounds half up and nothing past it counts; no floating point is involved. */
function editMilli(text: string): number | null {
  const m = EDIT_NUM.exec(text);
  const fraction = m?.[3] ?? '';
  if (!m || (m[2].length === 0 && fraction.length === 0)) return null;
  const whole = Math.min(Number(m[2] || '0'), 1001);   // past 1000 it only has to stay past it
  const frac = Number((fraction + '000').slice(0, 3));
  const up = fraction.length > 3 && fraction[3] >= '5' ? 1 : 0;
  const v = whole * 1000 + frac + up;
  return m[1] === '-' && v !== 0 ? -v : v;
}

/** `spool edit <slot> [material=<n>] [color=<rgb>] [vendor=<n>] [remaining=<g>]
 *  [initial=<g>] [pa=<x>] [then load]` -> server.openams_spoolman.edit
 *  (actions_map.c's spool_edit). The selects' values are the form's own:
 *  material and vendor are indices into the lists the edit form offers (an
 *  index one past the list is the bay's own), color is 0xRRGGBB as a decimal
 *  number or -1 for "none", the weights are grams and pa is the pressure
 *  advance itself. A key left out is a field left as it is, and so is one equal
 *  to what the form starts from (editDefaults): only a change is sent. The
 *  line never carries a vendor's name, only its index, so it has one shape for
 *  both renderers and needs no quoting.
 *
 *  ` then load` is the one thing that may follow: the line a "Load" on an
 *  unidentified bay sends, which saves what the user chose and loads in one go.
 *  It comes back as `then`, for the caller to send once the host has taken the
 *  edit. A material of -1 is that form's unanswered "Choose a material", and is
 *  refused for that, in its own words. */
function spoolEdit(args: string, m: Model): RpcMapped {
  const words = args.replace(/^ +/, '');
  const sp = words.indexOf(' ');
  const id = sp < 0 ? words : words.slice(0, sp);
  if (id.length === 0 || id.length >= 40) return { rc: 0 };
  const found = slotFind(m, id);
  if (!found) return { rc: 0 };

  const split = editTokens(sp < 0 ? '' : words.slice(sp + 1));
  if (!split) return { rc: 0 };
  const tokens = split.tokens;
  let thenLoad = false;

  if (split.tail) {
    if (split.tail !== 'then load') return { rc: 0 };
    thenLoad = true;
  }
  const given: Record<string, number> = {};
  for (const [key, val] of tokens) {
    if (key === 'pa') {
      const milli = editMilli(val);
      if (milli === null) return { rc: 0 };
      given[key] = milli;
      continue;
    } else if (['material', 'color', 'vendor', 'remaining', 'initial'].includes(key)) {
      if (!EDIT_INT.test(val)) return { rc: 0 };
    } else {
      return { rc: 0 };
    }
    given[key] = Number(val);
  }
  const has = (k: string): boolean => k in given;

  // what the bay can do now: the view's dimming and this refusal in one
  // a load the host does not offer must not follow a save, or a spool would be
  // saved for a load that cannot happen: a unit that is not ready says so
  // first, a bay list without `l` once the bay itself is known to be there
  if (thenLoad) {
    const idle = notReady(m, found.unit);
    if (idle !== null) return { rc: -1, why: idle };
  }
  if (!m.spoolmanEdit) return { rc: -1, why: str('REASON_EDIT_NO_SPOOLMAN') };
  if (!m.spoolmanOnline) return { rc: -1, why: str('REASON_SPOOLMAN_OFFLINE') };
  const s = m.units[found.unit].slots[found.slot];
  if (s.state === SlotState.EMPTY) return { rc: -1, why: str('REASON_BAY_EMPTY') };
  if (thenLoad) {
    const withheld = bayWithheld(m, found.unit, found.slot, BayAct.LOAD);
    if (withheld !== null) return { rc: -1, why: withheld };
  }
  if (thenLoad && has('material') && given.material === -1) {
    return { rc: -1, why: str('MATERIAL_CHOOSE') };
  }

  const d = editDefaults(m, found.unit, found.slot);
  const create = s.spoolId < 0;
  const range = { rc: -1, why: str('REASON_EDIT_RANGE') } as const;

  const effRemaining = has('remaining') ? given.remaining : d.remainingG;
  const effInitial = has('initial') ? given.initial : d.initialG;
  const paX1000 = has('pa') ? Math.min(given.pa, 100000) : d.paX1000;
  if (effRemaining < 0 || effInitial <= 0 || effRemaining > 100000 || effInitial > 100000 ||
      paX1000 < 0 || paX1000 > 2000 || (has('color') && (given.color < -1 || given.color > 0xffffff))) {
    return range;
  }

  const params: Record<string, unknown> = { unit: m.units[found.unit].name, bay: found.slot };

  // an index one past the list is the bay's own, which is no change (and a bay
  // with none of its own has no such choice)
  const vendorOwn = editVendorCount(m);
  if ((has('material') && (given.material < 0 || given.material > MATERIALS.length ||
        (given.material === MATERIALS.length && !s.material))) ||
      (has('vendor') && (given.vendor < 0 || given.vendor > vendorOwn || (given.vendor === vendorOwn && !s.brand)))) {
    return { rc: -1, why: str('REASON_EDIT_STALE') };
  }

  // a new spool needs its material, so it is always sent with one
  if (create || (has('material') && given.material !== MATERIALS.length && given.material !== d.material)) {
    const k = has('material') ? given.material : d.material;
    params.material = k === MATERIALS.length ? s.material : MATERIALS[k];
  }
  if (has('color') && given.color >= 0 && given.color !== d.color) {
    params.color_hex = given.color.toString(16).toUpperCase().padStart(6, '0');
  }
  if (has('vendor') && given.vendor !== vendorOwn && given.vendor !== d.vendor) {
    params.vendor = editVendorName(m, given.vendor);
  }
  if ((has('remaining') && given.remaining !== d.remainingG) || (has('initial') && given.initial !== d.initialG)) {
    if (effRemaining > effInitial) return { rc: -1, why: str('REASON_EDIT_EXCEEDS') };
    if (has('remaining') && given.remaining !== d.remainingG) params.remaining_weight = given.remaining;
    if (has('initial') && given.initial !== d.initialG) params.initial_weight = given.initial;
  }
  if (has('pa') && paX1000 !== d.paX1000) params.flow_k = paX1000 / 1000;

  // the unit and bay alone are no edit; a new spool is
  if (!create && Object.keys(params).length <= 2) return { rc: -1, why: str('EDIT_NO_CHANGES') };
  // what the caller sends once the host has taken this: the load ` then load`
  // asked for, and nothing at all without it.
  return thenLoad
    ? { rc: 1, method: 'server.openams_spoolman.edit', params, then: `load ${id}` }
    : { rc: 1, method: 'server.openams_spoolman.edit', params };
}

/** The RPC vocabulary: `spool list`, `vendor list`, `spool edit <slot> key=value
 *  ...`, `spool confirm <slot> <grams>` and `job metadata <file>`, with the
 *  refusals told apart (actions_rpc_why in C). */
export function actionsRpcMapped(line: string, m: Model): RpcMapped {
  // `stop` with the host's own stop -> server.openams.stop {lane}
  if (m.spoolmanStop) {
    const r = stopResolve(line, m);
    if (r.rc === -1) return { rc: -1, why: r.why };
    if (r.rc === 1) return { rc: 1, method: 'server.openams.stop', params: { lane: r.lane } };
  }

  if (line === 'spool list') {
    return { rc: 1, method: 'server.spoolman.proxy', params: { request_method: 'GET', path: '/v1/spool' } };
  }

  if (line === 'vendor list') {
    return { rc: 1, method: 'server.spoolman.proxy', params: { request_method: 'GET', path: '/v1/vendor' } };
  }

  const edit = rest(line, 'spool edit');
  if (edit !== null) return spoolEdit(edit, m);

  if (line.startsWith('spool confirm ')) {
    const s = new Scanner(line.slice(14));
    const id = s.word(39);
    const grams = id === null ? null : s.int();
    if (id === null || grams === null) return { rc: 0 };
    const found = slotFind(m, id);
    if (!found) return { rc: 0 };
    return {
      rc: 1,
      method: 'server.openams_spoolman.confirm',
      params: { bay: `${m.units[found.unit].name}-${found.slot}`, remaining_g: grams },
    };
  }

  if (line.startsWith('job metadata ')) {
    const filename = line.slice(13);
    if (!filename) return { rc: 0 };
    return { rc: 1, method: 'server.files.metadata', params: { filename } };
  }

  return { rc: 0 };
}

/** The RPC call a line maps to, or null (a refusal is not a call). */
export function actionsRpc(line: string, m: Model): { method: string; params: Record<string, unknown> } | null {
  const r = actionsRpcMapped(line, m);
  return r.rc === 1 ? { method: r.method, params: r.params } : null;
}

/**
 * The one predicate: whether `line` can run now, and if not why not. A line
 * neither mapper recognizes is reported enabled with no reason, rather than
 * hiding a real action behind a false negative.
 */
export function actionsCheck(line: string, m: Model): { enabled: boolean; why: string } {
  const r = actionsMap(line, m, 128);
  if (r.rc === 1) return { enabled: true, why: '' };
  if (r.rc === -1) return { enabled: false, why: r.why };

  // Not a G-code line: the RPC vocabulary next.
  const rpc = actionsRpcMapped(line, m);
  if (rpc.rc === -1) {
    // "No changes to save" is about this submission, not about the action: the
    // form's own starting values are always that, and the action is still there
    // to open.
    if (rpc.why === str('EDIT_NO_CHANGES')) return { enabled: true, why: '' };
    return { enabled: false, why: rpc.why };
  }
  return { enabled: true, why: '' };
}

// ------------------------------------------------------------ core_action

/** C's `%g` for a double (six significant digits, no trailing zeros). */
function formatG(d: number): string {
  if (d === 0) return '0';
  const exp = Math.floor(Math.log10(Math.abs(d)));
  if (exp < -4 || exp >= 6) {
    const [mant, e] = d.toExponential(5).split('e');
    const trimmed = mant.includes('.') ? mant.replace(/\.?0+$/, '') : mant;
    const sign = e.startsWith('-') ? '-' : '+';
    return `${trimmed}e${sign}${e.replace(/^[+-]/, '').padStart(2, '0')}`;
  }
  const fixed = d.toPrecision(6);
  return fixed.includes('.') ? fixed.replace(/\.?0+$/, '') : fixed;
}

/** One form value as text: strings as they are, whole numbers without a
 *  decimal point, booleans as 1 and 0; null when it is none of those. */
function formText(v: unknown): string | null {
  if (typeof v === 'string') return v;
  if (typeof v === 'number') return Number.isInteger(v) ? String(v) : formatG(v);
  if (typeof v === 'boolean') return v ? '1' : '0';
  return null;
}

/** Replace every `{key}` of `line` with the form's value for key. Returns the
 *  error reason when a value is missing or the result does not fit. */
function fillLine(line: string, form: Record<string, unknown> | null): { line: string } | { reason: string } {
  const LIMIT = 256;   // char resolved[256] in the C
  let out = '';
  let p = 0;
  while (p < line.length) {
    const end = line.indexOf('}', p);
    const key = line[p] === '{' && end >= 0 ? line.slice(p + 1, end) : null;
    if (key === null || key.length === 0 || key.length >= 32) {
      if (out.length + 1 >= LIMIT) return { reason: str('REASON_LINE_TOO_LONG') };
      out += line[p++];
      continue;
    }
    const value = formText(form ? form[key] : undefined);
    if (value === null) return { reason: format('REASON_FORM_MISSING', { key }, 160) };
    if (out.length + value.length >= LIMIT) return { reason: str('REASON_LINE_TOO_LONG') };
    out += value;
    p = end + 1;
  }
  return { line: out };
}

/** Resolve `line` (a form fills its placeholders) against the model and say
 *  where it goes: G-code, an RPC call, an error, or nothing to send. */
export function coreAction(m: Model, line: string, form: Record<string, unknown> | null | undefined): ActionResult {
  const filled = fillLine(line, form ?? null);
  if ('reason' in filled) return { kind: 'error', reason: filled.reason };

  const mapped = actionsMap(filled.line, m, 512);
  if (mapped.rc === 1) return { kind: 'gcode', script: mapped.script };
  if (mapped.rc === -1) return { kind: 'error', reason: mapped.why };

  const rpc = actionsRpcMapped(filled.line, m);
  if (rpc.rc === 1) {
    // `then` is what the host's answer releases, not the call itself: it goes
    // with the result so the caller can hold it back until then.
    return rpc.then
      ? { kind: 'rpc', method: rpc.method, params: rpc.params, then: rpc.then }
      : { kind: 'rpc', method: rpc.method, params: rpc.params };
  }
  if (rpc.rc === -1) return { kind: 'error', reason: rpc.why };
  return { kind: 'local' };
}

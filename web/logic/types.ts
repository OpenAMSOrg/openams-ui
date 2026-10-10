/**
 * Types for the OpenAMS logic's JS API and the view tree it returns
 * (docs/design/UNIFIED_UI.md 4, 8). The view shapes mirror src/view/view_json.c;
 * every string is final text from the core (src/view/strings.c).
 */

export type Severity = 'stop' | 'pause' | 'info';
export type Ink = 'light' | 'dark';
export type ActionStyle = 'primary' | 'normal' | 'danger';
export type TileState =
  | 'loaded' | 'ready' | 'empty' | 'positioning'
  | 'loading' | 'unloading' | 'error' | 'runout' | 'unknown';

/** The tone of a toolhead's message and of a tile's tag: red, yellow, plain gray. */
export type Tone = 'error' | 'info' | 'neutral';

export interface ViewConfirm {
  title: string;
  text: string;
  ok_label: string;
}

export interface ViewFieldOption {
  value: number;
  label: string;
  /** Only on a Spoolman spool option: its filament color as "#rrggbb", or null
   *  when the filament has none. Every other option has no `color` key. */
  color?: string | null;
}

export interface ViewField {
  id: string;
  label: string;
  kind: 'number' | 'select' | 'toggle';
  value: number;
  min: number;
  max: number;
  step: number;
  unit: string;
  /** Only on a `select` field. */
  options?: ViewFieldOption[];
  /** The field asks something only the person knows (an unidentified bay's
   *  "Choose a material"), so a form that carries one has nothing to send
   *  until it holds an answer. */
  required: boolean;
}

export interface ViewForm {
  fields: ViewField[];
  /** What the submit button says, or null for a form that has none: a
   *  renderer treats null as "behave as before". */
  submit_label: string | null;
  /** True when submitting the form as it stands would change nothing. */
  require_change: boolean;
}

export interface ViewAction {
  id: string;
  label: string;
  /** The action line; `{field_id}` placeholders are filled from the form by
   *  `core.action(line, form)`. */
  line: string;
  enabled: boolean;
  /** Why the action is disabled; "" when enabled. */
  reason: string;
  style: ActionStyle;
  /** What a click on the tile's parts does: the body loads or unloads, the ring
   *  edits the spool, the tag opens its details. "tile" on non-tile actions. */
  target: 'tile' | 'ring' | 'tag';
  confirm: ViewConfirm | null;
  form: ViewForm | null;
}

export interface ViewTile {
  slot_id: string;
  bay: number;
  tool: string | null;
  label: string;
  sublabel: string | null;
  spare: { position: number | null; count: number } | null;
  state: TileState;
  /** `detail` is a sentence for the tag's details popover; `code` the host's fault
   *  code behind an error tag ("" when none), shown small and secondary. */
  tag: { text: string; tone: Tone; detail: string; code: string } | null;
  /** "#rrggbb", or null when the color is unknown. */
  color: string | null;
  ink: Ink;
  gradient_top: string | null;
  gradient_bottom: string | null;
  /** The palette name nearest the color ("black"), for a meta line; "" when
   *  the color is unknown. */
  color_name: string;
  /** One flat, dimmed look: empty, unknown or offline. */
  dim: boolean;
  /** True exactly for a bay with no spool (empty) and for a tile of an offline unit. */
  hatched: boolean;
  name: string;
  material: string;
  brand: string;
  grams_text: string;
  pct: number;
  pct_text: string;
  low: boolean;
  rfid: boolean;
  pending_confirmation: boolean;
  calibrated: boolean;
  actions: ViewAction[];
}

export interface ViewUnit {
  id: string;
  /** The family ("AMS 2 Pro"). */
  title: string;
  /** The unit's config name ("oams1"). */
  subtitle: string;
  online: boolean;
  /** "Offline" when not online, else "". */
  status_text: string;
  env: { text: string } | null;
  dryer: {
    text: string;
    tone: 'heat' | 'hold' | 'cool' | 'fault' | 'off';
    adapter_missing: boolean;
  } | null;
  /** The unit's own alerts (its lane fault, the dryer's), or null. */
  alert: ViewAlertGroup | null;
  /** True only when nothing in this scope alerts at all, shown elsewhere or not: the slot rests on a check. */
  alert_clear: boolean;
  info: { serial: string; firmware: string; family: string } | null;
  actions: ViewAction[];
  bays: ViewTile[];
  /** Every group that holds one of this unit's bays, in order. */
  groups: ViewGroup[];
  /** This unit's bays that belong to no group, in bay order. */
  ungrouped_bays: ViewGroupMember[];
}

/** One bay of a group, as the groups list shows it. A bay of the unit the list
 *  is scoped to is named the way its own tile is ("Spool N") and carries its
 *  color, material and state; a bay of another unit is a backup that unit may
 *  fall back to, never something to manage from here, so it is named with its
 *  unit and dimmed, and still carries its color, material and state, which say
 *  whether the fallback would match and is ready (docs/GROUPS.md). */
export interface ViewGroupMember {
  slot_id: string;
  /** The bay's index in its own unit, so a renderer finds its tile. */
  bay: number;
  mine: boolean;
  dim: boolean;
  /** The one infinite spooling would load next. */
  spare: boolean;
  /** "Spool N", or "oams2 Spool N" for another unit's. */
  label: string;
  color: string | null;
  material: string;
  brand: string;
  /** Loaded / Ready / Empty / Error. */
  state: string;
  state_tone: Tone;
}

export interface ViewGroup {
  /** "T0": the tool the slicer loads by name. */
  name: string;
  /** Its membership may change now. */
  editable: boolean;
  /** "" when editable, else why not. */
  reason: string;
  members: ViewGroupMember[];
  /** The group's own actions ("Delete group"), dimmed with `reason`. */
  actions: ViewAction[];
}

export interface ViewToolhead {
  id: string;
  /** The extruder's display name ("DragonBurner"). */
  title: string;
  /** The FPS id as configured ("fps1"). */
  subtitle: string;
  /** The loaded tool: the hotend icon's overlay and tint only. */
  tool: { label: string; color: string | null; ink: Ink; outline: boolean } | null;
  pressure: {
    value: number;
    set_point: number;
    /** [low, high]: the band the unit's regulator keeps the pressure in, or
     *  null when the host does not say. Drawn on the bar, never judged: the
     *  bar stays neutral whatever the reading (UNIFIED_UI.md 4b). */
    band: [number, number] | null;
    /** "Pressure". */
    label: string;
    text: string;
    /** Tick labels under the bar: low, set point, high. */
    scale: [string, string, string];
  } | null;
  /** The stepper only. */
  activity: {
    kind: 'idle' | 'loading' | 'unloading' | 'runout' | 'error';
    steps: string[];
    index: number;
    failed: number;
    /** What the slot rests on while no plan runs (PRINCIPLES.md 2): the
     *  toolhead's own path. Null whenever `steps` is not empty, so the stepper
     *  and the rest never both speak. */
    rest: {
      label: string;
      /** "#rrggbb", or null when the color is unknown (nothing loaded, or a
       *  spool nobody told us the color of). */
      color: string | null;
      /** True when the color is too dark for the surface: draw a light 1 px outline. */
      outline: boolean;
      loaded: boolean;
    } | null;
  };
  /** The one status line (an error, a runout, a notice or the plain status). */
  message: { text: string; tone: Tone };
  /** The lane fault(s) that name no unit, or null. */
  alert: ViewAlertGroup | null;
  /** True only when nothing in this scope alerts at all, shown elsewhere or not: the slot rests on a check. */
  alert_clear: boolean;
  actions: ViewAction[];
  units: ViewUnit[];
}

export interface ViewAlert {
  severity: Severity;
  code: string;
  title: string;
  text: string;
  unit: string | null;
  slot_id: string | null;
  when_text: string;
  unread: boolean;
  actions: ViewAction[];
}

/** A toolhead's or a unit's alerts: the icon's count and severity, and the items
 *  (with their actions) for its popover. */
export interface ViewAlertGroup {
  severity: Severity;
  /** The badge's tone: error (a fault or a pause) or info, as in the message row. */
  tone: Tone;
  count: number;
  items: ViewAlert[];
}

export interface ViewSettingOption {
  /** The language's code ("en", "qps"), what `setLanguage()` takes. */
  code: string;
  /** That language's own name, never translated. */
  label: string;
}

export interface ViewSetting {
  key: string;
  label: string;
  note: string;
  /** The on/off state of a switch row; false on a choice row. */
  value: boolean;
  /** The two actions of a switch row; null on a choice row. */
  action_on: ViewAction | null;
  action_off: ViewAction | null;
  /** A choice row's list (the languages), empty on a switch row. */
  options: ViewSettingOption[];
  /** The index into `options` of the current choice; 0 on a switch row. */
  selected: number;
}

/** The panel's own chrome strings. */
export interface ViewLabels {
  alerts: string;
  settings: string;
  fault: string;
  pressure: string;
  no_alerts: string;
  close: string;
  cancel: string;
  /** What the panel says when an action got no answer. */
  no_response: string;
  /** "Could not save the spool:": the lead of a host's refusal of an edit, which ends in its own colon; a renderer adds the host's words after a space. */
  edit_failed: string;
  /** "Could not do that:": the same lead for a refusal of any other action (a G-code script the printer refused). */
  action_refused: string;
  /** The head of a unit's filament groups section. */
  groups: string;
  /** The row of the bays that belong to no group. */
  no_group: string;
  /** "Low": a tile's word for a spool under 15 % (`tile.low`). */
  low: string;
}

export interface View {
  toolheads: ViewToolhead[];
  unassigned_units: ViewUnit[];
  /** OpenAMS-wide items only (for the host's bar); a unit's and a toolhead's are on them. */
  alerts: ViewAlert[];
  alert_count: number;
  unread_count: number;
  notice: { text: string } | null;
  /** "Nothing is loading", for a list that has nothing to offer: no load or
   *  unload is under way anywhere, and so none of the list's rows apply. */
  nothing_to_load: { text: string } | null;
  labels: ViewLabels;
  spoolman: { online: boolean; pending: string[] };
  settings: ViewSetting[];
}

/** Where an action line goes. */
export type ActionResult =
  /** G-code lines joined by "\n", for `printer.gcode.script`. */
  | { kind: 'gcode'; script: string }
  /** A Moonraker JSON-RPC call, and the line to send once the host has taken
   *  it (`then`: "load oams11" after a spool edit that ends in "then load").
   *  The caller sends it, and only then - a refusal has to leave the user
   *  where they were. */
  | { kind: 'rpc'; method: string; params: Record<string, unknown>; then?: string }
  /** A host line the model cannot address right now, or a missing form value. */
  | { kind: 'error'; reason: string }
  /** A display-only line: nothing to send. */
  | { kind: 'local' };

/** The Moonraker objects `apply()` understands. */
export type ObjectKind =
  | 'openams_ui'         // name = the unit ("openams_ui oams1" -> "oams1")
  | 'oams_manager'
  | 'component_status'
  | 'print_stats'
  | 'toolhead'
  | 'spool_list'         // a whole result (an array), not a diff
  | 'vendor_list'        // a whole result (an array), not a diff
  | 'metadata'           // a whole result, not a diff
  | 'action_result';     // not a status: the host's answer to an action the
                         // display sent ({kind, ok, message}), which goes
                         // straight to the model

export interface Core {
  /**
   * Merge @p obj's top-level keys into the copy the core owns for (@p kind,
   * @p name) and map the merged copy (a whole object and a diff are both
   * correct). @p name matters only for "openams_ui"; pass null otherwise.
   * Returns true when the core's copy changed. Throws when the arguments
   * cannot be applied (not JSON-serializable, or out of memory).
   */
  apply(kind: ObjectKind | string, name: string | null | undefined, obj: unknown): boolean;
  /** Drop every owned copy and reset the model, keeping the alert history
   *  when @p keepAlerts. Call on a printer reset and on a socket reconnect. */
  reset(keepAlerts?: boolean): void;
  /**
   * Set the clock the core stamps alerts with and ages them by, in
   * milliseconds (a uint32; the host's Date.now() by default), or null to
   * follow the host's clock again. Tests set it to stay deterministic.
   */
  setClock(ms: number | null): void;
  /** The current panel tree. */
  view(): View;
  /** Resolve an action line (filling `{field}` placeholders from @p form). */
  action(line: string, form?: Record<string, string | number | boolean> | null): ActionResult;
  /**
   * Show the view's text in @p code ("en", "qps"): every later view() uses
   * it, and an id the language does not translate falls back to English.
   * Returns false when no such language is built in (the current one is
   * kept). The choice survives reset(). Read view() again afterwards: the
   * view is rebuilt, never patched.
   */
  setLanguage(code: string): boolean;
  /**
   * Call @p listener with the current view after any apply() or reset(), at
   * most once per animation frame (a microtask in node). Every listener of a
   * flush gets the same view object; treat it as read-only. There is no call
   * on subscribe: read view() for the first paint. Returns the unsubscribe
   * function.
   */
  subscribe(listener: (view: View) => void): () => void;
}

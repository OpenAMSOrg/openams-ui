# Unified UI: one core, two renderers (U0 design v2, 2026-09-28)

**Status:** v2. It folds in the Fable consult F-5 (`openams-wt/_briefs/F-5-result.md`) and awaits the owner's sign-off.

**Decided by the owner (2026-09-28):**
- openams-ui stays the one project and the master. The Mainsail panel is another output of it; no new repo, and the old Mainsail OpenAMS module is dropped.
- Architecture **option A**: one C core compiled for the ESP32 and to WebAssembly, and two thin renderers: LVGL on the device, HTML/CSS (Lit) on the web.
- The look is the web concept's.
- Web portability matters: Fluidd after Mainsail.
- v1 covers OpenAMS units only.

## 1. Outputs

| Output | Scope | Renderer | Data path |
|---|---|---|---|
| ESP32 display | one unit (the one it is mounted on) plus its toolhead, 480×320 | LVGL | its own Moonraker WebSocket (`moonraker_client`) |
| Simulator, Windows zip, web demo | the same as the ESP32 | LVGL (SDL; Emscripten for the demo) | the mock Moonraker, fixtures, or a real Moonraker |
| **Mainsail panel** (new) | every unit, grouped by toolhead (FPS lane), fluid width | Lit web component `<openams-panel>` | the host's store feeds a shared wasm core; commands go out through the host's socket |
| Fluidd panel (later) | the same | the same bundle | a second thin wrapper |

The Mainsail wrapper lives in the Mainsail fork, where panels register. openams-ui keeps its source under `web/hosts/mainsail/` and a sync script.

## 2. Layers and the one rule

```
Moonraker objects → core_apply (merge into owned copies) → status_map → model (units, toolheads)
                                                                          │
                     view builders (src/view): view_tile / view_unit / view_toolhead / view_alert / view_panel
                            │                                      │
                  LVGL renderer: calls the builders       view_json(): walks the builders
                  per widget, into stack buffers           → Lit renderer
                            │                                      │
                    action line ──────────► core_action(line) ◄─── action line (+ form values)
                                                 │
                                   {gcode, script} | {rpc, method, params} | {error, reason}
```

**The rule:**
- The core decides every piece of content: text (English in v1, from one string table), number formats, states, which actions exist, whether each is enabled (and why not), what each sends, and the ink on a filament color.
- A renderer draws what the builders return and reports the chosen action line, plus any form values.
- A renderer never reads Moonraker objects, never composes G-code or a line, and never branches on raw C2 fields.

## 3. The model: per-toolhead storage (U1)

**Storage:** `mmu_toolhead_t toolheads[MMU_MAX_TOOLHEADS]`, keyed by `lane.id` (the FPS). Each holds:
- the extruder, op, loaded unit and bay, busy and stage (plan, index, failed);
- the runout (from, to, phase), and pressure and set point;
- `motion_settled`, and the lane fault (`has_error`, the `error_*` fields, `nf`, and the fault's freshness check).

Each unit records its toolhead index.

**The legacy globals** (`loaded*`, `busy*`, `steps[]`, `step_*`, `current_group`, `has_error`/`error_*`, `motion_settled`, the runout fields, `print_unit`/`print_slot`/`filament_changes`) **stay as real fields.** `mmu_model_refresh_this()` copies this unit's toolhead into them:
- at the end of every `status_map_apply_*`;
- in `mmu_model_merge_live`;
- in every dummy-scenario transition.

The demo scenarios and transitions keep writing the globals, and `mmu_model_capture_this()` mirrors them into the toolhead after each one (U1a: the demo has one toolhead, `id ""`). The screens keep reading the globals until U3 moves them to the builders. **Memory note (U1a):** each toolhead stores its step texts, costing about 3 KB of static DRAM across the UI model and the network copy (32 KB free); U1c/U3 should store stage codes and let the builders produce the text.

**Gate:** every existing screenshot stays byte-identical. This also fixes a live bug: today a second lane's projection overwrites the globals.

**Also single-toolhead today, and moved to per-toolhead or to the view:**
- `ext_spools` / `loaded_ext`;
- `mmu_group_editable` (it reads the global busy state, runout and current group);
- `actions_map`'s `stop` (it reads `busy_unit`);
- the enough-filament shortfall. This one moves into the view, computed on demand, because today it is computed once when the metadata arrives and never recomputed if the groups arrive later.

**Renderer state leaves the core:** `selected_*` and `home_selection`. A shared wasm core would make two panels fight over them.

**Pressure and set point** are not in `openams_ui`, by design: C2 keeps fast-changing values out of the projection. They come from `oams_manager.lanes_by_fps.<l>` (`pressure`, `set_point`). The ESP32 adds `lanes_by_fps` to the `oams_manager` attributes it already subscribes to for `settings`. Mainsail already subscribes to every object.

## 4. The view: builders, not a materialized struct

A fully built view for the panel would be about 1.3 KB per tile, or about 60 KB for 6 units × 8 bays, which does not fit the ESP32's DRAM. So the core exposes **builders** that fill caller-provided structs:
- `view_tile(unit, bay, view_tile_t *)`;
- `view_unit(unit, view_unit_t *)`;
- `view_toolhead(th, view_toolhead_t *)`;
- `view_alert(i, view_alert_t *)`;
- `view_panel(view_panel_t *)`, which fills counts and notices, not children.

LVGL calls them per widget on redraw, into stack buffers. `view_json()` walks them for the web. Both renderers get their content from the same builders, and the goldens cover the builders' output.

**Field shapes** (JSON form; C structs mirror them with fixed-size text buffers; every string is final and cut by the core to its stated maximum):

```
panel      { toolheads: [toolhead], unassigned_units: [unit],
             alerts: [alert], alert_count, unread_count,
             notice: {text} | null,                  // not ready, no units, host offline
             spoolman: {online, pending: [slot_id]}, settings: [setting] }
toolhead   { id, title, subtitle,
             tool: {label, color, ink} | null,       // the loaded tool badge and toolhead tint
             pressure: {value, set_point, level: "ok"|"high"|"critical", text} | null,
             activity: {kind: "idle"|"loading"|"unloading"|"runout"|"error", text,
                        steps: [label], index, failed},
             runout: {text} | null,
             notice: {text, tone: "warn"|"info"} | null,    // e.g. "T1 needs 120 g; its spools have 80 g"
             actions: [action],                      // unload, stop, fault retry_load, resume, clear errors
             units: [unit] }
unit       { id, title, short, online,
             env: {text} | null,                     // "28% RH | 55°C"
             dryer: {text, tone: "heat"|"hold"|"cool"|"fault"|"off", adapter_missing} | null,
             fault: {severity, count} | null,
             info: {serial, firmware, family} | null,   // for the unit-info screen (U3)
             actions: [action],                      // dry start (with a form), dry stop, clear fault, rescan
             bays: [tile] }
tile       { slot_id, bay,
             tool: "T0" | null, label, sublabel,     // label = the tool, else the slot; sublabel = the slot, or null when label is the slot
             spare: {position: int | null, count} | null,
             state: "loaded"|"ready"|"empty"|"positioning"|"loading"|"error"|"runout"|"unknown",
             tag: text | null,                       // "Runout", "Not inserted", "Positioning", …
             color, ink: "light"|"dark", hatched,
             name, material, brand, grams_text, pct, pct_text, low,
             rfid, pending_confirmation, calibrated,
             actions: [action] }
action     { id, label, line, enabled, reason, style: "primary"|"normal"|"danger",
             confirm: {title, text, ok_label} | null,
             form: {fields: [{id, label, kind: "number"|"select"|"toggle", value, min, max, step, unit, options}]} | null }
alert      { severity: "stop"|"pause"|"info", code, title, text, unit, slot_id, when_text, unread, actions: [action] }
setting    { key, label, note, value, action_on: action, action_off: action }
```

**Rules:**
- **Tile label (owner, 2026-09-29):** the label is always the tool (`T0`, `T1`, …). A bay in no filament group shows `?`, because the slicer can only load a spool through a tool number. The sublabel is always `Spool N` (the bay, 1-based), small, under the label, at every width. There are no unit letters (`A1`, `B2`): the configuration has no unit-to-letter mapping and none is wanted. See §4a for the owner's other decisions.
- **Spare order:** `position` counts the group's non-loaded members in fallback order, so the next spare is `∞1` (what `mmu_group_next_spare` returns). The loaded tile has `position: null` and is drawn as a bare `∞` when the group has a usable spare; with no spare there is no badge.
- **Actions:** `enabled` and `reason` come from dry-running `actions_map` / `actions_rpc` on `line`, one predicate for both. A line with form placeholders (`dry A start {target} {hours}`, `spool confirm A1 {grams}`) is filled verbatim by the renderer from the form's values. No local-only line ever appears in `actions`. `confirm` is shown verbatim; the core never depends on it having been shown.
- **`core_action(line)`** returns `{kind: "gcode", script}`, `{kind: "rpc", method, params}` or `{kind: "error", reason}` (`actions_map`'s -1). A display-only line (never offered in `actions`, but possible if a renderer passes one) returns `{kind: "local"}`, which a web host ignores (U1d).
- **Color is known, or it is `null`.** A spool nobody told us the color of has `color`, `gradient_top` and `gradient_bottom` all `null` (and `tool.color` on a toolhead whose loaded bay is one), and the renderer draws the neutral surface, never a black fill, which would read as black filament (PRINCIPLES 1). A known `#000000` is black filament and is drawn black. The model carries this as `color_known` on a slot and on a Spoolman list entry; a missing, empty or unreadable `color_hex` is unknown.
- **Edit spool** is the ring's action after Unload, in both renderers. Its line is `spool edit <slot> material={material} color={color} vendor={vendor} remaining={remaining} initial={initial} pa={pa}`, an RPC to `server.openams_spoolman.edit`. The form's selects carry indices, never names, so one line fits both renderers and a vendor name with spaces needs no quoting: `material` indexes the common materials (one past the list is the bay's own), `color` is `0xRRGGBB` in decimal (`-1` = not set), `vendor` indexes Spoolman's vendor names plus `Generic` (one past is the bay's own), `remaining` and `initial` are grams, `pa` the pressure advance. Only what differs from what the form starts from is sent, except that a spool the host has no record of is always sent with its material. It is dimmed, with the reason, when the host's status has no `edit: true` ("Spoolman is not set up"), Spoolman is offline, or the bay is empty. The vendor list is `server.spoolman.proxy` `GET /v1/vendor`, read once the host says it can edit. A refusal by the host is shown in its own words after "Could not save the spool:" (`labels.edit_failed`): the web host calls the `done(message)` its `request` event hands it, the ESP32 toasts it.
- **Values a renderer may use** (geometry, never content): `pct`, `pressure.value` / `set_point`, `color`. Raw environment and dryer numbers stay inside the core; renderers get their text.
- **Strings:** every string the core emits comes from `src/view/strings.c`, keyed by id, so a message catalog later is a mechanical change. The host wrapper translates only its own chrome (the panel title) with its i18n. The component sets `lang`.

**The ESP32's single-unit screens** call `view_unit(this)`, `view_tile(this, i)` and `view_toolhead(this unit's toolhead)`. U3 moves the screens onto the builders as part of the restyle; until then the globals keep them working.

### 4a. Owner decisions from the first web review (2026-09-29)

These bind both renderers; the view fields change to match. The principles behind these rulings, and the way to derive new cases, are in `PRINCIPLES.md`.

**Naming**
- **Toolhead = extruder.** FPS and extruder are one to one, so the toolhead card's title is the extruder's display name, and the FPS id sits in smaller text under it.
  - The display name is built from the lane's `extruder` config name: underscores removed, words capitalized (`dragon_burner` → "DragonBurner", `extruder` → "Extruder").
  - The FPS id is shown as configured (`fps1`).
  - The loaded tool is shown only as an overlay on the hotend icon (as in the concept), never as separate text.
- **Unit:** the title is the family name ("AMS 2 Pro"), with the unit's config name (`oams1`) in smaller text under it. Unit letters are gone.

**One message row per toolhead card**
- The card has a fixed status area: the stepper ("Feed / Detect / Prime / Purge") while an operation runs, and **one** message line under it.
- The core merges everything into that one line, by priority: an error (red) > a runout or other information (yellow) > a notice (yellow) > the neutral status.
- There is no activity chip under the title ("Load failed", "Runout") and no separate runout banner plus runout chip. For example, a runout is the single line "T0 ran out; switching to T1 (Spool 2)".
- View fields: `toolhead.message: {text, tone: "error"|"info"|"neutral"}` replaces `activity.text`, `runout` and `notice`. `activity` keeps only the stepper (`steps`, `index`, `failed`).
- Notices have no left accent bar.

**Layouts never change height**
- Every element that can appear or disappear has its space reserved: the action buttons, the status area, and the tile tag.
- The web's toolhead actions (Load, Unload, Stop) sit **on the right**, in a reserved-height row.

**Tiles**
- **Always 4 across, never stacked**, at every width. The aspect ratio may flex, but a tile stays clearly taller than wide (at least 1.2 : 1): it is an abstract spool, and a square or wide card loses that.
- At smaller sizes, content drops in this order: meta, then name. The label, sublabel, ring and tag always stay.
- **One disabled look.** Empty, not inserted, unknown and offline all use the same flat, dimmed surface. There is **no hatching** anywhere (`hatched` is dropped for `dim: bool`). An unknown color stays black, which is a natural state. *(Hatching reversed in §4c.)*
- **The status tag** (Not inserted, Error, Runout, Positioning, …) always sits in the same place: bottom left, **vertically centered on the ring**. The ring is bottom right, as in the concept.
- **Tag colors:** red for errors, yellow for information (runout, positioning, loading, pending confirmation), neutral gray for plain states (not inserted). View field: `tag: {text, tone: "error"|"info"|"neutral"} | null`.
- **The ring must draw its arc** (the percentage highlight), not just its text.

**Structure:** several FPS lanes give several toolhead cards, each holding its units.

### 4b. Owner decisions from the second web review (2026-09-29)

These amend §4a where they differ. The alert-placement, spool-naming, unit-header and stepper rules are corrections made after A3.

**Tiles**
- **Status tags overlay the ring.** A tag (Not inserted, Error, Runout, Positioning, …) is drawn on top of the ring, centered on it horizontally and vertically, hiding the percentage.
- **The ring moves to the tile's bottom center,** so an overlaid tag has the tile's full width. This is for tags wider than the ring. When there is no percentage, the tag sits where the ring would be.
- **Click targets** (milestone B; the editing flow itself may come later). **The web tile has one menu**, opened by the tile and by the ring's row together (the owner: one part of the tile should not do something different from another). It lists every action the core marked `tile` or `ring`, in the core's own order (load and unload, then the spool's editing). Two exceptions:
  - **the tag** opens its details and the fault's recovery actions (its own popover);
  - **the tool pill** (`T0 ∞n`) changes the bay's group: the group is shown
    there, so it is changed there. It opens the bay's own "Change group…"
    directly (the same action as in the menu). Groups belong to an FPS
    lane, so there is no per-unit groups list.

  The core still marks each action with its target (`action.target: "tile"|"ring"|"tag"`): the target says where an action belongs, and the device (which has its own sheet) and the core's tests keep using it. Only the web merges `tile` and `ring` into one menu. The pill takes the bay's `change_group` action by its id.

**Real rules in every example**
- Mockups and galleries must obey the real rules: **exactly one loaded spool per FPS** (toolhead), and **spare numbers consecutive within a group** (the loaded member shows a bare `∞`, and the others show `∞1`, `∞2`, … in order).
- Several units may share a tool (T0 in two units), numbered across the group.
- Gallery states are therefore **built by feeding synthetic Moonraker objects through the real core**, not by writing view JSON by hand, so the rules hold by construction.

**Pressure**
- It is labeled "Pressure" (text from the core), left-aligned, on the same line as the value.
- The bar is **always neutral** (the accent blue), whatever the reading. Pressure is never anomalous by itself; a real problem arrives as a message. `pressure.level` is dropped.
- The regulator's normal band (`pressure.band`, `[low, high]`), when the host reports one, is drawn as a quiet neutral segment on the track, behind the fill. It is information, not a judgment: the fill and the number keep their color whatever the reading, and a problem still arrives as a message.

**Alerts by level (hierarchical)**
- **OpenAMS-wide** (`panel.alerts`): shown in the **host's bar** (Mainsail's panel title bar, through the wrapper). The standalone page shows its own small bar.
- **Toolhead / FPS** (`toolhead.alert`): an alert icon with a count at the **top right inside the FPS card**. Its details open in a popover.
- **Unit** (`unit.alert`): an alert icon at the **right end of the unit header**, where the sun icon was, always in that position (reserved space when there is none).
- There are no full-width alert banners.
- **Each alert has exactly one level,** decided by the core from the alert's `unit` and `toolhead` (`mmu_alert_t.toolhead` is added for this):
  - it names a unit → `unit.alert` (a lane fault in that unit, low filament, a dryer fault);
  - otherwise it names a toolhead → `toolhead.alert` (a load failure with no unit, a job shortfall for a group);
  - otherwise → `panel.alerts`.

  An alert never appears on two levels. A badge's tone follows the message row's: **red** when any item is a fault or pause, **yellow** otherwise.
- **Any text the core composes that names a spool includes its unit's config name** ("oams2 Spool 1"), because two units can share a family name. For example, "T0 ran out; switching to oams2 Spool 1".

**The unit header**
- The drying button (sun icon) is removed. **Clicking the humidity/temperature reading opens the drying and heating menu.**
- The header's right end is the unit alert's reserved slot.
- The header has a fixed height at each width, and the config name is always visible under the title, including at phone widths.
- The dryer pill shows drying status only; a dryer fault is an item in the unit alert.

**The stepper**
- When the step labels don't fit, it keeps the segments and shows one full-width label, the current (or failed) step's, at the same height. It never truncates a label.

**The toolhead header**
- Three zones: the icon and titles on the left, **the pressure block in the middle**, and the FPS alert slot on the right. The alert slot is only as wide as its icon, so the pressure block sits right of center and the titles (with the loaded spool's line) get the room.
- The hotend icon's frame is highlighted in the **loaded filament's color**, with the tool overlay (e.g. T6) on the icon.

**Width**
- An empty area to the right of a unit is accepted.
- A unit with more than four lanes (an EMU with 5 or more) is future work: one row per unit, wrapping in rows of four when narrow.

### 4c. Owner decisions from the third web review (2026-09-29)

- **A larger ring.** The ring scales with the tile (about 45% of its width, roughly 40–60 px) with a thicker stroke. The percentage is at least 12 px at 1200 px wide and at least 10 px at 360 px. The owner said "the size supports it and it is too small now and hard to see".
- **Larger overlaid tags,** scaled the same way (at least 11 px, or 10 px at 360), still never wrapping.
- **Hatching returns as the one disabled look.** A black spool and an empty bay were too hard to tell apart. One diagonal-stripe pattern (the `hatch` token, shared with the ESP32) marks exactly two states:
  - **not inserted** (no spool);
  - **disabled** (the unit is offline).

  A real spool is never hatched, whether black, unknown or in error. This follows principle 6: meaning is never carried by color alone. The texture tells "no spool" from "black spool". It reverses §4a's "no hatching", but keeps its point: there is one disabled look, not two.
- **A unit card is as wide as its lanes.** An AMS HT has exactly one spool, so its card is narrow: one standard tile, and a header no wider than it needs. Tiles are the same size in every unit, and tile rows stay aligned across units. The owner said an AMS HT "should not be 4 units wide". This is principle 4's "just enough" applied to space.

### 4d. The device home screen (U3-A review, 2026-09-29)

- **The menu button sits at the top left** (≡, the usual hamburger position), because the device has no bottom bar. The header's right end stays the unit alert's slot, exactly as on the web (§4b). This is the owner's choice.
- **Status tags are solid chips on the device,** as on the web. A translucent chip over a filament-colored tile fails principle 12's contrast floor ("Runout" on a red tile).
- **Second device review (the owner):** the device keeps its own identity, and must "look at least somewhat like the original".
  - **The toolhead is the original filament path:** a feed stem under each tile, and a line in the loaded filament's color from the loaded tile to a small extruder icon with the tool overlay. There is no "Toolhead" title and no FPS values (no pressure) on the device.
  - **The message row** ("Loading filament", warnings) sits **at the top, under the header**, where the web puts it in the FPS card. It is part of the layout, not an add-on at the bottom.
  - **No stepper on the device's home screen.** The dedicated load and unload screen shows the steps. On home, the message row shows the current step's name during a load or unload, and a tap on the row opens that screen. A message too long for the row ends with "…", and a tap shows the full text (for an error, with its actions).
  - **The RFID mark** is in the tile's **upper-right corner**. The spare badge sits next to the tool label, and its text is centered in its chip.
  - **The header is one line on the device:** the family title with the config name inline, smaller and muted, as the original's "AMS 2 Pro (A)". On a single-unit display, a second line for the config name adds nothing. The web keeps the two-line header, because there the config name tells two units of the same family apart. The menu button is at the top left, larger, sized to the header.

### 4e. Resting states (owner, 2026-10-02; principle 2)

- **Ruling:** a reserved slot is never blank. The owner: the status area "looks strange without something there. I love that we are not changing heights, which is the correct behavior; nonetheless, there should be something default whenever no actions are taking place."
- **The stepper slot at rest (approved as a proof of concept, 2026-10-02):** the toolhead's path in the stepper's own form. One full segment in the loaded filament's color, with the label "T1 · Spool 2 · ABS", plus the working temperature once the host publishes it. With nothing loaded, an empty gray segment labeled "Nothing loaded". The message row under it keeps saying the state ("Filament loaded"); the slot adds which filament and from where. (Correction: the device's bottom path strip draws the path and the tool chip but no text, so the two are not yet the same fact told the same way.)
- **Decided by the owner, from the reserved-slot audit (2026-10-02):**
  - **The empty stepper slot** says what could be loaded, "Ready: T0 · T1 · T3", instead of "Nothing loaded" (which repeated the message row's "No filament loaded", principle 4).
  - **An empty alert slot** shows a muted check, "No alerts": the toolhead's and the unit's on the panel, the header's and the message row's on the device. Each covers its own scope, and on the device a tap opens the alerts screen.
  - **The device's path strip** gets the same line beside the extruder: "T0 · Spool 1 · PLA", or "Ready: …" when empty.
  - **The load screen at rest** shows what is loaded and the next step.
  - **A missing pressure reading** keeps the toolhead header's three zones in place (a principle 2 bug).
- **Every reserved slot** gets its resting state written down here when it is designed.

### 4g. Owner rulings, 2026-10-02 evening

- **A read alert leaves the badge, after a while.** The toolhead's badge stops listing history entries once they are read, but not at once. It keeps a read entry for 2 minutes, and an unread information entry for 15 minutes, before it gives up the slot. Entries with recovery actions and active faults stay until resolved. (The owner: "give it some reasonable time before being replaced or timeout for the display".)
- **Dark filament gets a light outline** wherever its color is drawn on the dark surface: the resting segment, the hotend icon's frame, the device's path and tool chip.
- **Stop stops the whole sequence.** The card's existing Stop (the panel's bottom right, the device's load screen) stops whatever sequence is running: heat, home, cut, retract, feed, the hooks, not only the AMS's own load or unload. It is enabled for as long as a sequence runs. (The owner: "this 'stop' can be unload / load stop or just 'sequence' stop".)

### 4f. Finish the job (owner, 2026-10-02; principle 13)

- **Heating and homing are steps.** A load, unload or toolchange that needs heat heats as the plan's first step instead of refusing. During a print, the print's temperature applies and the macros never change it.
- **An unknown spool is completed just in time.** Load on an unknown spool opens the spool form ("Save and load"); the load runs once Spoolman has it. The 235 °C fallback is only for paths that cannot show a form.

## 5. Design tokens (`design/tokens.json`)

One file, generated into `src/ui/ui_tokens.h` (LVGL, by `tools/gen_tokens.mjs`) and `web/vue/src/tokens.{css,scss}` (`--oams-*`, by `web/vue/tools/gen_tokens.mjs`). The values come from the concept; see the file for both sets.

**The tile fill is data, not theme:** a two-stop vertical gradient of the filament color. Dark ink: 78 % filament mixed with `#141414` at the top, 42 % with `#1a1a1a` at the bottom. Light ink: 96 % with black at the top, 74 % with `#1a1a1a` at the bottom. The core computes `ink` (luminance 0.2126 R + 0.7152 G + 0.0722 B > 160) and the stops. LVGL uses them directly; the web uses `color-mix` with the same numbers.

**Type:**
- Web: title 21/600, section 15/600, tile label 20/600 (16 at narrow widths), body 14/500, meta 12, mono value 22.
- Device: 20, 15, 20, 13, 12, mono 18, as Geist and IBM Plex Mono bitmap fonts (SIL Open Font License; only the sizes in use; about 150 KB of flash; no DRAM).
- **In a host, no webfont ships:** the UI font inherits the host's, and the mono is `ui-monospace, 'Roboto Mono', monospace`. The standalone page loads Geist.

## 6. Theming on the web

**Filament colors never change with the theme. Everything else follows the host.**
- `--oams-bg` and `--oams-surface-1` are **transparent**: the toolhead card is the host's card. Several toolheads are separated by a `--oams-line` rule.
- `--oams-surface-2`, `--oams-surface-3` and `--oams-line` are `color-mix(in srgb, currentColor 5% | 8% | 10%, transparent)`. That is Mainsail's own convention (foreground at an alpha over whatever the card is), so they sit correctly on any host card, light or dark.
- Text inherits (`color: inherit`), and so does the UI font.
- `--oams-accent` follows the host's primary color, and `--oams-warn`, `--oams-error`, `--oams-ok` and `--oams-info` follow the host's status colors.
- **`--oams-pressure-*` and `--oams-heat-*` stay ours.** They are semantic scales, and pressure-ok must not turn into the host's primary.

**Mainsail wrapper** (Vuetify 2, `customProperties: true`):
```css
openams-panel { --oams-accent: var(--v-primary-base); --oams-error: var(--v-error-base);
                --oams-warn: var(--v-warning-base); --oams-ok: var(--v-success-base);
                --oams-info: var(--v-info-base); }
```
**Fluidd:** the same, after checking that it enables `customProperties` (U4 checks this first); the alpha surfaces work either way.

**The standalone page:** our dark or light set, chosen with the `theme` attribute.

**The device:** the dark set, compiled in.

## 7. Widget catalog

| Widget | Input | States | 480×320 (LVGL) | Fluid (web) |
|---|---|---|---|---|
| LaneTile | `view_tile` | loaded, ready, empty and unknown (hatched), positioning, loading, error, runout (tag), low | about 108×150: label 20, spare badge, RFID mark, name, meta, a 52 px ring | 3 : 4.15; 4 across down to 96 px each, **then 2×2**, which fixes the concept's phone overlap |
| SpareBadge | `tile.spare` | `∞n`, bare `∞`, none | mono on a dark chip | mono 16 |
| Ring | `pct`, `pct_text`, `low` | full, partial, unknown `?`, low (amber) | `lv_arc` 52 px | SVG sized from the remaining height |
| UnitHeader | `view_unit` | env, drying, fault badge, offline | the top bar: title, env pill, bell | the section header: title, pills, env button |
| EnvPill / DryerPill | `unit.env`, `unit.dryer` | tones | | |
| ToolheadCard | `view_toolhead` | idle, loaded (tint + badge), loading with the stepper, runout, error, notice | the bottom strip: icon + badge, "T0 · A1 PLA", a thin pressure bar | the card: icon, title and subtitle, pressure with its value and scale, stepper, notice, actions |
| PressureBar | `toolhead.pressure` | ok, high, critical; with or without the regulator's band (a quiet segment, not a judgment) | 4 px | 7 px, with a mono value |
| Stepper | `toolhead.activity` | the steps, current, failed | dots and the current label | labeled steps |
| AlertCard / AlertList | `view_alert`, `panel.alerts` | stop, pause, info; actions | the alerts screen, restyled | a banner on the affected card, and a list in a popover |
| ActionSheet | `tile.actions`, `toolhead.actions`, `unit.actions` | enabled, disabled with its reason, danger, confirm, form | a bottom sheet | a popover anchored to its source |
| ActionForm | `action.form` | number, select, toggle | stepper rows in a sheet | a native `<dialog>` form |
| SpoolConfirm / SpoolAssign | the tile's pending confirmation; the spool list | | sheets (D4a) | dialogs: the "Confirm" tag's details carry "Confirm spool" (`confirm_spool`, target `tag`), a form with the weight left, starting from the spool's initial weight, that sends `spool confirm <slot> {grams}` |
| SettingsList | `panel.settings` | toggles | the settings screen | a popover |

**LVGL rules for the look:**
- Two-stop vertical gradients are cheap; the complex-gradient option stays off.
- **No `shadow_*` styles anywhere.** Loaded is a 2 px accent ring plus one static rounded rect at opa 40 outside it.
- Hatching is an 8×8 A8 image tiled with `LV_IMAGE_ALIGN_TILE` (64 bytes), not drawn lines.
- Fonts live in flash.

**Web rules:** dialogs are native `<dialog>` opened with `showModal()`, and popovers use the `popover` attribute. Both render in the top layer, above Vuetify's overlays and cards, inherit `--oams-*`, and get focus trapping and Escape for free. Each draws on its own base and in its own ink (`--oams-pop-base`, `--oams-pop-ink`, following the host's light or dark theme), never in the ink of what it is anchored to: a tile's menu sits inside the tile, and a light spool's dark ink would vanish on a dark popover. There are no Vuetify overlays, and the shadow DOM keeps Vuetify's global CSS out.

## 8. The wasm core and the host contract

**Build:** a second Emscripten target, `openams-core`: the core only, no LVGL, no SDL. Compiled with `-Os`, `MODULARIZE` + `EXPORT_ES6`, a fixed 1 MB heap and no memory growth. **Budget:** ≤ 120 KB raw and 50 KB gzipped; the whole lazy chunk including Lit ≤ 100 KB gzipped. CI fails above either.

**Exports:**
```
core_init()
core_reset(keep_alerts)             // on every printer reset (Mainsail: printer/reset, notify_klippy_ready) and socket reconnect
core_apply(kind, name, json) -> changed
                                    // merges the object's top-level keys into a copy the core owns (mc_merge_object's rule),
                                    // then maps the merged copy. Diffs and whole objects are both correct.
                                    // kind: "openams_ui" (name = unit), "oams_manager", "component_status", "print_stats",
                                    //       "toolhead", "spool_list", "metadata"
core_view() -> json                 // view_json()
core_action(line, form_json|null) -> json
```

**One core per page:** `openams-core` is a JS singleton, with one wasm instance and one store watcher. Its `core_view()` fans out to every mounted `<openams-panel>`, because Mainsail can mount a panel in several layouts at once.

**The Mainsail wrapper (a Vue 2.7 SFC):**
- renders Mainsail's `<panel>` chrome;
- lazy-loads the bundle with a dynamic import;
- keeps one `store.watch` per `openams_ui *` key plus `oams_manager`, `print_stats`, `toolhead`, and the result of `files/getMetadataCurrentFile` (Mainsail already fetches it);
- calls `core_reset` on `printer/reset` and on a reconnect;
- calls `server.openams_spoolman.status` and listens for `notify_openams_spoolman_status`;
- computes `core_view()` at most once per animation frame;
- sends every `oams-request` with `$socket.emit('printer.gcode.script', …)` or the RPC method, so it appears in the console.

The Fluidd wrapper has the same shape.

**Standalone page** (optional, after the panel): the same component, with its own WebSocket.

## 9. Drift control and tests

- **Builder goldens:** each recorded and mock fixture is replayed through the core, units applied in fixture order, and `view_json()` is compared with a checked-in golden. This extends `tests/replay_vams_fixtures.sh`.
- **Galleries:**
  - LVGL `--gallery` renders every catalog state from golden builder outputs;
  - the web gallery (`web/vue/gallery.html`) renders the same states, screenshotted by Playwright (`web/vue/tools/shoot.mjs`);
  - a script builds a side-by-side sheet per widget and state.
- **Action vectors:** `test_actions_map.c`'s cases exported as JSON; the wasm build is tested against them.
- **Device memory:**
  - static DRAM free ≥ 20 000 bytes (the existing gate);
  - free internal heap after init ≥ 40 KB (ARCHITECTURE §7.5), measured on hardware (checklist X7), and in Espressif's QEMU in CI if that proves practical.
- **Integration:** a Mainsail dev server against the mock Moonraker with a multi-unit fixture, under Playwright (light and dark); the ESP32 build; CI jobs for each.

## 10. Phases (streams)

| Phase | Work | Stream / model | Gate |
|---|---|---|---|
| U1 | the per-toolhead storage and `refresh_this`, merge-owning `core_apply` / `core_reset`, `src/view` builders + `strings.c`, `view_json`, builder goldens, the `openams-core` wasm target and its size budget, the action vectors | core, Sonnet 5 | the goldens; every existing screenshot byte-identical; the wasm budget; every display gate |
| U2 | the Lit renderer: tokens.css, the widgets, the fluid layouts (2×2 below 96 px), native dialogs and popovers, the gallery, Playwright screenshots, an axe accessibility check | web, Sonnet 5 | the gallery reviewed; axe clean; the chunk budget |
| U3 | the LVGL restyle: `ui_tokens.h`, Geist and IBM Plex Mono, tile, ring, toolhead strip, unit header, alerts; the screens move onto the builders | display, Sonnet 4.6 | smoke and audit; the ESP32 build and memory; the side-by-side sheet |
| U4 | the Mainsail wrapper on a fresh branch of `jrlomas/mainsail`: theme mapping, lazy load, the singleton core; Playwright against the mock Moonraker (light and dark); Fluidd's `customProperties` checked | integration, Sonnet 4.6 | the panel renders the multi-unit fixture natively in both themes; actions arrive at the mock |
| Join U | the side-by-side review, Mainsail in light and dark, the device on the simulator gallery | review | owner sign-off |

U2 and U3 start once U1's builders, goldens and tokens exist, and run in parallel. U4 follows U2.

## 11. Revision (2026-09-29): Mainsail by generation, fork first

This supersedes the web parts of §1, §7, §8 and §10 (U2, U4). The ESP32, the C core, the view model and the principles are unchanged.

**Why:**
- Neither Mainsail nor Fluidd loads user JavaScript (`docs/research/hosts.md`).
- Upstream would not merge a wasm panel, and the owner does not want to argue for one.
- The panel therefore lives in the `jrlomas/mainsail` fork as ordinary Vue 2 + Vuetify 2 code. It is **generated from openams-ui** and committed there as static source, and it is never edited by hand in the fork. The owner's words: "we can generate them dynamically, then have the assets static for the mainsail".

**What is generated from openams-ui into the fork:**
- the tokens, as Mainsail's SCSS and CSS variables;
- the strings table, as i18n locale entries;
- the view types, as TypeScript types;
- the widgets: Vue 2 single-file components written once in openams-ui that only draw the view (§2's rule). The generator copies them into the fork, rewriting imports, i18n keys and theme hooks.

The frame uses Vuetify's own cards and buttons, so the host theme applies natively. Fluidd, also Vue 2 + Vuetify 2, takes the same components with its own small wrapper (phases F1 and F2, done 2026-09-29: `web/hosts/fluidd/` holds the dashboard card, its store adapter and its hook patch, and `tools/gen_host.mjs --host <mainsail|fluidd> --out <checkout>` generates either host; `gen_mainsail.mjs` remains as a shim for Mainsail).

**The logic** (objects to view, actions to G-code) is **ported to TypeScript once and kept identical by tests**:
- The C core's wasm build is no longer shipped. It becomes the test oracle: every recorded fixture and randomized state sequences run through both, and the view JSON and action results must match byte for byte.
- A logic change is made in C and TypeScript together, and CI refuses a merge until they agree.
- Generating both from a spec was considered, and deferred unless this proves costly.

**Retired:** the Lit renderer (U2 milestone A). Its approved look (reviews A1–A8, §4a–§4c) ports to the Vue components, and the gallery, screenshot tool and tone checks move with it.

**Phases (they replace U2's milestone B and U4):**

| Phase | What it covers |
|---|---|
| V1 | The TypeScript logic, and the differential tests against the C core |
| V2 | The Vue 2 components, and the gallery on Vue (porting the approved look) |
| V3 | The generator, the Mainsail fork (the panel folder, the dashboard registration, i18n, theme) and Playwright against the mock Moonraker |
| V4 | Interactions: popovers, dialogs, pending and never-twice, dimmed reasons on touch, and the floors of principle 12 |
| F1 | Fluidd, the host side: the dashboard card hook (following Fluidd's AFC card) and a store adapter, on a fresh `openams-panel` branch of Fluidd |
| F2 | One generator for both hosts: `web/hosts/<host>/` for each host's entry, adapter and hook patch; regenerate Mainsail and Fluidd from the same `web/logic` and `web/vue` |

## 12. Translations (planned 2026-09-29, the owner's decision)

**Languages.** English, plus German, Chinese (Simplified), Spanish, French, Japanese, Russian, Portuguese (Brazil), Italian, Polish and Korean.
- They were chosen by market size, spending power, comfort with English, and the completeness of Mainsail's volunteer translations.
- The runners-up are Turkish, Chinese (Traditional), Ukrainian and Czech.

**Where the translations live.** In the core's string table, never in a host's i18n files. The core composes the text, so one set of translations serves the device, Mainsail and Fluidd.

**Phases (the evaluation model, in clean rooms):**
- **i18n-a, the plumbing:**
  - per-language tables behind the one source (`design/strings/<lang>.json`), with English as the fallback;
  - generated C and TypeScript tables;
  - `set_language` in both cores, and differential tests in every language.
- **i18n-b, the translations:**
  - a glossary: `T0`, "Spool N", `∞` and unit config names stay untranslated, and each other term has one fixed translation per language;
  - placeholders checked mechanically;
  - length budgets on short strings, such as tags;
  - a Playwright check that fails on clipped or overflowing text in any language (principle 2);
  - round-trip back-translation, with drift flagged for review.
- **i18n-c, the device fonts** (after U3-A merges). `tools/check_glyphs.mjs` measured the gap: the fonts carry ASCII plus a few symbols only, so **every** translation is missing glyphs, including the Latin Extended accents of de/es/fr/it/pt-BR/pl (for example ä ö ü ß, ñ á é, ą ł ż), Cyrillic for ru, and 280–330 CJK glyphs each for ja/ko/zh-Hans. The checker's `--check` is the acceptance test: remove each language from its allow-list as its glyphs land.
  - confirm that Geist and Plex cover the Latin Extended and Cyrillic ranges;
  - a CJK font subset to exactly the glyphs in the strings table;
  - the flash cost measured.

  The default is all languages built in and selectable in settings, if flash allows; otherwise CJK becomes a build-time choice.

**Done (2026-09-29).** All eleven languages ship in one image, selectable on the device and following Mainsail's own language in the panel.
- **Fonts:** Geist carries the Latin and Cyrillic characters the translations use. Japanese, Korean and Chinese have their own Noto Sans CJK faces, in each language's regional design, subset to the characters used and drawn at 2 bpp. A writable slot per size lets the language switch them at run time.
- **Flash:** the owner chose to keep every language and every CJK size in one image. The pseudo-locale stays off the device (`VIEW_NO_PSEUDO`), and CI's OTA slot limit is 95% (it was 90%). The image is 94.07% of the slot, with 120 KB free. The next lever, if needed, is drawing CJK titles with the 15 px face (−93 KB).
- **Checks:** `check_glyphs --check` runs in CI, and the device layout audit passes in all eleven languages.

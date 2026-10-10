# MMU Display — screen workflow spec (simulator phase)

A small touchscreen add-on dedicated to a printer's multi-material unit(s)
(Bambu AMS-style units, AFC, Happy Hare, OpenAMS, ...). Target hardware is the
"Cheap Yellow Display" ESP32-2432S028: ESP32-WROOM-32E (520 KB SRAM, no PSRAM,
4 MB flash), 320x240 ILI9341 over SPI, XPT2046 resistive touch.

**This phase:** the complete screen workflow, running in a desktop SDL
simulator, with dummy data. Actions are not connected to anything: every
button that would do something logs a line and, where useful, animates the
dummy model (e.g. a load walks its steps). Nothing is flashed to hardware yet,
but every line of UI code must be ready to compile unchanged for the ESP32.

Reference for what each screen must show: a research report, the Bambu AMS screen inventory.
(read-only). Follow the A1's compact layout ideas, not the X1's large cutaway.

## Architecture (portability rules)

```
lib/lvgl/                LVGL v9.6.0 (submodule, do not modify)
lv_conf.h                ONE config shared by simulator and ESP32
src/model/               mmu_model.h/.c   — dummy data + scenarios (pure C)
src/ui/                  one .c/.h pair per screen + ui_theme, ui_nav, ui_widgets
src/app/                 app.h/.c (start-up, scenario switch), actions.h/.c (action log)
platform/sim/main.c      SDL window + mouse, CLI flags, smoke/screenshot modes
platform/esp32/          (later — not in this phase)
CMakeLists.txt           simulator build
```

- **C99**, LVGL 9.6 API only. `src/` includes nothing but `lvgl.h`, the C
  standard headers `<stdint.h> <stdbool.h> <stddef.h> <string.h> <stdio.h> <stdarg.h>`
  and project headers. No SDL, no POSIX, no `malloc`/`free` (use static
  storage; LVGL objects are the only heap users).
- Platform specifics (display, input, time source, action sink) live in
  `platform/`. `src/app/actions.c` calls one hook,
  `void platform_action(const char *line)`, which the simulator implements as
  a `printf` of `ACTION: ...`.
- **Memory budget = the CYD's.** `lv_conf.h`: `LV_COLOR_DEPTH 16`,
  `LV_USE_STDLIB_MALLOC LV_STDLIB_BUILTIN`, `LV_MEM_SIZE (64 * 1024)`.
  Only the Montserrat sizes actually used (12, 14, 16, 20 at most). Only the
  widgets used. Keep `LV_USE_LOG` on at WARN in the simulator.
- **One screen alive at a time.** Create a screen when navigating to it and
  delete the previous one (`lv_screen_load_anim` with auto-delete, or delete
  explicitly). Modals are children of `lv_layer_top()` and are deleted on close.
- Screen size 320x240 landscape. Minimum touch target 36x36 px. Text never
  below 12 px.

## Data model (dummy)

A generic MMU model, not Bambu-specific:

- **Unit**: name (e.g. "A", "B", "HT-A"), kind (4-slot, 1-slot high-temp,
  generic lane MMU), slot count (1..8), side (none/left/right for dual
  extruders), connected flag, capabilities: `has_humidity_level` (A-E),
  `has_humidity_pct`, `has_temp`, `can_dry`, `rfid`.
  Environment: humidity level/%RH, temperature °C, drying state
  (idle/heating/dehumidifying/cooling/error), target °C, remaining minutes,
  rotate-spool flag, power-adapter flag, `cannot_dry_reason` (text or empty).
- **Slot**: color (RGB), material (e.g. "PLA", "PETG-CF"), brand, remaining %
  (-1 = unknown), info source (tag/manual/unknown), state
  (empty/ready/loaded/error), nozzle min/max °C, K value, group membership
  (named, ordered groups replace a per-slot number - see docs/GROUPS.md).
- **External spool** (per side on dual-extruder setups).
- **System**: loaded slot (unit, slot or none), toolhead sensor state, busy
  operation (none/load/unload + step list + current step index + failed step),
  printing flag (+ current slot, filament change count), active error
  (severity stop/pause/info, code, text, unit/slot, buttons), alert list.

**Scenarios** (selectable with `--scenario NAME` and at runtime from a hidden
debug screen reached by long-pressing the title bar):
`idle` (two 4-slot units — one with %RH/°C + drying, one with A-E level — plus
one HT single-slot unit and the external spool, some slots empty / unknown /
tag-read), `loading` (step list auto-advancing via `lv_timer`), `error`
(feed failure during load, pause severity), `drying` (heating then
dehumidifying with countdown), `printing` (status strip, a filament change in
progress), `runout` (auto-refill switched to backup, banner names the
material), `dual` (left/right extruder grouping).

## Screens

Must-have (all of these in this phase):

1. **Unit/slot overview (home).** Title bar: unit name + "1/N" pager
   (left/right arrows or swipe), environment badge (A-E level or %RH + °C,
   drying icon when drying). Slot tiles for the current unit: color swatch,
   material text, remaining bar, tag/manual glyph, "?" for unknown, dashed
   outline for empty, highlight + path to toolhead for the loaded slot, error
   tint. External-spool tile. Toolhead sensor dot. Single-slot units are shown
   two per page. Dual-extruder: units grouped under "Left"/"Right".
2. **Slot action menu** (modal on tap): Edit / Load or Unload / Re-read, each
   disabled with a one-line reason when not allowed (busy, loaded to toolhead,
   printing, unknown type, empty).
3. **Load/unload progress**: vertical step list (e.g. Heat the nozzle → Cut
   filament → Pull back current filament → Push new filament → Purge),
   current step highlighted, failed step marked, Stop button, optional "Check
   filament location" step; external-spool flow ends with "Filament extruded,
   continue" / "Not extruded yet, retry".
4. **Error and recovery modal** + **alert list**: severity color (stop red,
   pause orange, info blue), unit/slot/material/color named in the text,
   claim-labelled primary button ("Filament loaded, resume", "Retry (problem
   solved)"), secondary "Details"; alert list screen shows past/active notices.
5. **Slot edit dialog**: vendor, material (roller or dropdown), color
   (palette grid), nozzle min/max °C (spinbox or +/-), K value, Reset / Save.
   Read-only mode with the notice "Filament info read from tag — read-only" for
   tag-read spools; blocked for the in-use slot while printing.
6. **Environment and drying page** (units that report it): %RH/level and °C
   big; drying state with countdown; material preset list that fills in target
   °C and hours; temperature and time adjusters clamped to the unit's limits
   (e.g. 45–65 °C, 45–85 °C for HT, 1–24 h); rotate-spool toggle; Start/Stop;
   "Cannot dry because ..." text with an inline action; power-adapter flag.
7. **In-print status strip**: on the home screen while printing — active slot
   chip (unit+slot, color, material, side), filament-change progress
   overlay, runout / auto-refill banner naming the material, clears when the
   slot is reloaded.

Nice-to-have (after the must-haves work):

8. Filament groups and infinite spooling, editable (see docs/GROUPS.md).
9. MMU settings list (read on insertion, read on startup, remaining estimate,
   detection toggles).
10. Print-start mapping (project filament ↔ slot chips; wrong type disabled
    with a reason).
11. Unit order and side binding (per-unit left/right).
12. Multi-unit humidity overview with an A-E scale explainer.
13. Unit info (model, serial, firmware, "update available", read-only).

Navigation: home is the hub. A compact top bar (back arrow when not home,
title, alert bell with count) and a bottom row or menu button reaching
Environment, Alerts, Settings and the nice-to-have screens.

## Look

Dark theme, high contrast, LVGL built-in symbols (`LV_SYMBOL_*`) for icons,
Montserrat fonts, one accent color, color swatches as the dominant visual
element. Consistent spacing tokens in `ui_theme.h` (padding 4/6/8, radius 6).

## Verification

- Build: `cmake -S . -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build -j4`
  (never more than `-j4`).
- `./build/openams-ui-sim` opens a 320x240 window scaled 5x by default (`--zoom N`).
- `SDL_VIDEODRIVER=dummy ./build/openams-ui-sim --smoke` visits every screen
  in every scenario, opens every modal, runs the timers, prints
  `lv_mem_monitor` peak usage, and exits 0. It exits non-zero if LVGL's pool
  peak exceeds 90% of `LV_MEM_SIZE` or any screen fails to build.
- `SDL_VIDEODRIVER=dummy ./build/openams-ui-sim --screenshots out/` writes
  one 24-bit BMP per screen/scenario/modal (320x240, via `lv_snapshot`) so the
  layouts can be reviewed without a display.

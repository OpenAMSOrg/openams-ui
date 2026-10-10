# AMS clip screen — 480×320

Build target: 480×320 landscape, LVGL 9, 16-bit colour, resistive touch, ESP32, no GPU. Pixel density matches the 320×240 panel, so type, icons, and touch targets stay the sizes already shipping. The extra 160×80 px is layout and information.

Frames, each exactly 480×320:

- `mock_home_480.svg`
- `mock_groups_480.svg`
- `mock_environment_480.svg`
- `mock_loading_480.svg`

If a frame and a number in this file disagree, this file wins. The frames are the visual direction (type, colour, density).

Allowed drawing is unchanged: solid fills, linear gradients, rounded rects, 1–2 px strokes, arcs, lines, pre-rendered fonts, an icon font, and position or opacity animation of one small object. No blur, no backdrop, no drop shadow, no texture, no full-screen fade.

Palette, on-filament luminance, and the red / charcoal gradient stops are unchanged from `VISUAL_SPEC_320x240.md`. Repeated here so this file stands alone.

## What the extra pixels are for

| Screen | Where the +160 × +80 goes |
|---|---|
| Home | Tiles grow from 73×86 to 110×150. Each filled tile gains a group chip, a spare line, and a 52 px remaining ring. The path and the toolhead line no longer share one 36 px row. |
| Groups | Member state and “next on runout” sit on one 44 px row. Unassigned bays are one row of chips. |
| Environment | Readout, ring, and “Heating” share one row. A 48 px card under the steppers shows the preset, the chamber range, and elapsed time. |
| Loading | Step rows grow from 28 to 40 so a row is a 36 px target. Labels have 420 px and do not clip. |
| Edit | Four fields, no temperatures. Vendor and Material on row 1, PA and Color on row 2, each control 220 px wide. |
| Alerts | The active alert’s sentence fits on two lines inside a 456 px card. |
| Mapping | Tool, material, member chips, and the Ready / No spool pill share one 52 px row. |
| Settings | The note under each toggle stays on one line, so a row is 56 px. |
| Sheets | Width 480. Max height 272, which leaves a 48 px scrim, the same scrim as on 240. |

## Palette

| Token | Hex | Use |
|---|---|---|
| bg-top | `#0B0E12` | Gradient start. Nav fill. Label on a mint fill. |
| bg-bot | `#12161C` | Gradient end. |
| card | `#161B22` | Empty tile, pill, unselected chip, list row, pressed sheet row. |
| elev | `#1E252E` | Sheet, stepper, selected chip, Stop band, info card, toast. |
| hair | `#2A3340` | Dividers, ring track, inactive rail, stepper rules, nav top rule. |
| text | `#F2F5F8` | Primary. |
| text-2 | `#8D98A5` | Secondary, ages, “unit B”, done-step labels. |
| text-3 | `#5A6572` | Tertiary, empty slot id, pending steps, disabled label. |
| mint | `#3DDC97` | Loaded border, done step, selected chip text and border, Start fill, “Loaded”, “next on runout”. |
| warn | `#F5A524` | Heating, drying arc, pct &lt; 15, “Paused by unit …”. |
| error | `#F2555A` | Stop label, error border, bell badge, “No spool”. |
| info | `#5AA9FF` | Info-severity alerts only. |
| on-light | `#F4F7F8` | Text and ring on a dark filament. |
| on-dark | `#14181D` | Text and ring on a light filament. |
| scrim | `#000000` at opa 179 | Modal dim. |
| mint-dim fill | `#1A4C3A` | Disabled accent button. |
| mint-dim text | `#298A62` | Label on a disabled accent button. |
| danger-dim text | `#672A2F` | Label on a disabled danger button. |

Disabled accent: `mix(#3DDC97, #0B0E12, 178)` → `#1A4C3A`. Disabled accent label: `mix(#3DDC97, #0B0E12, 102)` → `#298A62`. Disabled danger label: `mix(#F2555A, #0B0E12, 153)` → `#672A2F`. `mix` is the channel mix from the 320 spec. Disabled neutral fill is `#12161C`, label `#5A6572`. Enabling a disabled accent animates the fill to `#3DDC97` and the label to `#0B0E12` over 150 ms, linear.

Screen background: vertical gradient, stop 0 `#0B0E12`, stop 255 `#12161C`.

On-filament rule and the two spool gradients are unchanged:

| Spool | F | Top | Bottom | Text |
|---|---|---|---|---|
| PLA red | `#D23B3B` | `#D75353` | `#82292B` | `#F4F7F8` |
| PETG-CF black | L ≤ 0.08 | `#3A3E45` | `#16181C` | `#F4F7F8` |

The lit filament path uses the gradient-top colour (`#D75353` for A1).

## Type and icons

Same six fonts. Do not add a size.

| Font | Role |
|---|---|
| Inter Regular 12 | Vendor, Empty / Unknown, spare line, sheet reason, RH, notes, ages, “unit B”, “Bambu” on the toolhead. |
| Inter Medium 12 | Pill, group chip, in-sheet percents, “Loaded” / “Ready”, toast is Medium 14 (below). |
| Inter Medium 14 | Sheet rows, step labels, toolhead slot and material, chip labels, countdown, ring percent, mapping chips. |
| Inter SemiBold 16 | Screen titles, Heating, Stop / Start / Done, stepper value, group name on the Groups screen. |
| Inter SemiBold 20 | Slot id on a tile. “Loading A3”. Edit hero. Print hero slot id. |
| Inter SemiBold 28 | Environment numerals only (`31`, `24`). The unknown-tile `?` also uses 28. |

Icons: Material Design Icons. 18 px is the default. 24 px is the nav pair (`menu`, `water-percent`) and empty-state illustrations only.

Glyphs: `bell-outline`, `water-percent`, `menu`, `chevron-left`, `chevron-right`, `pencil-outline`, `refresh`, `tray-arrow-down`, `check`, `plus`, `minus`, `close`, `printer-3d-nozzle`, `nfc-variant`, `sync`, `swap-horizontal`, `cog`, `link-variant`, `information-outline`, `alert`, `alert-circle`, `power-plug`, `rotate-right`.

## Grid

Base unit 4 px. Screen inset 12, except the home tiles, which use inset 8. Gaps are 4, 6, or 8.

| Radius | Where |
|---|---|
| 1 | Tab marker. |
| 2 | Swatch, sheet handle, remaining-ring caps are round via the arc, not this radius. |
| 9 | Group chip (h 18). “next on runout” chip uses 11 (h 22). |
| 10 | Tiles, list rows, chips, steppers, pressed icon button, menu tile. |
| 12 | Humidity pill (h 24). Toast uses half its height. |
| 16 | Sheet. Bottom corners are clipped by the screen. |
| 18 | Edit hero swatch (36 px circle). |

Touch target minimum 36×36. A row shorter than 36 is not a button.

Constants:

| Name | Value |
|---|---|
| `UI_SCREEN_W` | 480 |
| `UI_SCREEN_H` | 320 |
| `UI_TOPBAR_H` | 36 |
| `UI_TOPBAR_HOME_H` | 40 |
| `UI_BOTTOMNAV_H` | 48, so the nav is y 272–320 |
| `UI_ACTIONBAR_H` | 36, so the band is y 284–320 |
| Sheet max h | 272, so y = 320 − h, and the scrim above the sheet is at least 48 |

Screens with a band have no nav. Screens with a nav have no band. Home, Groups, Settings, This unit, Unit info, and Print (when it is not in a runout) use the nav. Environment, Loading, Edit, and Mapping use the band. Alerts uses the band when there is a resolved notice to clear, and neither bar when there is not. Print swaps the nav for the band while a runout is active.

The nav’s Env icon is mint with a marker only on a screen that is Environment and still shows the nav. This layout gives Environment a band, so the marker is not on screen. The marker geometry is still defined under Nav so the widget has one implementation.

## Shared widgets

### Top bar, non-home (y 0–36)

No fill. Gradient shows through.

| Item | x | y | w | h |
|---|---|---|---|---|
| Back | 0 | 0 | 36 | 36 |
| Title | centered on x = 240 | 8 | text | 20 |
| Bell | 444 | 0 | 36 | 36 |

Back: `chevron-left` 18, `#F2F5F8`, centered. Title: SemiBold 16 `#F2F5F8`. Bell: `bell-outline` 18, centered. Badge: 8×8 `#F2555A` at local (22, 4) of the bell button when the unread count is &gt; 0. No numeral. Pressed back or bell: fill `#1E252E`, radius 10, for the press only.

### Nav (y 272–320, h 48)

Fill `#0B0E12`. Top border 1 px `#1E252E`. Two tabs, no labels.

| Tab | x | w | Icon | Icon box |
|---|---|---|---|---|
| Menu | 0 | 240 | `menu` 24 | (108, 284, 24, 24) |
| Env | 240 | 240 | `water-percent` 24 | (348, 284, 24, 24) |

Inactive icon `#8D98A5`. Pressed tab: fill `#1E252E` on that 240×48, for the press only. Menu opens the menu sheet and is never the active tab.

Active Env, if the nav is visible on Environment: icon `#3DDC97`. Marker 16×2, radius 1, fill `#3DDC97`, y = 272, x = 352.

### Action band (y 284–320, h 36)

Full width. One label, SemiBold 16, centered.

| Kind | Fill | Label | Top border |
|---|---|---|---|
| Stop | `#1E252E` | `#F2555A` | 1 px `#2A3340` |
| Start, Confirm, Done, Retry | `#3DDC97` | `#0B0E12` | none |
| Neutral (`Pause and load`) | `#1E252E` | `#F2F5F8` | 1 px `#2A3340` |
| Disabled accent | `#1A4C3A` | `#298A62` | none |
| Disabled neutral or Stop | `#12161C` | `#5A6572` or `#672A2F` for Stop | 1 px `#2A3340` |

Two halves, used by Loading-failed and by Edit: each w = 240, h = 36, y = 284. A 1 px `#2A3340` rule at x = 240.

### List row

x = 12, w = 456, radius 10, fill `#161B22`. Pressed fill `#1E252E`. Gap between rows 8 unless a screen says 4 or 6.

| Kind | h | Contents |
|---|---|---|
| One line | 44 | Icon 18 at local x = 12, label Medium 14 at local x = 40, vertically centered. Chevron 18 `#5A6572` at local x = 426. |
| Two line | 48 or 56 | Title Medium 14 at local (40, 8). Subtitle Regular 12 `#8D98A5` at local (40, 28). |
| Settings toggle | 56 | As two-line. Toggle visual 40×22, hit 44×36, right inset 12. On: track `#3DDC97`, knob 18 `#F2F5F8`. Off: track `#2A3340`, knob `#8D98A5`. |

### Chip (h 36)

Radius 10, border width 1, label Medium 14 centered. Min width 72. Pressed, was unselected: fill `#1E252E`, text `#F2F5F8`, border `#161B22`.

| State | Fill | Text | Border |
|---|---|---|---|
| Unselected | `#161B22` | `#F2F5F8` | `#161B22` |
| Selected | `#1E252E` | `#3DDC97` | `#3DDC97` |

### Stepper (h 36)

Radius 10, fill `#1E252E`. Minus segment w = 36, plus segment w = 36, value is the middle. Icons `minus` / `plus` 18 `#F2F5F8`, centered in the segment. Value SemiBold 16 `#F2F5F8`, centered in the middle. Rules: 1×20 `#2A3340` at the segment edges, local y = 8. Pressed segment fills `#2A3340`. The value is not a button.

### Sheet frame

Scrim (0, 0, 480, 320), `#000000` opa 179. A tap on the scrim dismisses. A downward drag of 24 px or more on the sheet dismisses. Both dismiss instantly. No Close, no Cancel.

Sheet x = 0, y = 320 − h, w = 480, fill `#1E252E`, radius 16. h is content height, clamped to 272.

| Item | Local | Size |
|---|---|---|
| Handle | x = 224, y = 8 | 32×4, radius 2, `#5A6572` |
| Title row | y = 22 | h = 20, SemiBold 16 `#F2F5F8` |

Opening: scrim opacity 0 → 179 in 120 ms ease-out, sheet y from (rest + 12) → rest in 140 ms ease-out, together. Closing: delete both the same frame. No close tween.

### Toast

One at a time. A new toast replaces the one on screen. Fill `#1E252E`, radius = h / 2, pad x = 12. Icon `check` 18 `#3DDC97`, gap 8, text Medium 14 `#F2F5F8`. h = 36 for one line. Max text width 360, then wrap and grow h by 16 per extra line. Horizontally centered.

Bottom edge y = 264 when the nav is showing (8 px above it). Bottom edge y = 276 when a band is showing. Opacity 0 → 255 in 120 ms, hold 1500 ms, opacity → 0 in 100 ms, then delete.

Illustrated toast: text `Saved A2`, w = 132, x = 174, y = 228, h = 36, over a nav screen.

## Home

```
 y
  0  top bar          h 40
 40  gap              h  8
 48  tiles            h 150
198  stubs and path   h 10
208  path + toolhead  h 64
272  nav              h 48
320
```

No tab is active. The Env pill’s hit target and the Env tab both open Environment.

### Top bar (y 0–40)

| Item | x | y | w | h | Draw |
|---|---|---|---|---|---|
| `AMS 2 Pro` | 12 | 10 | text | 20 | SemiBold 16 `#F2F5F8` |
| `(A)` | title right + 4 | 12 | text | 18 | Medium 14 `#8D98A5`, baseline aligned with the title |
| Pill | 214 | 8 | 152 | 24 | radius 12, fill `#161B22` |
| Pill hit | pill x − 8 | 2 | pill w + 16 | 36 | Opens Environment. Drawn pill stays 24 px tall. |
| Bell | 432 | 2 | 36 | 36 | `bell-outline` 18 `#F2F5F8` |

This title ends near x = 148, so the pill sits centered in the span from x = 164 to x = 424: span 260, pill 152, x = 164 + (260 − 152) / 2 = 218, drawn at 214 to keep the glyphs on a 2 px grid. Recompute x when the title width changes. Do not center the pill on x = 240 if that overlaps the title.

Pill contents, vertically centered, left pad 14: `31%` Medium 12 `#F2F5F8`, gap 4, `RH` Regular 12 `#8D98A5`, gap 8, rule 1×12 `#5A6572`, gap 8, `24°C` Medium 12 `#F2F5F8`.

While drying, the pill grows to w = 228 and stays centered in that same span. Contents add gap 8, rule 1×12, gap 8, a 6×6 circle `#F5A524`, gap 4, `Heating` Medium 12 `#F5A524`. Temperature stays; the 320 pill had to drop it.

### Tiles

y = 48, h = 150, w = 110, radius 10, border width 2, pad 0. Gap 8, inset 8.

| Slot | x |
|---|---|
| A1 | 8 |
| A2 | 126 |
| A3 | 244 |
| A4 | 362 |

Check: 8 + 110 + 8 + 110 + 8 + 110 + 8 + 110 + 8 = 480. Centers x = 63, 181, 299, 417.

Content box 106×146, origin at tile (2, 2). Coordinates below are in that box.

Filled tile:

| Item | x | y | w | h | Style |
|---|---|---|---|---|---|
| Slot id | 8 | 2 | 60 | 24 | SemiBold 20, on-filament |
| Group chip | 68 | 6 | 28 | 18 | radius 9. Fill `#0B0E12` opa 120. Label Medium 12, on-filament, centered. Text is the group name (`T0`, `T1`). Omit the chip when the bay has no group. |
| Material | 8 | 28 | 90 | 14 | Medium 12, on-filament |
| Vendor | 8 | 44 | 90 | 14 | Regular 12, on-filament, opa 170 |
| Spare | 8 | 62 | 90 | 14 | Regular 12, on-filament, opa 200. `next B2` when the group has a spare (the spare’s slot id). `no spare` at opa 170 when it does not. Omit when the bay has no group. |
| Ring | 27 | 84 | 52 | 52 | stroke 4. Track on-filament opa 70. Indicator on-filament, round caps, from 12 o’clock, sweep = round(pct × 360 / 100). A1 → 295°. A2 → 162°. |
| Percent | centered in the ring | | | 16 | Medium 14, on-filament. `82%`, `45%`. |

If pct &lt; 15, the percent and the indicator use `#F5A524`. The track stays. pct 0 draws no indicator. pct &gt; 0 draws at least 8°.

Loaded: border `#3DDC97` opa 255. That is the only loaded mark on the tile.

Filled, not loaded: border opa 0.

Empty: fill `#161B22`, border opa 0. Slot id SemiBold 20 `#5A6572` at (8, 2). `Empty` Regular 12 `#5A6572`, horizontally centered, y = 64. No chip, no ring, no spare.

Unknown: same fill. Slot id SemiBold 20 `#8D98A5` at (8, 2). `?` SemiBold 28 `#8D98A5`, centered, y = 48, h = 32. `Unknown` Regular 12 `#5A6572`, centered, y = 92.

Error: border `#F2555A`. The spare line becomes the fault (`Feed fail`, `Blocked`) Regular 12 `#F2555A`. Ring stays.

Loading this slot: border `#3DDC97`. The spare line becomes `Loading` Regular 12 `#3DDC97`. A 16×16 arc at content (84, 28), stroke 2, track `#2A3340`, indicator `#3DDC97`, one revolution per 1100 ms.

The whole tile is one button. Pressed, and only while pressed: if not loaded and not in error, border opa 255 and border `#8D98A5`. Loaded keeps mint. Error keeps `#F2555A`.

AMS HT: one tile, 110×150, at x = 185, y = 48. Center x = 240. Do not stretch it.

### Path and toolhead (y 198–272)

Stubs: 2×8, `#2A3340`, y = 198, x = center − 1. This frame: x = 62, 180, 298, 416.

Lit path, stroke 3, round caps and joins, colour = gradient top of the loaded slot. This frame `#D75353`:

```
(63, 206) → (63, 222) → (32, 222) → (32, 237)
```

Nozzle: `printer-3d-nozzle` 18 `#F2F5F8`, box (23, 237, 18, 18), center (32, 246). The path ends at the top of that box.

Filament from another unit or the external spool enters at (0, 222) and runs to (32, 222), then down to (32, 237). Stubs on this unit stay dim.

While this unit loads or unloads, a 6 px disc in the path colour travels that polyline once every 1200 ms, linear.

Toolhead line, one row, vertically centered at y = 250, items from x = 52. No background. This frame:

`T0` Medium 14 `#F2F5F8`, gap 6, `·` Medium 14 `#5A6572`, gap 6, swatch 10×10 radius 2 fill `#D23B3B` (add a 1 px `#5A6572` border only for the charcoal gradient), gap 6, `A1` Medium 14 `#F2F5F8`, gap 4, `PLA` Medium 14 `#F2F5F8`, gap 6, `·` Medium 14 `#5A6572`, gap 6, `Bambu` Regular 12 `#8D98A5`.

The row is a button only when the priority table names a destination.

Priority, one line at a time:

| Mode | Content | Colour | Tap |
|---|---|---|---|
| Error on this unit | `A3 · feed failed` | Medium 14 `#F2555A` | Alerts |
| Paused by another unit | `Paused by unit B` | Medium 14 `#F5A524` | Alerts |
| Runout that involves this unit | `T0 · A1 ran out → B2` | Medium 14 `#F2F5F8` | none |
| This unit loading | `Loading A3` + 16×16 arc | Medium 14 `#F2F5F8` | Loading |
| Another unit loading | `Unit B is loading B3` | Medium 14 `#F2F5F8` | none |
| Toolhead | the line above | as above | none |

## Slot sheet

Home stays live under the scrim. Illustrated on A2 (filled, not loaded, group T1, no spare, no tag).

h = 268, y = 52. That is under the 272 clamp. Scrim band y 0–52.

| Item | Local x, y | Size | Style |
|---|---|---|---|
| Handle | 224, 8 | 32×4 | `#5A6572` |
| Swatch | 16, 26 | 10×10 r 2 | `#3A3E45`, border 1 `#5A6572` |
| `A2` | 32, 22 | h 20 | SemiBold 16 `#F2F5F8` |
| `·` | after A2 + 6 | h 20 | SemiBold 16 `#5A6572` |
| `PETG-CF` | after the dot + 6 | h 20 | SemiBold 16 `#F2F5F8` |
| `45%` | right inset 16, y 24 | h 18 | Medium 14 `#8D98A5`, right aligned |
| Group line | 16, 48 | h 20, w 448 | Medium 14. `T1 · no spare`, the group name `#F2F5F8`, the rest `#8D98A5`. |

A1’s group line is `T0 · next spare B2 (unit B)`. `T0` and `B2` are `#F2F5F8`. `next spare` and `(unit B)` are `#8D98A5`.

Rows. Icon 18 at local x = 16, label Medium 14 at x = 44, chevron 18 `#5A6572` at x = 446. Enabled row pressed: fill `#161B22`.

| Row | Local y | h | Icon | Label |
|---|---|---|---|---|
| Load | 76 | 44 | `tray-arrow-down` `#3DDC97` | `Load (unloads A1 first)` `#F2F5F8` |
| Edit | 120 | 44 | `pencil-outline` `#8D98A5` | `Edit` `#F2F5F8` |
| Change group | 164 | 44 | `swap-horizontal` `#8D98A5` | `Change group…` `#F2F5F8` |
| Re-read | 208 | 56 | `refresh` `#5A6572` at row y + 10 | `Re-read` Medium 14 `#5A6572` at row y + 8. Reason `No tag in this slot` Regular 12 `#8D98A5` at x = 44, row y + 28. No chevron. |

Dividers: h 1, `#2A3340`, x = 44 to 464, at local y = 120, 164, 208 (the top of the next row).

Change group is omitted when the lane is loading, unloading, calibrating, handling a runout, or this bay’s group is the group loaded on the lane. A1’s group is the loaded group, so A1’s sheet has no Change group row and its h drops by 44, to 224, y = 96. Load on the already-loaded bay reads `Unload`.

Disabled row, any of them: label and icon `#5A6572`, no chevron, h grows from 44 to 56, one Regular 12 `#8D98A5` reason. Edit’s reason while printing that slot is `In use`.

### Change-group sheet

Title `Group for A2`. h depends on the group count. Each group is a 44 px row: name Medium 14, member count Regular 12 `#8D98A5` on the same line after a gap of 8, check 18 `#3DDC97` on the current group. Then a 44 px row `New group` with `plus`. Then, if the bay is in a group, a 44 px row `Remove from T1` in `#F2555A`.

`New group` opens a sheet h = 160: title `New group`, one stepper w = 448, x = 16, local y = 56, value the next free `T{n}` (SemiBold 16). Minus and plus walk the free names. One mint button `Create`, local y = 108, x = 16, w = 448, h = 36. No keyboard.

`Remove from T1` opens a confirm sheet h = 148: title `Remove A2 from T1?`, one button `Remove` at local y = 96, x = 16, w = 448, h = 36, fill `#1E252E`, label SemiBold 16 `#F2555A`, top of the button has no extra band. Scrim dismisses.

## Groups

Nav. Back in the top bar. Title `Groups`. List is not scrolling in this frame; five or more member rows scroll inside y = 40–264.

```
 y
  0  top bar            h 36
 40  T0                 h 22
 64  A1 row             h 44
112  B2 row             h 44
162  T1                 h 22
186  A2 row             h 44
234  not in a group     h 36
272  nav                h 48
```

Section name: SemiBold 16 `#F2F5F8` at x = 12, vertically centered in its 22 px band. Only groups that contain a bay on this unit. Members in declared order.

Member row: x = 12, w = 456, h = 44, radius 10, fill `#161B22`.

| Item | Local | Style |
|---|---|---|
| Swatch | (12, 17) 10×10 r 2 | Slot colour. Charcoal gets a 1 px `#5A6572` border. |
| Slot id | (32, 6) h 16 | Medium 14. This unit `#F2F5F8`. Another unit `#8D98A5`. |
| Unit tag | after the id + 6, y = 8 | Regular 12 `#5A6572`. `unit B`. Omitted for this unit. |
| Material | (32, 24) h 14 | Regular 12 `#8D98A5`. `Empty` or `Unknown` when that is the state. |
| State | right inset 12, vertically centered | Medium 12. `Loaded` `#3DDC97`. `Ready` `#F2F5F8`. `Empty` `#5A6572`. `Error` `#F2555A`. |

The spare member also gets a chip, h = 22, radius 11, pad x = 8, fill `#1E252E`, border 1 `#3DDC97`, label Medium 12 `#3DDC97`, text `next on runout`. Right inset 12, vertically centered. The state sits 8 px to the chip’s left. This frame the chip is local x = 324, w = 120, on the B2 row. B2 is ready PLA, swatch `#D23B3B`, same material as the loaded member of T0.

A one-member group shows `No spare` Regular 12 `#5A6572` in the state slot, and no chip. That is A2 / T1.

Unassigned row, y = 234, h = 36. Not a scrolling list.

| Item | x | y | w | h | Style |
|---|---|---|---|---|---|
| `Not in a group` | 12 | 234 | 160 | 36 | Regular 12 `#8D98A5`, vertically centered |
| A3 chip | 188 | 234 | 136 | 36 | radius 10, fill `#161B22`. `A3` Medium 14 `#5A6572` at local x = 12. `Empty` Regular 12 `#5A6572` at local x = 40. Both centered on the chip. |
| A4 chip | 332 | 234 | 136 | 36 | `A4` Medium 14 `#8D98A5`. `Unknown` Regular 12 `#5A6572`. |

A chip tap opens the change-group sheet for that bay. There is no auto-refill toggle.

## Mapping

Title `Mapping`. Band is `Confirm`. List viewport y = 44–276. Row h = 52, gap = 6, x = 12, w = 456, radius 10, fill `#161B22`. Four rows fit: y = 44, 102, 160, 218, last ends 270. A fifth row scrolls inside the viewport.

Illustrated tools:

| y | Line 1 | Members | Pill |
|---|---|---|---|
| 44 | `T0 · PLA` | `A1` highlighted, `B2 · unit B` | `Ready` |
| 102 | `T1 · PETG-CF` | `A2` highlighted | `Ready` |
| 160 | `T2 · TPU` | `No group` | `No spool` |

Line 1: swatch 10×10 at local (12, 10), text Medium 14 `#F2F5F8` at local x = 28, y = 8.

Member chips, local y = 28, h = 22, gap 6, starting at x = 12:

- This unit: fill `#1E252E`, border 1 `#3DDC97`, text Medium 12 `#F2F5F8`, pad x = 6. The highlight is the border.
- Other unit: no fill, no border, text Medium 12 `#8D98A5`, the unit name in the same run (`B2 · unit B`).
- No group: Regular 12 `#5A6572`, the words `No group`, no chip.

Pill, h = 22, right inset 12, local y = 15, pad x = 8, radius 11.

| Pill | Fill | Text | Border |
|---|---|---|---|
| Ready | `#1E252E` | Medium 12 `#3DDC97` | 1 `#3DDC97` |
| No spool | `#161B22` | Medium 12 `#F2555A` | 1 `#F2555A` |

`No spool` is the flag for a tool whose group has no ready member, and for a tool with no group.

Confirm is the disabled accent (`#1A4C3A` / `#298A62`) while any tool is `No spool`. It becomes `#3DDC97` / `#0B0E12` when every tool is Ready. The 150 ms fill animation applies.

## Environment

No nav. Band at y = 284. Back returns home.

```
 y
  0  top bar            h 36
 36  gap                h 12
 48  readout + status   h 72
120  gap                h 12
132  chips              h 36
168  gap                h 12
180  steppers           h 36
216  gap                h 12
228  info card          h 48
276  gap                h  8
284  band               h 36
320
```

Title `Environment`.

### Readout and status (y 48–120)

Numerals SemiBold 28 `#F2F5F8`, baseline y = 100.

| Item | x | Style |
|---|---|---|
| `31` | 20 | SemiBold 28 |
| `%` | 62 | Medium 14 `#8D98A5` |
| `RH` | 78 | Regular 12 `#8D98A5` |
| `24` | 160 | SemiBold 28 |
| `°C` | 206 | Medium 14 `#8D98A5` |
| Ring | box (292, 68, 32, 32), center (308, 84) | stroke 3. Track `#2A3340`. Indicator `#F5A524`, round caps, rotation 270, sweep as in the 320 spec. This frame elapsed 1080 s of 43200 s, sweep 9°. |
| `Heating` | 336, baseline 80 | SemiBold 16 `#F5A524` |
| `11 h 42 m left` | 336, baseline 100 | Medium 14 `#F2F5F8` |

### Chips (y 132–168)

w = 84, h = 36, gap = 8. x = 14, 106, 198, 290, 382. Labels `PLA`, `PETG`, `ABS`, `PA`, `TPU`. PLA selected: fill `#1E252E`, text and border `#3DDC97`. The others are unselected. PLA is the preset that produced 55°C and 12 h.

### Steppers (y 180–216)

| Group | x | w | Value |
|---|---|---|---|
| Temperature | 12 | 220 | `55°C` |
| Time | 248 | 220 | `12 h` |

12 + 220 + 16 + 220 + 12 = 480. Minus and plus are the outer 36 px. Rules at local x = 36 and x = 183, y = 8, size 1×20. Value centered in the middle 148 px.

### Info card (y 228–276)

x = 12, w = 456, h = 48, radius 10, fill `#161B22`.

| Item | x | y | Style |
|---|---|---|---|
| `PLA preset` | 24 | 240 | Medium 14 `#3DDC97` |
| `45–65°C · 1–24 h` | 24 | 258 | Regular 12 `#5A6572` |
| `18 min elapsed` | right edge 452, vertically centered | | Medium 14 `#8D98A5`, right aligned |

18 min is 1080 s, the elapsed part of this 12 h cycle. The range is the unit’s real limit, swapped in when the unit reports a different one (HT is 45–85°C). The card does not repeat the stepper values as a second control.

### Band

This frame is Stop: fill `#1E252E`, top border 1 `#2A3340`, label `Stop` SemiBold 16 `#F2555A`.

### Cooling

The unit is cooling down after a dry cycle. Ring stays 32×32, indicator `#8D98A5`, sweep shows remaining cool-down the same way Heating shows elapsed dry time. Line 1 `Cooling` SemiBold 16 `#8D98A5`. Line 2 the remaining time Medium 14 `#F2F5F8` when the unit reports one, otherwise `Chamber cooling` Regular 12 `#8D98A5`. Chips, steppers, and the info card stay, so the next cycle can be set. The band is Start, disabled accent, until the unit accepts a new cycle, then Start in mint.

### Idle, drying available

Ring hidden. Line 1 `Idle` SemiBold 16 `#8D98A5` at the same (336, 80). Line 2 is absent, because the range lives on the info card. Chips and steppers stay. Band is Start, fill `#3DDC97`, label `#0B0E12`.

### No heater, or no adapter

The readout row only: `31`, `%`, `RH`, `24`, `°C`, in the same positions. No ring, no Heating / Idle / Cooling, no chips, no steppers, no info card, no band, no explanatory sentence. The open area below y = 120 stays the screen gradient.

## Loading

No nav, no back. Band is the exit.

```
 y
  0  header            h 64
 64  gap               h  8
 72  five steps        h 200  (5 × 40)
272  gap               h 12
284  Stop              h 36
320
```

Header:

| Item | x | y | Style |
|---|---|---|---|
| `Loading A3` | 16 | 8 | SemiBold 20 `#F2F5F8`, h = 28 |
| Swatch | 16, 44 | 10×10, radius 5, `#D23B3B` |
| `PLA · Red` | 34, 42 | Medium 14. `PLA` `#F2F5F8`, `·` `#5A6572`, `Red` `#8D98A5`. Gaps 6. |

Unload uses the same regions. The title is `Unloading A1`.

Steps. Row i has y = 72 + i × 40, h = 40.

| i | y | Label | State in this frame |
|---|---|---|---|
| 0 | 72 | Heat the nozzle | done |
| 1 | 112 | Cut filament | done |
| 2 | 152 | Pull back current filament | active |
| 3 | 192 | Push new filament | pending |
| 4 | 232 | Purge | pending |

Circle 16×16 at (16, row y + 12). Center (24, row y + 20). Label Medium 14 at x = 44, vertically centered. The label column is w = 420.

Rail: x = 23, w = 2, from y = 92 to y = 252 (first center to last center), `#2A3340`. A second 2 px segment `#3DDC97` lies on it from y = 92 to the active center. This frame the active center is y = 172.

| State | Circle | Label |
|---|---|---|
| Done | Fill `#3DDC97`. `check` drawn in 10 px, `#0B0E12`. | `#8D98A5` |
| Active | Stroke 2 `#2A3340` full circle. Indicator stroke 2 `#3DDC97`, 0 → 360 in 1100 ms, linear, infinite. | `#F2F5F8` |
| Pending | Stroke 1 `#5A6572`. No fill. | `#5A6572` |
| Failed | Fill `#F2555A`. `close` in 10 px, `#0B0E12`. | `#F2555A` |

### Failed

The failed row uses the failed circle. Later rows stay pending. The band becomes two halves:

| Half | x | w | Fill | Label |
|---|---|---|---|---|
| Retry | 0 | 240 | `#3DDC97` | `Retry` `#0B0E12` |
| Stop | 240 | 240 | `#1E252E` | `Stop` `#F2555A` |

Rule 1×36 `#2A3340` at x = 240.

### Done

Every circle is done. The rail is `#3DDC97` for its full length. The band is fill `#3DDC97`, label `Done` SemiBold 16 `#0B0E12`. A tap cuts home. With no tap, home cuts in at 400 ms. The headline stays `Loading A3` during that interval.

## Edit

Title `Edit A2`. No nav. Band is two halves. Illustrated values: vendor Generic, material PETG-CF, PA 0.020, colour `#22262B` (Charcoal). Save is dimmed until a value changes.

```
 y
  0  top bar       h 36
 44  hero          h 64
116  labels        h 16
132  steppers      h 36
180  labels        h 16
196  PA + Color    h 36
244  note          h 32
284  band          h 36
```

Hero:

| Item | x | y | w | h | Style |
|---|---|---|---|---|---|
| Colour button | 16 | 52 | 36 | 36 | Circle, radius 18. Fill `#3A3E45`, border 1 `#5A6572` because L ≤ 0.08. Tap opens the colour sheet. |
| `A2 · PETG-CF` | 64 | 56 | text | 24 | SemiBold 20 `#F2F5F8` |
| `Generic` | 64 | 84 | text | 16 | Regular 12 `#8D98A5` |

A tag-read spool inserts `nfc-variant` 18 `#8D98A5` at (64, 82) and moves the vendor to x = 88.

Labels Regular 12 `#8D98A5`, h = 16: `Vendor` and `Material` at y = 116; `PA` and `Color` at y = 180. Column x = 12 and x = 248.

Steppers y = 132 and the PA stepper y = 196: each w = 220, h = 36. Values `Generic`, `PETG-CF`, `0.020`. PA shows three digits and steps by 0.001. Vendor walks Bambu, Prusament, Polymaker, eSUN, Sunlu, Generic. Material walks PLA, PETG, ABS, ASA, TPU, PA, PC, PLA-CF, PETG-CF, PVA.

Colour field, same slot as a stepper: x = 248, y = 196, w = 220, h = 36, radius 10, fill `#1E252E`. Swatch 16×16 radius 2 at local (10, 10). Name Medium 14 `#F2F5F8` at local x = 34, vertically centered (`Charcoal`). Chevron 18 `#5A6572` at local x = 190. Tap opens the colour sheet.

Note slot y = 244, h = 32, x = 16. Empty in this frame. A tag-read spool shows `nfc-variant` 18 `#8D98A5` and `Read from tag` Regular 12 `#8D98A5`, vertically centered. The steppers and the colour field then use the disabled treatment: fill `#12161C`, label `#5A6572`, and they do not receive taps.

Band:

| Half | x | Clean | Dirty |
|---|---|---|---|
| Reset | 0–240 | fill `#12161C`, `Reset` `#5A6572`, not clickable | fill `#1E252E`, `Reset` `#F2F5F8` |
| Save | 240–480 | fill `#1A4C3A`, `Save` `#298A62` | fill `#3DDC97`, `Save` `#0B0E12` |

Top border 1 `#2A3340` on the Reset half only. The Save fill animates over 150 ms when the form becomes dirty.

Reset opens a sheet h = 160. Title `Reset A2?` at local y = 22. Body Regular 14 `#8D98A5` at local (16, 52), w = 448, h = 40: `Clears vendor, material, colour and PA. The slot shows ? until you set it again.` Button `Reset` at local (16, 108), w = 448, h = 36, fill `#1E252E`, label `#F2555A`.

### Colour sheet

Title `Color`. h = 240, y = 80. Eighteen swatches, 6 columns × 3 rows, each 44×44, gap 12. Grid origin local (78, 56). 6 × 44 + 5 × 12 = 324, so the grid runs x = 78 to 402.

| | 0 | 1 | 2 | 3 | 4 | 5 |
|---|---|---|---|---|---|---|
| row 0 | `#FFFFFF` White | `#E8E8E8` Light grey | `#8A9099` Grey | `#22262B` Charcoal | `#D23B3B` Red | `#E08A30` Orange |
| row 1 | `#F2C438` Yellow | `#2ECC71` Green | `#1F8A70` Teal | `#2F9EC4` Cyan | `#2F6FB2` Blue | `#3A3AD2` Indigo |
| row 2 | `#6A3FA0` Purple | `#B03AA0` Magenta | `#E0609A` Pink | `#8B5A2B` Brown | `#F5F5DC` Beige | `#000000` Black |

The swatch is the colour. No caption inside it. `#FFFFFF`, `#F5F5DC`, `#22262B`, and `#000000` get a 1 px `#5A6572` border. The selected swatch gets a 2 px `#3DDC97` border, drawn inside the 44 px. A tap writes the colour, closes the sheet, and marks the form dirty. Stored `#000000` and any F with L ≤ 0.08 still paint the tile with the charcoal gradient `#3A3E45` → `#16181C`.

## Alerts

Title `Alerts`. The bell on every screen opens this one.

With at least one resolved notice, the band is `Clear resolved` in the neutral style (fill `#1E252E`, label `#F2F5F8`) and the list viewport is y = 44–276. With none resolved, the band is absent and the viewport is y = 44–312, with the nav also absent: Alerts is a pushed screen, back returns to the previous one. The band is the only bottom chrome this screen uses.

Card x = 12, w = 456, radius 10, fill `#161B22`, gap 8. A 3 px severity bar at the card’s left edge, full height: stop `#F2555A`, pause `#F5A524`, info `#5AA9FF`.

| Card | y | h | Title | Age | Body |
|---|---|---|---|---|---|
| Active | 44 | 80 | `Feed failed` Medium 14 `#F5A524` at local (16, 12) | none | Regular 12 `#F2F5F8` at local (16, 34), w = 424, two lines: `AMS A slot 3 failed to send out PLA (red). The extruder might be clogged.` |
| Pause | 132 | 64 | `A3 pause` Medium 14 `#F5A524` at local (16, 12) | `2 min ago` Regular 12 `#5A6572`, right inset 12, local y = 12 | `A3: failed to send out PLA (red)` Regular 12 `#F2F5F8` at local (16, 34) |
| Info | 204 | 64 | `info` Medium 14 `#5AA9FF` | `11 min ago` | `Loading A3 (PLA, red)` |

A card tap opens the recovery sheet for that notice. Further notices scroll inside the viewport. Age format: under 60 min `2 min ago`, under 24 h `2h ago`, else `1d ago`.

### Recovery sheet

Illustrated for the active feed failure. h = 232, y = 88.

| Item | Local | Style |
|---|---|---|
| Handle | 224, 8 | 32×4 |
| `Feed failed` | 16, 22 | SemiBold 16 `#F2F5F8`, h = 20 |
| `0700-8003` | 16, 46 | Regular 12 `#5A6572`, h = 16 |
| Body | 16, 70, w = 448, h = 40 | Medium 14 `#F2F5F8`, two lines, the same sentence as the card |
| `Between the AMS and the extruder` | 16, 118 | Regular 12 `#8D98A5` |
| Slot | swatch (16, 146) 10×10 r 2 `#D23B3B`, text x = 32 | Medium 14 `#F2F5F8`: `A3 · PLA · red` |
| Button | 16, 184, w = 448, h = 36 | radius 10, fill `#3DDC97`, label SemiBold 16 `#0B0E12`: `Retry (problem solved)` |

The button label is the claim for that fault (`Resume (problem solved)`, `Finished, continue`, `OK`, `Stop drying`, `Filament extruded, continue`). A claim that aborts (`Stop drying`) uses fill `#1E252E` and label `#F2555A`. Every other claim is the mint button. One button. Scrim or swipe dismisses.

## Print status

Title `Printing`.

### Not printing

Nav. Body y = 36–272. Empty state centered in that rect: `printer-3d-nozzle` 24 `#5A6572`, then 8 px, then `Not printing` Medium 14 `#8D98A5`.

### Printing, this unit’s bay

Nav, unless a runout is active. Hero card x = 12, y = 44, w = 456, h = 88, radius 10, fill `#161B22`.

| Item | x | y | Size | Style |
|---|---|---|---|---|
| Swatch | 16 | 30 | 28×28, radius 6 | `#D23B3B` |
| `A1` | 56 | 22 | h 24 | SemiBold 20 `#F2F5F8` |
| `· T0` | after A1 + 6, y = 26 | h 18 | Medium 14 `#8D98A5` |
| `PLA · Bambu` | 56 | 52 | h 18 | Medium 14 `#8D98A5` |
| Ring | 396 | 22 | 44×44, stroke 3 | Track `#2A3340`. Indicator `#3DDC97`, or `#F5A524` when pct &lt; 15. Sweep from the remaining percent. |
| Percent | centered in the ring | | Medium 12 `#F2F5F8` | `82%`. Unknown remaining is `?` and the indicator is hidden. |

Line under the card, x = 12, y = 140, h = 18, Regular 12 `#8D98A5`: `Filament change 3`. When a change is in progress: `Filament change 3 · Pull back current filament`.

Another unit in the toolhead: the hero slot id is `B2`, the `· T0` suffix becomes `· unit B`, and a line at y = 164 reads `This unit is on standby` Regular 12 `#8D98A5`.

### Runout

Replaces the nav with the band. Card x = 12, y = 168, w = 456, h = 48, radius 10, fill `#161B22`. Icon `sync` 18 `#3DDC97` at local (12, 15). Text Medium 14 `#F2F5F8` at local x = 40, vertically centered: `T0 · A1 ran out → B2`. Band label `Pause and load`, neutral style.

## Settings

Title `Settings`. Nav. Viewport y = 44–264, x = 12, w = 456. It scrolls.

Section label: h = 24, Medium 12 `#8D98A5`, text at local y = 6. Gap 8 under the label, gap 8 between rows.

Toggle row h = 56. The note fits on one line at this width, which is why the row is 56 and not a wrapped 72.

| Section | Row | Note | Illustrated |
|---|---|---|---|
| Filament detection | Read filament on insertion | Read the spool tag when a spool is inserted (~20 s) | on |
| | Read filament on startup | Read every spool tag at power-on (~1 min) | on |
| | Remaining filament estimation | Estimate how much filament is left from tag reads | off |
| Runout | Toolhead runout detection | Pause when the toolhead sensor sees no filament | on |
| Drying | Rotate spools while drying | Turn the spools during a drying cycle | off |
| This display | This display | `AMS 2 Pro (A)` | chevron, opens This unit |
| | Unit info | Model, serial and firmware | chevron, opens Unit info |
| | Groups | Named groups and the order they back each other up | chevron, opens Groups |

Icons, 18 px `#8D98A5`, local x = 12: `nfc-variant`, `power-plug`, a filament glyph, `alert`, `rotate-right`, `link-variant`, `information-outline`, `sync`.

First viewport, so the extra width is visible as three full rows:

| y | What |
|---|---|
| 44 | section `Filament detection` h 24 |
| 68 | insertion, on, h 56 |
| 132 | startup, on, h 56 |
| 196 | remaining, off, h 56, ends 252 |

`Runout` starts at y = 260, under the nav, and scrolls up. There is no auto-refill toggle. Group order is the whole policy.

Nav rows are h = 48, title Medium 14, subtitle Regular 12 `#8D98A5`, chevron at local x = 426.

## This unit

Title `This unit`. Nav. Viewport y = 44–264.

Section `Cable order`, h = 24, Medium 12 `#8D98A5`, y = 44.

Unit row h = 48, x = 12, w = 456, radius 10, fill `#161B22`, gap 8.

| y | Line | Trailing |
|---|---|---|
| 76 | `A · AMS 2 Pro` Medium 14, sub `4 slots` Regular 12 `#8D98A5` | pill `This display`, h 22, pad x 8, fill `#1E252E`, border 1 `#3DDC97`, text Medium 12 `#3DDC97` |
| 132 | `B · AMS 2 Pro`, sub `4 slots` | chevron. Tap opens the bind sheet. |

A single toolhead draws no Feeds row. A dual-extruder unit adds, at the next row slot, title `Feeds`, subtitle `Left nozzle / Right nozzle`, and two chips `Left` and `Right` (the chip widget, w = 72) in the trailing area. The chip for the bound side is selected. While this unit’s filament is in the toolhead the chips are disabled and the subtitle is `Unload first`.

`Re-scan units` is the next row, h = 48, icon `refresh`, chevron. It opens the rescan sheet.

Bind sheet, h = 148, y = 172. Title `Bind to B (AMS 2 Pro)?`. Body Regular 14 `#8D98A5` at local (16, 52): `This display will show unit B.` Button `Bind` at (16, 96), w = 448, h = 36, mint.

Rescan sheet, h = 168, y = 152. Title `Re-scan units?`. Row at local y = 52, h = 44: icon `refresh`, title `Unplug and replug each unit` Medium 14, reason `IDs follow cable order, not physical position.` Regular 12 `#8D98A5`. Button `Re-scan` at (16, 116), w = 448, h = 36, mint.

Waiting replaces the list. Two rows at y = 76 and y = 132, h = 48: `Waiting for units…` with the replug sentence as the subtitle, and `Cancel` with icon `close`. Cancel returns to the cable-order list.

## Unit info

Title `Unit info`. Nav. Read-only. No chevrons.

Hero y = 44, h = 48, x = 16. `AMS 2 Pro` SemiBold 20 `#F2F5F8`. `(A)` Medium 14 `#8D98A5`, gap 6, baseline aligned.

Rows y = 100, 156, 212. h = 48, x = 12, w = 456, radius 10, fill `#161B22`. Label Medium 14 `#8D98A5` at local x = 12. Value Medium 14 `#F2F5F8`, right inset 12, vertically centered.

| Row | Label | Value |
|---|---|---|
| 100 | Serial | `0M10A35104729` |
| 156 | Firmware | `01.07.00.00`, and a pill `Update available` immediately left of the value, h 22, the same mint pill as This display |
| 212 | Latest | `01.09.00.00` |

The pill is omitted when firmware and latest are equal. Icons 18 `#8D98A5` at local x = 12 (`tag` stand-in is the serial row’s label with no extra glyph if the font has no tag; use `information-outline` for firmware and `sync` for latest), and the label then starts at local x = 40. Right-side values stay right-aligned.

## Menu sheet

Opened by the Menu tab. h = 200, y = 120. Title `Menu` at local (16, 22).

Grid of 8 tiles. Tile 104×64, radius 10, fill `#161B22`, pressed `#2A3340`. Gap 8. Origin local (20, 52).

| | x 20 | x 132 | x 244 | x 356 |
|---|---|---|---|---|
| y 52 | Env | Alerts | Print | Groups |
| y 124 | Map | Setup | This unit | Info |

Icon 18 `#F2F5F8`, centered at local (52, 22). Label Regular 12 `#F2F5F8`, centered at local (52, 46).

| Tile | Icon | Opens |
|---|---|---|
| Env | `water-percent` | Environment |
| Alerts | `bell-outline` | Alerts |
| Print | `printer-3d-nozzle` | Print status |
| Groups | `sync` | Groups |
| Map | `swap-horizontal` | Mapping |
| Setup | `cog` | Settings |
| This unit | `link-variant` | This unit |
| Info | `information-outline` | Unit info |

The sheet closes, then the screen cuts. 104 px is what fits `This unit` in Regular 12 with 8 px of pad on each side. The 320 grid used 70 px tiles and clipped that label.

## Motion

| What | Property | From → to | Duration | Curve |
|---|---|---|---|---|
| Scrim, opening | opacity | 0 → 179 | 120 ms | ease-out |
| Sheet, opening | y | rest + 12 → rest | 140 ms | ease-out |
| Sheet and scrim, closing | — | deleted | 0 | — |
| Active step arc, loading arc on a tile, loading arc on the toolhead | indicator end | 0 → 360 | 1100 ms | linear, infinite. One arc at a time. |
| Path dot | position along the polyline | start → end | 1200 ms | linear, infinite, only while this unit loads or unloads |
| Drying or cooling ring | indicator end | previous sweep → new sweep | 200 ms | ease-out, when the minute changes |
| Save fill | fill and label colour | dim mint → mint | 150 ms | linear, when the form becomes dirty |
| Toast | opacity | 0 → 255, hold, → 0 | 120, 1500, 100 ms | ease-out, linear, ease-in |

Screen changes are a cut. That includes home → environment, home → loading, and Done → home.

## Copy on the four frames

Home: `AMS 2 Pro`, `(A)`, `31%`, `RH`, `24°C`, `A1`, `T0`, `PLA`, `Bambu`, `next B2`, `82%`, `A2`, `T1`, `PETG-CF`, `Generic`, `no spare`, `45%`, `A3`, `Empty`, `A4`, `?`, `Unknown`, `T0`, `A1 PLA`, `Bambu`.

Groups: `Groups`, `T0`, `A1`, `PLA`, `Loaded`, `B2`, `unit B`, `Ready`, `next on runout`, `T1`, `A2`, `PETG-CF`, `No spare`, `Not in a group`, `A3`, `Empty`, `A4`, `Unknown`.

Environment: `Environment`, `31`, `%`, `RH`, `24`, `°C`, `Heating`, `11 h 42 m left`, `PLA`, `PETG`, `ABS`, `PA`, `TPU`, `55°C`, `12 h`, `PLA preset`, `45–65°C · 1–24 h`, `18 min elapsed`, `Stop`.

Loading: `Loading A3`, `PLA`, `·`, `Red`, `Heat the nozzle`, `Cut filament`, `Pull back current filament`, `Push new filament`, `Purge`, `Stop`.

Degree sign is `°` with no space before `C`. Stepper hours are `12 h`. The countdown is `11 h 42 m left`. Elapsed is `18 min elapsed`.

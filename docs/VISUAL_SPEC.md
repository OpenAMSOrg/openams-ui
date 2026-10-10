# Visual spec (designer's hand-off, adopted)

This is the designer's 320x240 build spec, adopted as written, with the product decisions below layered on top. Where a decision below and the spec disagree, the decision wins. `docs/DESIGN.md` explains how the code implements it (tokens, components).

## Product decisions on top of the spec

1. **Geometry is global.** Home: top bar 0–40, tiles 46–150, toolhead row 156–192, nav 192–240 (48 px). Every other screen: top bar 0–36; if it has a bottom nav it is the same 48 px nav at 192–240; if it has a main action it has the 36 px action band at 204–240 instead (no nav). The constants live in `ui_theme.h` (`UI_TOPBAR_H` 36, `UI_TOPBAR_HOME_H` 40, `UI_BOTTOMNAV_H` 48, `UI_ACTIONBAR_H` 36).
2. **Toolhead row priorities** (home, one line, text only, no background): an error on this unit (`A3 · feed failed`, error color, tap opens Alerts) > `Paused by unit B` (warn, tap opens Alerts) > a runout that involves this unit (`A1 ran out → B2`, primary text) > this unit loading (`Loading A3` + the 16 px arc, tap opens the load screen) > another unit loading (`Unit B is loading B3`) > the toolhead contents (`Toolhead` + swatch + `B2 PETG` + `from unit B`). The row is tappable only in the cases that name a destination.
3. **Tiles carry no tag/manual icon.** Whether filament info came from a tag shows in the slot sheet header (a small `nfc-variant` icon before the percent) and on the edit screen.
4. **Loading, finished:** the band becomes mint `Done` and returns home. A failed step shows an error circle (fill `#F2555A`, `close` icon) and the band reads `Retry` (mint) | `Stop` (error) as two halves.
5. **Rotate spools** is a toggle in Settings under "Drying". **Power adapter** is status only on the environment screen (`No adapter` line, Start disabled).
6. **Sheets close instantly** (no closing animation); opening follows the Motion table. Any sheet uses the same frame: y = 240 − height, height clamped to 192, radius 16, handle, title row at local y 22, rows as in the slot sheet.
7. **AMS HT home:** the single tile at x 123 as specified; the drying state and temperature live in the top-bar pill.
8. **Icons:** 18 px everywhere (`UI_FONT_ICON`); a 24 px set exists only for empty-state illustrations (`UI_FONT_ICON_L`).

9. **Home keeps the filament path (owner's call, overrides "The toolhead pipe is gone" and the 104 px tiles).** Tiles are 73×86 at y 44 so a path zone fits at y 130–190: a dim 2×8 feeder stub under every bay, a nozzle icon centered at (21, 172), and the lit path of the slot in the toolhead (3 px, in the filament's gradient-top color) running down from its bay to y 146, left to x 21 and down into the nozzle. Filament from another unit or the external spool enters from the left screen edge instead. While this unit loads or unloads, a 6 px dot travels the lit path every 1.2 s. The toolhead text row sits right of the nozzle (x 40–308, y 156–190).

10. **Two icon-only tabs: Menu | Env (owner).** The bottom nav has no Alerts tab (the bell in the top-right corner opens Alerts) and no text labels: a 24 px menu icon on the left (opens the screens sheet), a 24 px water-percent icon on the right (Environment; mint with the marker when active). Same 48 px bar. There is no "Screens" button and no hamburger.
11. **Show only what is available (owner).** A screen does not explain what the unit cannot do: an environment screen for a unit that cannot dry right now (no heater, or no power adapter) shows only its readouts, with no "cannot dry" text and no drying controls.
12. **Disabled buttons are dimmed versions of themselves (owner).** A disabled accent button is a muted dark mint with muted mint text, a disabled neutral or danger button keeps its label in a faint/muted color on a darker fill; enabling animates the fill (150 ms) so an edit visibly "lights up" Save. Popups have no Close or Cancel buttons: tapping outside a sheet dismisses it, and a confirmation sheet has only its action button.

---

# AMS clip screen — 320×240 LVGL spec

Build target: 320×240 landscape, LVGL 9, 16-bit color, resistive touch, ESP32-class RAM. If a mockup frame disagrees with a number in this file, this file wins. The frames are the visual direction (type, color, density). They also invent a margin around the UI, a “LOADED” caption, a texture on the black spool, a solid mint PLA chip, and a glow on the active step. None of those are in the product.

Allowed drawing: solid fills, vertical or horizontal linear gradients, rounded rects, 1–2 px strokes, arcs, lines, pre-rendered anti-aliased fonts, an icon font, and position or opacity animation of a single small object. No blur, no backdrop, no drop shadow, no texture, no full-screen fade.

Touch targets are at least 36×36. Text is at least 12 px. Icons are 18 px, one size, so the icon font is loaded once.

## Decisions against the previous pass

Kept from the proposed system: background gradient `#0B0E12` → `#12161C`, card `#161B22`, elevated `#1E252E`, text `#F2F5F8` / `#8D98A5` / `#5A6572`, mint `#3DDC97` only as the primary action / active tab / loaded slot, warn `#F5A524`, error `#F2555A`, info `#5AA9FF`, card radius 10, a status pill, filament color as the hero with luminance-aware text, a slim remaining bar, a three-tab bar with an accent marker, and a sheet that moves 12 px under a scrim.

Changed:

- The humidity pill sits in the top bar. A second row would cost the tiles 24 px.
- A filled slot tile is the filament gradient edge to edge. There is no gray footer under the color.
- The remaining bar uses the on-filament text color, not mint. Below 15% the percent and the bar use warn.
- “Heating” is warn, not mint. Mint means loaded, done, selected, or the one primary tap.
- The selected material chip is not a mint fill. Fill `#1E252E`, mint text, 1 px mint border.
- Stop is the elevated bottom band with error-colored text. It is not a filled orange or red button.
- Home has no pager, no “Screens” control, and no back chevron. The device is clipped to one unit. The title is the identity.
- The toolhead pipe is gone. One text row replaces it.
- Empty and unknown are the same 73×104 tile as a filled slot. The row does not break.
- A near-black spool is the fixed charcoal gradient below, never `#000000` and never a texture.
- Sheet actions are rows. No Close row, no extra summary card.
- Rotate and Adapter are not on the environment screen. They belong to Menu. If Start is blocked, the status line says why and the Start band is disabled.
- Environment, loading, and the open sheet do not show the tab bar.
- Screen changes are a cut. Motion is limited to the list in “Motion”.

## Palette

| Token | Hex | Use |
|---|---|---|
| bg-top | `#0B0E12` | Screen gradient start (top). Also the nav fill, and the label on a mint fill. |
| bg-bot | `#12161C` | Screen gradient end (bottom). |
| card | `#161B22` | Empty/unknown tiles, pill, unselected chips, pressed sheet row. |
| elev | `#1E252E` | Sheet, stepper groups, selected chip, Stop band, pressed icon button. |
| hair | `#2A3340` | Dividers, ring track, inactive step rail, stepper separators, nav top rule. |
| text | `#F2F5F8` | Primary text. |
| text-2 | `#8D98A5` | Secondary text, done-step labels, sheet reason. |
| text-3 | `#5A6572` | Tertiary, disabled action label, pending steps, empty slot id. |
| mint | `#3DDC97` | Loaded tile border, active tab, done step, selected chip text and border, Start fill, Load icon, completed rail. |
| warn | `#F5A524` | Heating label and drying arc. Remaining percent and bar when pct < 15. “Paused by unit …” line. |
| error | `#F2555A` | Stop label, error tile border, error line on a tile, bell badge. |
| info | `#5AA9FF` | Info-severity alerts only. Not used on these four screens. Not the humidity pill. |
| on-light | `#F4F7F8` | Text, icons, and the remaining bar on a dark filament. |
| on-dark | `#14181D` | Text, icons, and the remaining bar on a light filament. |
| scrim | `#000000` at opa 179 | Modal dim. 179/255 ≈ 70%. |

Screen background is one vertical gradient, stop 0 `#0B0E12`, stop 255 `#12161C`. Nav, sheet, chips, steppers, and the Stop band are solid. The only other gradients are filled slot tiles (at most four).

## On-filament color

Source filament color `F`. Relative luminance, sRGB:

```
lin(c) = c <= 0.04045 ? c/12.92 : ((c+0.055)/1.055)^2.4
L      = 0.2126*lin(R) + 0.7152*lin(G) + 0.0722*lin(B)
```

with R,G,B in 0–1. If `L >= 0.42` use `#14181D`, otherwise `#F4F7F8`. One decision per tile, from `F` itself, not from the gradient stops.

Gradient, vertical, painted as the tile’s own background (under the border, not as a child):

- If `L > 0.08`: top = mix(F, `#FFFFFF`, 12%), bottom = mix(F, `#0B0E12`, 40%).
- If `L ≤ 0.08`: top = `#3A3E45`, bottom = `#16181C`. This is the black-spool case.

`mix(A, B, t)` per channel: `(A*(255-t) + B*t) / 255`, with t = 31 for 12% and t = 102 for 40%.

Precomputed for this design:

| Spool | F | Top | Bottom | Text |
|---|---|---|---|---|
| PLA red | `#D23B3B` | `#D75353` | `#82292B` | `#F4F7F8` |
| PETG-CF black | ≤ 0.08 | `#3A3E45` | `#16181C` | `#F4F7F8` |

Vendor text uses the same on-filament color at opacity 170. Everything else on the tile is opacity 255.

## Type

Pre-render these six fonts, 4 bpp, subset to the characters actually used (ASCII punctuation that appears below, plus `°` U+00B0, `·` U+00B7, `–` U+2013). Do not mount a TTF at runtime.

| Font | Role |
|---|---|
| Inter Regular 12 | Vendor, “Empty” / “Unknown”, nav labels, sheet reason, “RH”, toolhead prefix “Toolhead”, “from unit B”. |
| Inter Medium 12 | Pill text. Material name on a tile (`PLA`, `PETG-CF`). |
| Inter Medium 14 | Sheet rows, step labels, toolhead slot and material, chip labels, “11 h 42 m left”, “Red”, “45%” on the sheet. |
| Inter SemiBold 16 | Screen titles, “Heating”, “Stop” / “Start”, tile percent, stepper value, sheet title. |
| Inter SemiBold 20 | Slot id on a tile (`A1`…`A4`). Loading headline “Loading A3”. |
| Inter SemiBold 28 | The two environment numerals only (`31`, `24`). |

Icon font: Material Design Icons, 18 px, 4 bpp, these glyphs only.

`bell-outline`, `water-percent`, `menu`, `chevron-left`, `chevron-right`, `pencil-outline`, `refresh`, `tray-arrow-down`, `check`, `plus`, `minus`.

## Grid, radii, borders

Base unit is 4 px. Gaps used below are 4, 6, or 8.

| Radius | Where |
|---|---|
| 1 | Remaining bar. Tab marker (2 px tall, so the ends are round). |
| 2 | Filament swatch, sheet handle. |
| 10 | Tiles, chips, stepper groups, pressed icon-button fill. |
| 12 | Humidity pill (height 24, so it is a capsule). |
| 16 | Sheet. The bottom of the sheet is the bottom of the screen, so the bottom corners are clipped and only the top reads as rounded. |

Every slot tile has `border_width` 2 even when the border is invisible (`border_opa` 0). The loaded mint border then does not change the content box. Every chip has `border_width` 1 for the same reason; unselected chips use border color `#161B22`.

LVGL draws the border inside the widget and insets children by border + pad. Tiles and chips use `pad` 0. Child coordinates in the tile section are in that content space: screen position minus the widget origin minus `(2, 2)` for a tile, minus `(1, 1)` for a chip.

## Home — 320×240

```
 y
  0  top bar          h 40
 40  gap              h  6
 46  slot tiles       h 104
150  gap              h  6
156  toolhead row     h 36
192  nav              h 48
240
```

No tab is active on home. The marker is absent. Environment is a different screen, reached from the Env tab or from the pill’s hit rect.

### Top bar (y 0–40)

No bar background. The screen gradient shows through.

| Item | x | y | w | h | Draw |
|---|---|---|---|---|---|
| Title `AMS 2 Pro` | 12 | 10 | text | 20 | SemiBold 16, `#F2F5F8` |
| Suffix `(A)` | title right + 4 | 12 | text | 18 | Medium 14, `#8D98A5`, baseline aligned with the title (bottom-align, then up 1 px) |
| Pill | 146 | 8 | 126 | 24 | radius 12, fill `#161B22`, no border |
| Pill hit rect | 116 | 2 | 160 | 36 | Invisible button. Opens Environment. The drawn pill stays 24 px. |
| Bell | 282 | 2 | 36 | 36 | Transparent. Icon `bell-outline` 18, `#F2F5F8`, centered. |

Pill contents, vertically centered, left pad 8, in this order: `31%` Medium 12 `#F2F5F8`, 3 px, `RH` Regular 12 `#8D98A5`, 6 px, a 1×12 rule `#5A6572`, 6 px, `24°C` Medium 12 `#F2F5F8`. The pill is centered in the gap between the title block and the bell, not centered on the 320 px screen. At this width a screen-centered pill collides with `AMS 2 Pro (A)`.

Badge, only when the unread count is greater than 0: an 8×8 circle `#F2555A` at local (22, 4) inside the bell button. No numeral. The count lives on the Alerts screen. The home frame has no badge.

Pressed bell or back: fill `#1E252E`, radius 10, for the pressed state only. No animation.

While this unit is drying, the pill’s right half changes: keep `31%` and `RH`, replace the rule and `24°C` with a 6×6 circle `#F5A524` and the word `Heating` in Medium 12 `#F5A524`. Temperature during a dry cycle is on the environment screen.

### Slot tiles

Four tiles, y = 46, h = 104, w = 73, radius 10, border width 2, pad 0.

| Slot | x |
|---|---|
| A1 | 8 |
| A2 | 85 |
| A3 | 162 |
| A4 | 239 |

Check: 8+73+4+73+4+73+4+73+8 = 320.

Content box inside the border: 69×100, origin at tile (2, 2). Coordinates below are in that content box.

Filled tile (A1, A2):

| Item | x | y | w | h | Style |
|---|---|---|---|---|---|
| Background | — | — | widget | widget | Vertical gradient from the table above |
| Slot id | 6 | 2 | 57 | 22 | SemiBold 20, on-filament |
| Material | 6 | 24 | 57 | 14 | Medium 12, on-filament. `PLA` or `PETG-CF` |
| Vendor | 6 | 62 | 57 | 14 | Regular 12, on-filament, opa 170. `Bambu` / `Generic` |
| Percent | 6 | 76 | 57 | 18 | SemiBold 16, on-filament. `82%` / `45%` |
| Bar track | 6 | 92 | 57 | 3 | radius 1, on-filament at opa 70 |
| Bar fill | 6 | 92 | see below | 3 | radius 1, on-filament |

The 20 px between the material line and the vendor line is empty on purpose. That gap is the color.

Bar fill width = round(57 × pct / 100). A1 → 47. A2 → 26. If pct is 0, draw no fill. If pct > 0, minimum fill width is 3 so a sliver remains.

If pct < 15, the percent label and the bar fill use `#F5A524` instead of the on-filament color. The track stays as specified.

Loaded (A1 on this screen): border color `#3DDC97`, border opa 255. No check icon, no “loaded” word, no dot. The border is the only loaded signal on the tile. The toolhead row names it.

Not loaded, filled (A2): border width stays 2, border opa 0.

Empty (A3): widget fill `#161B22`, border opa 0. Slot id `A3` at the same (6, 2), SemiBold 20, `#5A6572`. The word `Empty`, Regular 12, `#5A6572`, horizontally centered, y = 46. No bar, no percent, no dashed stroke.

Unknown (A4): same fill and border as empty. Slot id `A4` at (6, 2), SemiBold 20, `#8D98A5`. A single `?` in SemiBold 28, `#8D98A5`, horizontally centered, y = 32, h = 32. The word `Unknown`, Regular 12, `#5A6572`, horizontally centered, y = 68. No box around the question mark.

Error: border color `#F2555A`, border opa 255, width still 2. The vendor line is replaced by a single clipped fault in Regular 12 `#F2555A` (`Feed fail`, `Blocked`). Percent and bar stay.

Loading (this slot is the one in progress): border `#3DDC97`. A 16×16 arc at content (47, 4), stroke 2, track `#2A3340` drawn as the arc background, indicator `#3DDC97`, animated as in “Motion”. The vendor line is replaced by `Loading` in Regular 12 `#3DDC97`.

The whole tile is one button, including empty and unknown. Pressed, and only while pressed: if the tile is not loaded and not in error, set border opa 255 and border color `#8D98A5`. Loaded keeps mint. Error keeps `#F2555A`. Do not recolor the gradient.

AMS HT (one slot): draw one tile, same 73×104, at x = 123, y = 46. Do not stretch it across the row.

### Toolhead row (y 156–192, h 36)

No background object. One horizontal row, items vertically centered. Start x = 12.

This screen, A1 loaded into the toolhead:

`Toolhead` Regular 12 `#8D98A5`, gap 8, swatch 10×10 radius 2 fill `#D23B3B` (use a 1 px `#5A6572` border on the swatch only when the spool is the charcoal gradient), gap 6, `A1` Medium 14 `#F2F5F8`, gap 4, `PLA` Medium 14 `#F2F5F8`.

No chevron. The row is not a button. The tile is the button.

Other legal strings, same row, one at a time. Priority if several are true: paused, then another unit loading, then loaded.

| Mode | Content | Color |
|---|---|---|
| Other unit in the toolhead | `Toolhead`, swatch, `B2`, `PETG`, `from unit B` | Prefix and “from unit B” are Regular 12 `#8D98A5`. Slot and material are Medium 14 `#F2F5F8`. |
| Other unit loading | `Unit B is loading B3` | Medium 14 `#F2F5F8`. No swatch. |
| Paused by another unit | `Paused by unit B` | Medium 14 `#F5A524`. No swatch. |
| This unit loading, and home is still up | `Loading A3`, gap 8, 16×16 arc | Medium 14 `#F2F5F8`. Arc as in the loading tile. |

### Nav (y 192–240, h 48)

Fill `#0B0E12`. Top border only, 1 px `#1E252E`. Three tabs:

| Tab | x | w | Icon |
|---|---|---|---|
| Env | 0 | 107 | `water-percent` |
| Alerts | 107 | 106 | `bell-outline` |
| Menu | 213 | 107 | `menu` |

Icon 18 at local y = 7, horizontally centered. Label Regular 12 at local y = 28, horizontally centered. Inactive icon and label `#8D98A5`.

Active tab (Environment, Alerts, or Menu — never home): icon and label `#3DDC97`. Marker: w = 16, h = 2, radius 1, fill `#3DDC97`, y = 192, x = tab_x + (tab_w − 16) / 2. Env → x = 45. Alerts → x = 152. Menu → x = 258.

## Environment — drying

No nav. The bottom band is the action. Back returns to home.

```
 y
  0  top bar            h 36
 36  gap                h  4
 40  readout            h 32
 72  gap                h  4
 76  status             h 36
112  gap                h  8
120  chips              h 36
156  gap                h  6
162  steppers           h 36
198  gap                h  6
204  Stop band          h 36
240
```

### Top bar (y 0–36)

| Item | x | y | w | h |
|---|---|---|---|---|
| Back | 0 | 0 | 36 | 36 |
| Title `Environment` | centered on x = 160 | 8 | text | 20 |
| Bell | 284 | 0 | 36 | 36 |

Back icon `chevron-left` 18, `#F2F5F8`. Title SemiBold 16 `#F2F5F8`. Bell same as home.

### Readout (y 40–72, h 32)

Two groups, vertically centered in the 32 px row. Numerals SemiBold 28 `#F2F5F8`. Units bottom-aligned to the numeral, then moved up 2 px.

Left group, right edge at x = 148: `31` then `%` Medium 14 `#8D98A5` (gap 1) then `RH` Regular 12 `#8D98A5` (gap 4).

Right group, left edge at x = 172: `24` then `°C` Medium 14 `#8D98A5` (gap 1).

24 px separates the groups. The screen center is the gap, not a rule.

### Status (y 76–112, h 36)

Ring at (16, 78), 32×32. Stroke 3. Track is the full circle `#2A3340`. Indicator `#F5A524`, round caps.

LVGL arc: 0° is 3 o’clock, clockwise. Set rotation 270 so the indicator starts at 12 o’clock. Background angles 0–360. Indicator start 0. Indicator end = sweep.

```
sweep = max(8, (elapsed_s * 360 + duration_s/2) / duration_s)
```

This frame: duration 12 h = 43200 s, remaining 11 h 42 m = 42120 s, elapsed 1080 s, sweep = 9°. The 8° floor exists so a cycle that just started is still visible. The ring does not spin.

Text at x = 56:

| Line | y | Style |
|---|---|---|
| `Heating` | 78 | SemiBold 16 `#F5A524` |
| `11 h 42 m left` | 96 | Medium 14 `#F2F5F8` |

Idle, same region, ring hidden: line 1 `Idle` SemiBold 16 `#8D98A5`. Line 2 `45–65°C · 1–24 h` Regular 12 `#5A6572`. If Start is blocked because no adapter is present: line 2 is `No adapter` in Regular 12 `#F5A524`, and the bottom band uses the disabled treatment below. No Adapter button on this screen.

### Chips (y 120–156, h 36)

Five chips, w = 56, gap = 6, radius 10, border width 1, label Medium 14 centered.

x = 8, 70, 132, 194, 256. Labels: `PLA`, `PETG`, `ABS`, `PA`, `TPU`.

| State | Fill | Text | Border |
|---|---|---|---|
| Unselected | `#161B22` | `#F2F5F8` | `#161B22` |
| Selected | `#1E252E` | `#3DDC97` | `#3DDC97` |
| Pressed, was unselected | `#1E252E` | `#F2F5F8` | `#161B22` |

This frame: PLA is selected. It is the preset that produced 55°C and 12 h. Do not paint the selected chip solid mint.

### Steppers (y 162–198, h 36)

Two groups, radius 10, fill `#1E252E`, no outer border.

| Group | x | w | Value |
|---|---|---|---|
| Temperature | 8 | 148 | `55°C` |
| Time | 164 | 148 | `12 h` |

8+148+8+148+8 = 320.

Inside a group: minus segment w = 36, value area w = 76, plus segment w = 36. Icon `minus` or `plus`, 18 px, `#F2F5F8`, centered in its 36×36 segment. Value SemiBold 16 `#F2F5F8`, centered in the middle 76 px. A 1×20 rule `#2A3340` at x = 36 and at x = 111, y = 8, so the rules stay off the rounded corners.

Pressed segment: that 36 px slice fills `#2A3340`. The value is not a button.

Step size is a firmware concern. The control only has to show the current target. Temperature is in whole degrees. Time is in whole hours.

### Bottom band (y 204–240, h 36)

Full width. This frame is Stop:

- Fill `#1E252E`
- Top border 1 px `#2A3340`
- Label `Stop`, SemiBold 16, `#F2555A`, centered. Sentence case.

Idle, and drying is allowed:

- Fill `#3DDC97`
- No top border
- Label `Start`, SemiBold 16, `#0B0E12`, centered

Idle, blocked:

- Fill `#1E252E`, top border 1 px `#2A3340`
- Label `Start`, SemiBold 16, `#5A6572`
- Not clickable

## Loading

Takes over the screen. No nav, no back. Stop is the exit.

```
 y
  0  header            h 56
 56  gap               h  4
 60  five steps        h 140   (5 × 28)
200  gap               h  4
204  Stop band         h 36
240
```

Header:

| Item | x | y | Style |
|---|---|---|---|
| `Loading A3` | 16 | 6 | SemiBold 20 `#F2F5F8`, h = 24 |
| Swatch | 16 | 36 | 10×10, radius 5, fill `#D23B3B` |
| `PLA` | 32 | 34 | Medium 14 `#F2F5F8` |
| `·` | after PLA + 4 | 34 | Medium 14 `#5A6572` |
| `Red` | after the dot + 4 | 34 | Medium 14 `#8D98A5` |

Steps. Row i (0-based) has y = 60 + i×28, h = 28.

| i | Label | State |
|---|---|---|
| 0 | Heat the nozzle | done |
| 1 | Cut filament | done |
| 2 | Pull back current filament | active |
| 3 | Push new filament | pending |
| 4 | Purge | pending |

Circle 16×16 at (16, row_y+6). Label Medium 14 at x = 40, vertically centered in the row.

Rail: x = 23, w = 2, from y = 74 to y = 186 (center of the first circle to center of the last). Color `#2A3340`. A second 2 px segment in `#3DDC97` lies on top of the rail from y = 74 to the center of the active circle. This frame the active center is y = 130.

| State | Circle | Label |
|---|---|---|
| Done | Fill `#3DDC97`. Icon `check` 18 scaled into 10 px, color `#0B0E12`. No border. | `#8D98A5` |
| Active | No fill. Track: full circle, stroke 2, `#2A3340`. Indicator arc stroke 2 `#3DDC97`, animated. No glow, no arrow. | `#F2F5F8` |
| Pending | No fill. Stroke 1 `#5A6572`. No icon. | `#5A6572` |

Stop band: identical to the environment Stop band, y = 204.

## Slot sheet

The home screen stays live underneath. Do not snapshot it.

Scrim: (0, 0, 320, 240), fill `#000000`, opacity 179. A click anywhere on the scrim that is not the sheet closes it. The visible scrim band above the sheet is y 0–48, which is 48 px tall.

Sheet: (0, 48, 320, 192), fill `#1E252E`, radius 16. It covers the nav. The nav is not tappable while the sheet is open.

| Item | Sheet-local x, y | Size | Style |
|---|---|---|---|
| Handle | 144, 8 | 32×4 | radius 2, `#5A6572` |
| Swatch | 16, 26 | 10×10 | radius 2, fill `#3A3E45`, border 1 `#5A6572` |
| `A2` | 32, 22 | h 20 | SemiBold 16 `#F2F5F8` |
| `·` | after A2 + 6 | h 20 | SemiBold 16 `#5A6572` |
| `PETG-CF` | after the dot + 6 | h 20 | SemiBold 16 `#F2F5F8` |
| `45%` | right inset 16, y 24 | h 18 | Medium 14 `#8D98A5`, right aligned |

Rows. Icons 18 px, left x = 16, label x = 44. Enabled rows get `chevron-right` 18 `#5A6572` at x = 286, vertically centered on the row.

| Row | Local y | h | Icon | Label | Extra |
|---|---|---|---|---|---|
| Load | 52 | 40 | `tray-arrow-down` `#3DDC97` | `Load` Medium 14 `#F2F5F8` | Chevron. No filled square behind the icon. |
| Edit | 92 | 40 | `pencil-outline` `#8D98A5` | `Edit` Medium 14 `#F2F5F8` | Chevron. |
| Re-read | 132 | 56 | `refresh` `#5A6572`, aligned to the label (row y+10), not to the row center | `Re-read` Medium 14 `#5A6572` at row y+8 | Reason `No tag in this slot`, Regular 12 `#8D98A5`, x = 44, row y+28. No chevron. Not clickable. |

Dividers: h = 1, color `#2A3340`, x = 44 to 304, at local y = 91 and y = 131.

Enabled row, pressed: fill `#161B22` for that row only.

Which row is disabled is data, not a fixed choice. The disabled treatment is always: label `#5A6572`, icon `#5A6572`, no chevron, not clickable, and one Regular 12 `#8D98A5` reason under the label. The row grows from 40 to 56 to hold the reason. If the reason is absent, the row stays 40 and is enabled. This frame’s reason is `No tag in this slot` on Re-read, because A2’s vendor is Generic and the slot has no tag.

## Motion

Only these. Everything else changes in place, with no tween.

| What | Property | From → to | Duration | Curve | Notes |
|---|---|---|---|---|---|
| Scrim, opening | opacity | 0 → 179 | 120 ms | ease-out | One full-screen rect. Not a framebuffer fade. |
| Sheet, opening | y | 60 → 48 | 140 ms | ease-out | 12 px. Runs together with the scrim. |
| Scrim and sheet, closing | reverse of the above | | 100 ms | ease-in | Delete both at the end. |
| Active step arc, or the one loading arc on home | indicator end angle | 0 → 360 | 1100 ms | linear, infinite | A single 16×16 arc. Never two at once. |
| Drying ring | indicator end angle | previous sweep → new sweep | 200 ms | ease-out | Only when the minute value changes. No continuous spin. |
| Tab marker | x | old center → new center | 120 ms | ease-out | The 16×2 rect. |

Screen-to-screen changes, including home → environment and home → loading, are an instant cut.

## Copy on these four screens

Home: `AMS 2 Pro`, `(A)`, `31%`, `RH`, `24°C`, `A1`, `PLA`, `Bambu`, `82%`, `A2`, `PETG-CF`, `Generic`, `45%`, `A3`, `Empty`, `A4`, `?`, `Unknown`, `Toolhead`, `Env`, `Alerts`, `Menu`.

Environment: `Environment`, `31`, `%`, `RH`, `24`, `°C`, `Heating`, `11 h 42 m left`, `PLA`, `PETG`, `ABS`, `PA`, `TPU`, `55°C`, `12 h`, `Stop`.

Loading: `Loading A3`, `PLA`, `·`, `Red`, `Heat the nozzle`, `Cut filament`, `Pull back current filament`, `Push new filament`, `Purge`, `Stop`.

Sheet: `A2`, `·`, `PETG-CF`, `45%`, `Load`, `Edit`, `Re-read`, `No tag in this slot`.

Degree sign is `°` with no space before `C` (`24°C`, `55°C`). Hours in a stepper are `12 h` with a space. The drying countdown is `11 h 42 m left`.

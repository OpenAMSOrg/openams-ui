# AMS clip screen — 800×480

Build target: 800×480 landscape, LVGL 9, 16-bit colour, capacitive touch, ESP32-S3 with PSRAM (the ES3C50P and ES3C43P boards, `-DMMU_BOARD=es3c50p` or `es3c43p`). The simulator builds the same UI with `-DMMU_UI_RESOLUTION=800x480`.

There is no second design. Every screen is still laid out on the 480×320 grid of `VISUAL_SPEC_480.md`, and this file only says how that grid is drawn on the larger panel. If this file and `VISUAL_SPEC_480.md` disagree about a screen, `VISUAL_SPEC_480.md` wins.

## The rule

The 800×480 build draws the grid at **1.5×**.

- Every length on the grid goes through `UI_PX()` (`src/ui/ui_scale.h`): positions, sizes, paddings, gaps, radii, border and line widths, offsets and drag distances. At 480×320 `UI_PX()` is the identity, so that build *is* the grid.
- `UI_PX()` rounds half away from zero: 1 → 2, 13 → 20, 15 → 23.
- The design tokens (`ui_tokens.h`, from `design/tokens.json`) and the theme constants (`UI_PAD_*`, `UI_RADIUS*`, `UI_TOPBAR_H`, `UI_TOUCH_MIN`, …) are scaled where they are defined, so code that uses them needs nothing more.
- Counts, colours, opacities, percentages, durations and angles are not lengths and do not scale.

320 × 1.5 is exactly the panel's 480, so heights need nothing else. 480 × 1.5 is 720, which leaves **80 px of width**. A screen spends it by sizing from `UI_SCREEN_W` instead of from a fixed length. Every such expression is written so that it gives the grid's own number at 480×320 (for example `UI_SCREEN_W - 2 * UI_PX(12)` is 456 there).

## Type

The fonts are regenerated at 1.5× under the same symbols (`tools/gen_fonts.py --set 800x480`, into `src/ui/fonts/800x480/`). A face is named for its size on the grid, so `ui_font_geist_13` is a 20 px face in this build. The build picks one set (`cmake/resolution.cmake`); the headers are shared.

| Grid | 800×480 |
|---|---|
| Geist 12 / 13 / 15 / 20 | 18 / 20 / 23 / 30 |
| IBM Plex Mono 18 | 27 |
| Inter 12 / 14 / 16 / 20 / 28 | 18 / 21 / 24 / 30 / 42 |
| Icons 18 / 24, severity 18, chevrons 36 | 27 / 36, 27, 54 |
| CJK 12 / 13 / 15 / 20 (2 bpp) | 18 / 20 / 23 / 30 |

## Where the extra width goes

| Screen | At 800×480 |
|---|---|
| Home | The four tiles share the width between the margins: 182 px instead of 162. Every full-width band (header, message row, filament strip) runs to 800. |
| Menu sheet | The tile grid fills the sheet, with even margins. |
| Edit | The two field columns stretch to 376 px each, with the same gutter as the Reset and Save buttons. |
| Environment | The two steppers stretch like Edit's columns; the five preset chips fill the band. |
| Sheets | Full width, as on the grid. The title stops short of the header's right-hand items, whatever the width. |
| Group picker | The divider, stepper and buttons are full width. |
| Print | The material line runs from the swatch to 16 px short of the ring. The button is anchored to the bottom edge. |
| Load, Alerts, Groups, Settings, Mapping, This unit | Rows and cards are full width; their contents keep grid sizes. |
| Tag writer | Its pages are the grid at 1.5× (720×480), centred, in LVGL's Montserrat at 22 / 24 / 30 / 42 for its 14 / 16 / 20 / 28. |

## Rounding

Two lengths scaled one at a time can differ by a pixel from the same lengths scaled as a sum (`UI_PX(a) + UI_PX(b)` vs `UI_PX(a + b)`). Where two edges must meet, the code scales the parts and derives the rest. For example, the toggle knob's "on" position is the track width minus the knob and its inset, so the knob stays centred, and the outline round a toolhead bar is `UI_PX(1)` per side. Hairlines (`UI_PX(1)`) are 2 px.

The hatch (the disabled look) is a hand-drawn A8 tile: period 8, stripe 2 on the grid, and period 12, stripe 3 here (`ui_home.c`).

## Checking a change

- **480×320 must not move.** Render every screen and modal before and after (`openams-ui-sim --screenshots DIR`, 702 images) and compare the files byte for byte.
- **800×480 must audit clean.** Build the simulator with `-DMMU_UI_RESOLUTION=800x480` and run `--audit` (`AUDIT TOTAL issues=0`) and `--screenshots`. The audit's tolerances are on the grid too.
- `ctest` runs at either resolution. `test_screen_update` also checks that a screen updated in place matches a fresh build at that resolution.

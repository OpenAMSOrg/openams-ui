/**
 * @file ui_scale.h
 * The UI's resolution (cmake/resolution.cmake, MMU_UI_RESOLUTION).
 *
 * Every screen is laid out on a 480x320 grid (docs/VISUAL_SPEC_480.md). The
 * 800x480 build draws that grid at 1.5x: every length on the grid goes
 * through UI_PX(), and the fonts are generated at 1.5x under the same names
 * (tools/gen_fonts.py --set 800x480). 320 x 1.5 is exactly 480; 480 x 1.5 is
 * 720, and a screen spends the other 80 px of width by sizing from
 * UI_SCREEN_W rather than from a fixed length (docs/VISUAL_SPEC_800.md).
 *
 * At 480x320 UI_PX() is the identity, so that build is the grid itself.
 */
#ifndef UI_SCALE_H
#define UI_SCALE_H

#if defined(MMU_UI_800X480)
#define UI_SCREEN_W 800
#define UI_SCREEN_H 480
/* x1.5, half away from zero: 1 -> 2, 13 -> 20, -3 -> -5. A constant
 * expression, so it works in static initializers and #define arithmetic. */
#define UI_PX(v) ((v) >= 0 ? ((v) * 3 + 1) / 2 : -((-(v) * 3 + 1) / 2))
#else
#define UI_SCREEN_W 480
#define UI_SCREEN_H 320
#define UI_PX(v) (v)
#endif

#endif /* UI_SCALE_H */

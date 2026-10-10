/**
 * @file ui_bars.h
 * The chrome the screens that predate the restyle share: the one-line header
 * (ui_kit_header). The bottom tab bar is gone from every screen; the menu on
 * home and the back arrow on the others are the way around (docs/design/
 * UNIFIED_UI.md 4d).
 */

#ifndef UI_BARS_H
#define UI_BARS_H

#include "lvgl.h"

#include <stdbool.h>

/** The sub-screen header: the one-line header of ui_kit_header() - back arrow,
 * title, alert slot. (@p with_back is kept for callers; every sub-screen has
 * the arrow.) */
lv_obj_t *ui_bar_top(lv_obj_t *parent, const char *title, bool with_back);

#endif /* UI_BARS_H */

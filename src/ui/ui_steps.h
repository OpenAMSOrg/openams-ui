/**
 * @file ui_steps.h
 * The loading step list: a vertical rail with one row per step, shared by the
 * load / unload flows. See docs/VISUAL_SPEC_480.md, "Loading".
 */

#ifndef UI_STEPS_H
#define UI_STEPS_H

#include "lvgl.h"

/** How far along one step of a load / unload sequence is. */
typedef enum {
    UI_STEP_DONE,
    UI_STEP_ACTIVE,
    UI_STEP_PENDING,
    UI_STEP_FAILED,
} ui_step_state_t;

/* One 40 px step row at (0, y), full width: circle 16x16 at (16, y+12), label
 * UI_FONT_M at x 44 vertically centered, width UI_SCREEN_W - 60, long mode
 * DOTS. Returns the row. */
lv_obj_t *ui_step_row(lv_obj_t *parent, int y, const char *label, ui_step_state_t state);

/* The rail: a 2 px UI_COL_LINE line at x 23 from y_first_center to
 * y_last_center, and on top of it a 2 px UI_COL_ACCENT segment from
 * y_first_center to y_progress_center (none when y_progress_center <=
 * y_first_center). Call it BEFORE creating the rows so the circles are drawn
 * over it. Use plain 2 px wide objects, not lv_line. */
void ui_step_rail(lv_obj_t *parent, int y_first_center, int y_last_center,
                  int y_progress_center);

#endif /* UI_STEPS_H */

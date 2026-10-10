/**
 * @file ui_band.h
 * The bottom action band: the 36 px strip at the foot of a screen that carries
 * its main action (docs/VISUAL_SPEC_480.md, "Shared widgets" -> "Action band").
 * A screen has either this band or the 48 px bottom nav, never both.
 */

#ifndef UI_BAND_H
#define UI_BAND_H

#include "lvgl.h"

/** How a band reads. ACCENT is the one primary action, DANGER the destructive
 * one, NEUTRAL anything else. */
typedef enum {
    UI_ACTION_ACCENT,
    UI_ACTION_DANGER,
    UI_ACTION_NEUTRAL,
} ui_action_kind_t;

/** Full-width 36 px band at y = UI_SCREEN_H - UI_ACTIONBAR_H of @p parent (pass
 * the screen). The whole band is one button; @p cb may be NULL. Returns the
 * band. */
lv_obj_t *ui_action_bar(lv_obj_t *parent, const char *label, ui_action_kind_t kind,
                        lv_event_cb_t cb, void *user);

/** Two halves of 160 px, each its own button, a 1x20 UI_COL_LINE rule centered
 * between them. Any callback may be NULL. Returns the band. */
lv_obj_t *ui_action_bar2(lv_obj_t *parent, const char *left, ui_action_kind_t left_kind,
                         lv_event_cb_t left_cb, void *left_user, const char *right,
                         ui_action_kind_t right_kind, lv_event_cb_t right_cb, void *right_user);

/** Disabled: a dimmed read of the button's own kind, not clickable - muted
 * mint fill/text for ACCENT, UI_COL_PANEL fill with UI_COL_TEXT_FAINT for
 * NEUTRAL, UI_COL_PANEL fill with a muted red text for DANGER. For a two-half
 * band, disables both halves. */
void ui_action_bar_set_enabled(lv_obj_t *bar, bool enabled);

/** Enable or disable one half of a two-half band built by ui_action_bar2():
 * @p half is 0 for the left button, 1 for the right. A disabled half uses the
 * same look as ui_action_bar_set_enabled(). */
void ui_action_bar2_set_half_enabled(lv_obj_t *bar, int half, bool enabled);

#endif /* UI_BAND_H */

/**
 * @file ui_update.h
 * The setters the screens' update() paths share (ui_screens.h's `update` op).
 *
 * Every one of them compares before it writes: a label's text or a ring's
 * value is set only when the object does not already hold it, so LVGL
 * invalidates the boxes that really changed and leaves the rest of the screen
 * alone. A screen that writes unconditionally would redraw as much as the
 * rebuild it replaced, which is the whole cost this work removes.
 *
 * Each setter writes exactly what the matching builder wrote, so an object
 * updated here is pixel-identical to one a fresh build() drew - the contract
 * ui_screens.h's update op makes.
 */

#ifndef UI_UPDATE_H
#define UI_UPDATE_H

#include "lvgl.h"

#include <stdbool.h>
#include <stdint.h>

/** @p label's text, when it differs from what the label shows now. */
void ui_update_text(lv_obj_t *label, const char *text);

/** @p label's text, cut to @p max_w px in @p font exactly as
 * ui_home_label_fit() cuts it (same algorithm, so the same result), set only
 * when the label shows something else. */
void ui_update_text_fit(lv_obj_t *label, const char *text, const lv_font_t *font, int max_w);

/** @p label's text color, when it differs from the one it has now. */
void ui_update_text_color(lv_obj_t *label, lv_color_t color);

/** A box's background color, when it differs from the one it has now. */
void ui_update_bg(lv_obj_t *box, lv_color_t color);

/** A box's border, in @p color at @p opa, each set only when it differs. */
void ui_update_border(lv_obj_t *box, lv_color_t color, lv_opa_t opa);

/** A ring's sweep and indicator color: @p percent of 100 (0 or less for none,
 * as ui_kit_ring() and ui_tile.c's own ring do), in @p color. */
void ui_update_arc(lv_obj_t *arc, int percent, lv_color_t color);

/** An alert slot's severity icon and count (ui_home_alert_mark()'s two
 * labels), for a slot that already holds a mark. */
void ui_update_alert_mark(lv_obj_t *slot, int tone, int count);

/** A filament swatch of @p rgb, or the empty one, exactly as ui_kit_swatch()
 * draws it - including the hairline, so the two directions both land on what a
 * fresh build would. */
void ui_update_swatch(lv_obj_t *swatch, uint32_t rgb, bool empty);

/** @p hdr's (ui_kit_header()'s) title, cut to the room the header left it, the
 * same cut the header made itself - so a headline that arrives mid-run can be
 * put up without the header being rebuilt. */
void ui_update_header_title(lv_obj_t *hdr, const char *text);

/** This display's own alert totals - the bound unit's and its toolhead's, in
 * the tone of the worse - the numbers ui_kit_header()'s alert slot is drawn
 * from, so a screen can tell whether that slot still belongs on screen. */
void ui_update_alert_totals(int *count, int *tone, bool *clear);

/** How many objects an alert slot holds: 2 for a mark, 1 for the resting
 * check, 0 when its alert is shown elsewhere. */
int ui_update_alert_slot_objects(int count, bool clear);

#endif /* UI_UPDATE_H */
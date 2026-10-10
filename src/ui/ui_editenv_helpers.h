/**
 * @file ui_editenv_helpers.h
 * Small layout pieces shared by the slot edit dialog and the environment page:
 * a panel, labels, a button and the [-][value][+] stepper.
 *
 * LV_USE_SWITCH, LV_USE_DROPDOWN, LV_USE_ROLLER and LV_USE_GRID are all 0 in
 * lv_conf.h, so the "roller/dropdown" and the rotate toggle are hand-built
 * from buttons and panels instead.
 */

#ifndef UI_EDITENV_HELPERS_H
#define UI_EDITENV_HELPERS_H

#include <stdbool.h>
#include <stddef.h>

#include "lvgl.h"
#include "ui/ui_theme.h"

/** Bare styled object at (x,y,w,h). */
lv_obj_t *ui_editenv_panel(lv_obj_t *parent, ui_style_id_t style,
                           int x, int y, int w, int h);

/** Label anchored to the parent's top-left. */
lv_obj_t *ui_editenv_label(lv_obj_t *parent, const char *text,
                           ui_style_id_t style, int x, int y);

/** Label vertically centered in the parent's left edge at @p x. */
lv_obj_t *ui_editenv_label_mid(lv_obj_t *parent, const char *text,
                               ui_style_id_t style, int x, int dy);

/** Button with a centered label; @p cb may be NULL for a dead button. */
lv_obj_t *ui_editenv_button(lv_obj_t *parent, const char *text, ui_style_id_t style,
                            int x, int y, int w, int h,
                            lv_event_cb_t cb, void *user);

/** [-][value][+] filling (x,y,w,h); returns the value label. When @p enabled is
 * false the ends are grayed and do not respond. */
lv_obj_t *ui_editenv_stepper(lv_obj_t *parent, int x, int y, int w, int h,
                             bool enabled, lv_event_cb_t cb,
                             void *minus_user, void *plus_user);

/** "#RRGGBB" for a 0xRRGGBB color. */
void ui_editenv_color_text(uint32_t rgb, char *out, size_t out_len);

#endif /* UI_EDITENV_HELPERS_H */

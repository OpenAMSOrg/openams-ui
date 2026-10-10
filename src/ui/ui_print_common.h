/**
 * @file ui_print_common.h
 * Small drawing helpers shared by the three screens owned by the "print"
 * worker: in-print status (7), auto-refill groups (8) and print-start mapping
 * (10). Everything is explicit-coordinate so it fits the 320x240 panel.
 */
#ifndef UI_PRINT_COMMON_H
#define UI_PRINT_COMMON_H

#include "lvgl.h"
#include "ui/ui_theme.h"

#include <stdint.h>
#include <stdbool.h>

/** Short human color name for a 0xRRGGBB value ("red", "white", ...). */
const char *print_color_name(uint32_t rgb);

/** The palette print_color_name() names exactly, so a picker built from it
 * can never drift out of sync with the names it later shows. */
int print_palette_count(void);
uint32_t print_palette_color(int i);

/** "Left" / "Right" for a unit on a dual-extruder setup, else "". */
const char *print_side_text(int unit_idx);

/** Label at (x,y). @p w > 0 wraps, otherwise the text is clipped. */
lv_obj_t *print_label(lv_obj_t *parent, const char *text, lv_style_t *style,
                      int x, int y, int w);

/** Plain panel with the given theme style. */
lv_obj_t *print_panel(lv_obj_t *parent, ui_style_id_t style,
                      int x, int y, int w, int h);

/** Button with a centered label. */
lv_obj_t *print_button(lv_obj_t *parent, const char *text, ui_style_id_t style,
                       int x, int y, int w, int h,
                       lv_event_cb_t cb, void *user);

/** Rounded color swatch; @p unknown draws a "?" instead of the color. */
lv_obj_t *print_swatch(lv_obj_t *parent, uint32_t rgb, bool unknown,
                       int x, int y, int w, int h);

/** Circle with a thick border in @p rgb and @p center written inside. */
lv_obj_t *print_ring(lv_obj_t *parent, uint32_t rgb, const char *center,
                     int x, int y, int size);

/** Compact slot chip: color stripe on the left, "A1" and remaining %.
 * @p cb receives the packed (unit, slot); pass NULL for a read-only chip. */
lv_obj_t *print_slot_chip(lv_obj_t *parent, int unit, int slot,
                          int x, int y, int w, int h,
                          lv_event_cb_t cb, void *user);

#endif /* UI_PRINT_COMMON_H */

/**
 * @file ui_input.h
 * The input family: a numeric stepper (with an optional caption), a selectable
 * chip, and a capsule switch.
 *
 * Geometry follows the Environment screen's "Chips" and "Steppers" sections in
 * docs/VISUAL_SPEC_480.md: a stepper and a chip are 36 px tall, a toggle is a
 * 40x24 capsule. All three take their own x/y (except the chip, whose width
 * comes from its label unless the caller fixes it with ui_chip_fixed()) so a
 * caller can place them on any grid.
 */

#ifndef UI_INPUT_H
#define UI_INPUT_H

#include "lvgl.h"

/** 36 px tall, radius 10, fill UI_COL_PANEL_HI. Minus segment (36 px) | value
 * | plus segment (36 px). Returns the VALUE label so the caller can
 * lv_label_set_text() it. @p on_minus / @p on_plus fire on click. */
lv_obj_t *ui_stepper(lv_obj_t *parent, int x, int y, int w, const char *value,
                     lv_event_cb_t on_minus, lv_event_cb_t on_plus, void *user);

/** The same stepper with a UI_FONT_S UI_COL_TEXT_DIM caption at (x, y); the
 * stepper sits at y + 16 (52 px total). Returns the value label. */
lv_obj_t *ui_stepper_captioned(lv_obj_t *parent, int x, int y, int w, const char *caption,
                               const char *value, lv_event_cb_t on_minus, lv_event_cb_t on_plus,
                               void *user);

/** 36 px tall, radius 10, 1 px border always; width = max(56, text width + 24)
 * with the label in UI_FONT_M, centered. @p cb fires on click. */
lv_obj_t *ui_chip(lv_obj_t *parent, const char *text, bool selected, lv_event_cb_t cb,
                  void *user);

/** Like ui_chip(), but @p w is the chip's exact width instead of a
 * content-driven one - the spec fixes some chip rows to one width regardless
 * of label (the Environment screen's five 84 px material chips). */
lv_obj_t *ui_chip_fixed(lv_obj_t *parent, const char *text, int w, bool selected,
                        lv_event_cb_t cb, void *user);

/** Paint @p chip as selected / unselected (fill, text, border). */
void ui_chip_set_selected(lv_obj_t *chip, bool selected);

/** A 40x24 capsule switch: clicking flips it and then calls @p cb, which reads
 * the new state with ui_toggle_get(). */
lv_obj_t *ui_toggle(lv_obj_t *parent, bool on, lv_event_cb_t cb, void *user);

/** Set the switch state programmatically (no animation). */
void ui_toggle_set(lv_obj_t *toggle, bool on);

/** The switch state: true when on. */
bool ui_toggle_get(lv_obj_t *toggle);

#endif /* UI_INPUT_H */

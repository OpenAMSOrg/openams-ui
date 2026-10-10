/**
 * @file ui_display.h
 * Display pieces: the hero readout number with its unit, the progress ring,
 * status pills, filament swatches, list-section captions and the empty-state
 * block.
 *
 * The pixel spec behind each piece is in docs/VISUAL_SPEC_480.md ("Readout",
 * "Status", "On-filament color", "Palette").
 */

#ifndef UI_DISPLAY_H
#define UI_DISPLAY_H

#include "lvgl.h"

#include "ui/ui_theme.h"

#include <stdbool.h>
#include <stdint.h>

/** Big number + small unit: number UI_FONT_HERO UI_COL_TEXT, unit UI_FONT_M
 * UI_COL_TEXT_DIM, 1 px gap, unit bottom-aligned with the number then moved up
 * 2 px. A transparent content-sized container at (x, y). */
lv_obj_t *ui_hero_value(lv_obj_t *parent, int x, int y, const char *number, const char *unit);

/** Replace the number and unit of a hero readout built by ui_hero_value(). */
void ui_hero_value_set(lv_obj_t *hero, const char *number, const char *unit);

/** Progress ring: an arc size x size, stroke 3, full-circle UI_COL_LINE track,
 * a `color` indicator with round caps, starting at 12 o'clock. @p pct is
 * 0..100; anything below 8% still draws an 8 degree sliver. */
lv_obj_t *ui_ring(lv_obj_t *parent, int x, int y, int size, int pct, lv_color_t color);

/** Update a ring's sweep; animated by the caller if it wants motion. */
void ui_ring_set(lv_obj_t *ring, int pct);

/** Content-sized 20 px capsule, UI_FONT_S_MED, padding 0/8, filled by one of
 * UI_STYLE_PILL / _PILL_ACCENT / _PILL_WARN / _PILL_ERR / _PILL_INFO. */
lv_obj_t *ui_pill(lv_obj_t *parent, const char *text, ui_style_id_t pill_style);

/** Filament swatch size x size: radius 2 (or size/2 when @p round), filled with
 * the vertical filament gradient, plus a 1 px UI_COL_TEXT_FAINT border when the
 * filament is dark (the charcoal case). */
lv_obj_t *ui_swatch(lv_obj_t *parent, uint32_t rgb, int x, int y, int size, bool round);

/** List-section caption: UI_FONT_S, UI_COL_TEXT_FAINT, full width, 4 px left
 * and 6 px top inset. */
lv_obj_t *ui_section_label(lv_obj_t *parent, const char *text);

/** Centered empty state: a 24 px icon in UI_COL_TEXT_FAINT, an 8 px gap, then
 * one UI_FONT_S UI_COL_TEXT_DIM line wrapped to (parent width - 32). */
lv_obj_t *ui_empty_state(lv_obj_t *parent, const char *icon, const char *text);

#endif /* UI_DISPLAY_H */

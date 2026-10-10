/**
 * @file ui_list.h
 * The list family: a scrolling column of rows, the shape the slot sheet uses.
 */

#ifndef UI_LIST_H
#define UI_LIST_H

#include "lvgl.h"

/** A vertically scrolling flex column at (x, y, w, h): transparent, pad 0,
 * row gap 6, no scrollbar, scroll dir vertical. */
lv_obj_t *ui_list_column(lv_obj_t *parent, int x, int y, int w, int h);

/** A list row for a flex column: width LV_PCT(100), height LV_SIZE_CONTENT
 * with min height 44, fill UI_COL_PANEL, radius 10, padding 12/12/10/10
 * (left/right/top/bottom), pressed fill UI_COL_PANEL_HI when @p cb is set (a
 * row is clickable only then). The children are a flex row, cross-axis
 * centered, column gap 10: an optional @p icon in UI_FONT_ICON / UI_COL_TEXT_DIM
 * (width 18), a text column that takes the space left (title in UI_FONT_M /
 * UI_COL_TEXT, elided with dots; optional @p subtitle in UI_FONT_S /
 * UI_COL_TEXT_DIM, wrapped) and the trailing slot. */
lv_obj_t *ui_list_row(lv_obj_t *parent, const char *icon, const char *title, const char *subtitle,
                      const char *trailing, lv_event_cb_t cb, void *user);

/** The trailing slot, the row's last child, so callers can put a toggle or a
 * pill there (they create it as a child of this). */
lv_obj_t *ui_list_row_trailing(lv_obj_t *row);

#endif /* UI_LIST_H */

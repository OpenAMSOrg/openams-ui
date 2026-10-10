/**
 * @file ui_tile.h
 * The lane tile: one bay's whole anatomy on a fixed 108x150 box
 * (docs/design/UNIFIED_UI.md 7, LaneTile) - the tool label, the bay's sublabel,
 * the spare badge, the RFID mark, the name and its meta, and the 52 px ring at
 * the bottom center with the status tag overlaid on it.
 *
 * It draws a view_tile_t and nothing else: no model, no strings of its own
 * (UNIFIED_UI.md 2's rule).
 */

#ifndef UI_TILE_H
#define UI_TILE_H

#include "lvgl.h"
#include "view/view.h"

/** The tile for @p t at (x, y), @p w x @p h (the design size is 108x150; the
 * ring is anchored to the bottom, so the anatomy holds for any height). @p cb
 * gets @p user as its event user data, and may be NULL. */
lv_obj_t *ui_tile_bay(lv_obj_t *parent, const view_tile_t *t, int x, int y, int w, int h,
                      lv_event_cb_t cb, void *user);

/** The same tile for bay @p slot of unit @p unit, built by calling view_tile()
 * on the model first. This is the shape the component gallery calls; the home
 * screen builds the view itself and calls ui_tile_bay(). */
lv_obj_t *ui_tile_slot(lv_obj_t *parent, int unit, int slot, int x, int y, int w, int h,
                       lv_event_cb_t cb, void *user);

#endif /* UI_TILE_H */

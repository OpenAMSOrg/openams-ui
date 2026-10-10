/**
 * @file ui_toolhead_strip.h
 * The home screen's toolhead pieces (docs/design/UNIFIED_UI.md 4d): the message
 * row under the header, and the filament path under the tiles - a stem under
 * each tile, the loaded filament's line down to a small extruder icon with the
 * loaded tool overlaid.
 *
 * Everything is drawn from view_toolhead_t; nothing here composes text of its
 * own (UNIFIED_UI.md 2's rule).
 */

#ifndef UI_TOOLHEAD_STRIP_H
#define UI_TOOLHEAD_STRIP_H

#include "lvgl.h"
#include "view/view.h"

/** The message row, UI_HOME_MSG_H tall, under the header: the one message,
 * and the toolhead's own alert slot at its right end. Both keep their place
 * when empty; either callback may be NULL. */
lv_obj_t *ui_toolhead_message(lv_obj_t *parent, const view_toolhead_t *th,
                              lv_event_cb_t msg_cb, void *msg_user,
                              lv_event_cb_t alert_cb, void *alert_user);

/** The filament path band, UI_HOME_STRIP_H tall, at the foot of @p parent.
 * @p bays tiles are in the row above; @p loaded_col is the column of the
 * loaded one, or -1. @p pages is the unit's page count: with more than one the
 * paging chevrons take the strip's right end, and the path's words stop short
 * of them. The words (activity.rest's label, empty while a plan runs) are the
 * last child but the chevrons. */
lv_obj_t *ui_toolhead_strip(lv_obj_t *parent, const view_toolhead_t *th, int bays, int loaded_col,
                            int pages);

/** Where the path's words start, and how wide they may be on a unit of
 * @p pages pages: the one place both the builder and the in-place update
 * read, so the same text is cut at the same width. */
int ui_toolhead_rest_x(void);
int ui_toolhead_rest_w(int pages);

#endif /* UI_TOOLHEAD_STRIP_H */

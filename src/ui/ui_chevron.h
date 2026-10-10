/**
 * @file ui_chevron.h
 * The home screen's paging chevrons: the pair that moves the row of four
 * tiles a page left or right on a unit with more than four bays.
 *
 * They are the owner's answer to a wide MMU ("a chevron / arrow that allows
 * to scroll left and right so it supports more spools, everything else
 * remains the same"), and they are drawn only when the unit has more than one
 * page: a unit of four bays or fewer gets no chevrons at all, so its screen
 * is pixel for pixel what it was.
 *
 * The pair is built inside the toolhead strip's own box (ui_toolhead_strip()
 * returns it), not beside it, so a target can never cover a tile, the
 * message row or the header, and the strip's own drawing (the filament path,
 * the extruder icon) is what it is placed against: the geometry is in
 * ui_home.h, beside the rest of the screen's.
 */

#ifndef UI_CHEVRON_H
#define UI_CHEVRON_H

#include "lvgl.h"

/** The pair of chevrons for @p page of @p pages, inside @p strip (see
 *  UI_HOME_CHEVRON_X/Y for where). Nothing is built when @p pages is 1 or
 *  less. A chevron that cannot move further stays in place, dimmed, and is
 *  not a target (PRINCIPLES.md, ruling 1).
 *
 *  @p cb runs on a tap with -1 for the left chevron and +1 for the right one;
 *  the caller moves its page and refreshes. */
void ui_chevron_pair(lv_obj_t *strip, int page, int pages, lv_event_cb_t cb);

#endif /* UI_CHEVRON_H */
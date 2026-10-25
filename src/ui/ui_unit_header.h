/**
 * @file ui_unit_header.h
 * The home screen's top band (docs/design/UNIFIED_UI.md 4b, "The unit
 * header"): the unit's family name as the title, its config name under it, the
 * environment pill, and the unit alert's reserved slot at the right end.
 *
 * Every element keeps its place whether or not it has something to say, so the
 * band never changes height (PRINCIPLES.md 2).
 */

#ifndef UI_UNIT_HEADER_H
#define UI_UNIT_HEADER_H

#include "lvgl.h"
#include "view/view.h"

/** The header band, UI_HOME_HEADER_H tall, across the top of @p parent.
 * @p env_cb fires on the environment pill (the drying and heating menu) and
 * @p alert_cb on the alert slot, @p menu_cb on the menu glyph at the top left; any may be NULL, and the slot stays
 * reserved either way. */
lv_obj_t *ui_unit_header(lv_obj_t *parent, const view_unit_t *u,
                         lv_event_cb_t env_cb, void *env_user,
                         lv_event_cb_t alert_cb, void *alert_user,
                         lv_event_cb_t menu_cb, void *menu_user);

/** The header's menu glyph alone, in the same place at the top left, for a
 * home screen with no unit to head (the printer's data has not arrived): the
 * other screens stay reachable. @p cb fires on a tap. */
lv_obj_t *ui_unit_header_menu(lv_obj_t *parent, lv_event_cb_t cb, void *user);

#endif /* UI_UNIT_HEADER_H */

/**
 * @file ui_misc.h
 * Small helpers shared by the three "single unit" screens: Settings, This
 * unit and Unit info. Header-only so no build-system change is needed; every
 * function is `static inline` and only the used ones are emitted.
 */
#ifndef UI_MISC_H
#define UI_MISC_H

#include "lvgl.h"

#include <stdint.h>

#include "ui/ui_display.h"
#include "ui/ui_list.h"
#include "ui/ui_nav.h"
#include "ui/ui_theme.h"

/* The body these screens share: a scrolling list under the 44 px header,
 * down to the screen's bottom margin: viewport y 48-312, x 12, w 456. */
#define UI_MISC_LIST_X 12
#define UI_MISC_LIST_Y 48
#define UI_MISC_LIST_W 456
#define UI_MISC_LIST_H 264

static inline void ui_misc_goto_cb(lv_event_t *e)
{
    ui_nav_goto_async((ui_screen_id_t)(int)(intptr_t)lv_event_get_user_data(e));
}

/** A full-width list row that navigates to @p id when tapped. */
static inline lv_obj_t *ui_misc_nav_row(lv_obj_t *col, const char *icon,
                                        const char *title, const char *sub,
                                        ui_screen_id_t id)
{
    return ui_list_row(col, icon, title, sub, NULL, ui_misc_goto_cb,
                       (void *)(intptr_t)id);
}

/** A section caption as a list-column child. */
static inline lv_obj_t *ui_misc_section(lv_obj_t *col, const char *text)
{
    return ui_section_label(col, text);
}

#endif /* UI_MISC_H */

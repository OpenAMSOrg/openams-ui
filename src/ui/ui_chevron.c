/**
 * @file ui_chevron.c
 * The home screen's paging chevrons (see ui_chevron.h).
 *
 * A bare chevron on a UI_TOUCH_MIN target: chrome stays quiet (PRINCIPLES.md 1),
 * so the tiles stay the content. The pair never moves - only the ink changes
 * when a chevron has nowhere to go, which is the one disabled look every other
 * control on the device uses (a dimmed TEXT_MUTED at TEXT_FAINT, as a sheet
 * row does).
 *
 * The chevron is Material Design's own glyph (UI_HOME_FONT_CHEVRON), set at
 * the size of its target: an 11x18 px mark with a 3 px stroke, antialiased like
 * the type around it. The 18 px icon face would draw it at 6x9 px, a stray
 * mark on a panel that is looked at from across a room.
 */

#include "ui/ui_chevron.h"

#include "ui/ui_home.h"

#include <stdbool.h>
#include <stdint.h>

/** One target: @p index 0 moves a page left, 1 moves it right. Its position
 *  and size are fixed (ui_home.h); @p enabled only decides the ink and
 *  whether the target is there to be tapped. */
static lv_obj_t *chevron(lv_obj_t *strip, int index, bool enabled,
                         lv_event_cb_t cb, void *user)
{
    lv_obj_t *b = lv_obj_create(strip);
    lv_obj_t *g;

    lv_obj_remove_style_all(b);
    lv_obj_set_scrollable(b, false);
    lv_obj_set_pos(b, UI_HOME_CHEVRON_X(index), UI_HOME_CHEVRON_Y);
    lv_obj_set_size(b, UI_HOME_CHEVRON_S, UI_HOME_CHEVRON_S);
    lv_obj_set_clickable(b, enabled);

    if(enabled) {
        /* A pressed state, so a tap that lands is seen (PRINCIPLES.md 10). */
        lv_obj_set_style_radius(b, 8, 0);
        lv_obj_set_style_bg_color(b, UI_TOK_TEXT, LV_STATE_PRESSED);
        lv_obj_set_style_bg_opa(b, LV_OPA_20, LV_STATE_PRESSED);
        lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, user);
    }

    g = ui_home_label(b, index == 0 ? UI_HOME_ICON_PAGE_LEFT : UI_HOME_ICON_PAGE_RIGHT,
                      UI_HOME_FONT_CHEVRON,
                      enabled ? UI_TOK_TEXT_MUTED : UI_TOK_TEXT_FAINT, 0, 0);
    lv_obj_center(g);
    return b;
}

void ui_chevron_pair(lv_obj_t *strip, int page, int pages, lv_event_cb_t cb)
{
    if(!strip || pages <= 1) return;

    chevron(strip, 0, page > 0, cb, (void *)(intptr_t)-1);
    chevron(strip, 1, page + 1 < pages, cb, (void *)(intptr_t)1);
}
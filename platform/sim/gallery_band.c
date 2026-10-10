/**
 * @file gallery_band.c
 * Gallery page for the action band family: the three kinds stacked, one
 * disabled, and both two-half pairings. Each band is built at its natural
 * place (the foot of the screen) and then moved up, so what is shown is the
 * component's own geometry.
 */

#include "gallery.h"

#include "ui/ui_band.h"

void gallery_page_band(lv_obj_t *scr)
{
    lv_obj_t *b;

    b = ui_action_bar(scr, "Start", UI_ACTION_ACCENT, NULL, NULL);
    lv_obj_set_y(b, 4);

    b = ui_action_bar(scr, "Stop", UI_ACTION_DANGER, NULL, NULL);
    lv_obj_set_y(b, 44);

    b = ui_action_bar(scr, "Back home", UI_ACTION_NEUTRAL, NULL, NULL);
    lv_obj_set_y(b, 84);

    b = ui_action_bar(scr, "Start", UI_ACTION_ACCENT, NULL, NULL);
    ui_action_bar_set_enabled(b, false);
    lv_obj_set_y(b, 124);

    b = ui_action_bar2(scr, "Reset", UI_ACTION_NEUTRAL, NULL, NULL,
                       "Save", UI_ACTION_ACCENT, NULL, NULL);
    ui_action_bar2_set_half_enabled(b, 0, false);
    lv_obj_set_y(b, 164);

    b = ui_action_bar2(scr, "Retry", UI_ACTION_ACCENT, NULL, NULL,
                       "Stop", UI_ACTION_DANGER, NULL, NULL);
    lv_obj_set_y(b, 204);
}

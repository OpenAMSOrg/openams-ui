/**
 * @file screen_debug.c
 * Hidden screen reached by long-pressing a header title: switch the dummy
 * scenario. A technician's screen, so it stays a plain list, in the design
 * language (ui_kit). Which unit this display is bound to is Menu -> "This
 * unit" (screen_order.c) - the one place that choice is made.
 */
#include "view/strings.h"
#include "ui/ui_screens.h"

#include "app/app.h"
#include "ui/ui_kit.h"

#include <stdint.h>

static void on_scenario_click(lv_event_t *e)
{
    int index = (int)(intptr_t)lv_event_get_user_data(e);

    app_set_scenario(app_scenario_name(index));
}

static lv_obj_t *build(void)
{
    lv_obj_t *scr = ui_kit_screen();
    lv_obj_t *col;
    int i, n;

    (void)ui_kit_header(scr, view_str(VS_SCREEN_DEBUG), true);
    col = ui_kit_column(scr, UI_HOME_HEADER_H + UI_PX(2), UI_HOME_H - UI_HOME_HEADER_H - UI_PX(2));

    ui_kit_section(col, view_str(VS_DEBUG_SCENARIOS));
    n = app_scenario_count();
    for(i = 0; i < n; i++)
        (void)ui_kit_text_row(col, app_scenario_name(i), NULL, NULL, UI_TOK_TEXT_FAINT,
                              on_scenario_click, (void *)(intptr_t)i);
    return scr;
}

static int modal_count(void)
{
    return 0;
}

static void open_modal(int index)
{
    (void)index;
}

const ui_screen_ops_t screen_debug_ops = { build, modal_count, open_modal, NULL };

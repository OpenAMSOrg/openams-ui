/**
 * @file ui_screens.c
 * Screen id -> ops table, plus the shared placeholder screen.
 */
#include "ui/ui_screens.h"
#include "view/strings.h"
#include "ui/ui_theme.h"
#include "ui/ui_widgets.h"

static const ui_screen_ops_t *const s_ops[UI_SCREEN_COUNT] = {
    [UI_SCREEN_HOME] = &screen_home_ops,
    [UI_SCREEN_LOAD] = &screen_load_ops,
    [UI_SCREEN_ALERTS] = &screen_alerts_ops,
    [UI_SCREEN_EDIT] = &screen_edit_ops,
    [UI_SCREEN_ENVIRONMENT] = &screen_environment_ops,
    [UI_SCREEN_PRINT] = &screen_print_ops,
    [UI_SCREEN_REFILL] = &screen_refill_ops,
    [UI_SCREEN_SETTINGS] = &screen_settings_ops,
    [UI_SCREEN_MAPPING] = &screen_mapping_ops,
    [UI_SCREEN_ORDER] = &screen_order_ops,
    [UI_SCREEN_UNIT_INFO] = &screen_unitinfo_ops,
    [UI_SCREEN_DEBUG] = &screen_debug_ops,
};

const ui_screen_ops_t *ui_screen_ops(ui_screen_id_t id)
{
    if(id < 0 || id >= UI_SCREEN_COUNT) return NULL;
    return s_ops[id];
}

lv_obj_t *ui_placeholder_screen(ui_screen_id_t id)
{
    lv_obj_t *scr = lv_obj_create(NULL);
    lv_obj_t *label;
    ui_top_bar(scr, ui_nav_screen_title(id), id != UI_SCREEN_HOME);
    label = lv_label_create(scr);
    lv_label_set_text(label, view_str(VS_UI_COMING_SOON));
    lv_obj_center(label);
    return scr;
}

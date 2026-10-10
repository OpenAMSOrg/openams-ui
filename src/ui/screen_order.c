/**
 * @file screen_order.c
 * Screen 11: "This unit" - the display's binding, in the design language
 * (ui_kit). The units the printer reports in cable order (the view's family
 * title and config name), the bound one marked with a "This display" chip, a
 * nozzle binding row for dual-extruder setups (dimmed, with the reason on tap,
 * while this unit's filament is in the toolhead), and a "Re-scan units" action
 * behind a confirm sheet followed by a "waiting for units" state.
 */
#include "ui/ui_screens.h"

#include "app/actions.h"
#include "app/app.h"

#include "model/mmu_model.h"
#include "ui/ui_icons.h"
#include "ui/ui_kit.h"
#include "ui/ui_sheet.h"
#include "ui/ui_theme.h"
#include "view/strings.h"
#include "view/view.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

/* Set after the re-scan confirm; cleared by Cancel. */
static bool s_waiting;
/* Unit the open bind sheet would bind to. */
static int s_bind_target = -1;


/* ---------------------------------------------------------------- binding */

static void on_confirm_bind(lv_event_t *e)
{
    int target = s_bind_target;

    (void)e;
    ui_sheet_close(NULL);
    if(target >= 0) app_set_this_unit(target);
}

static void open_bind_confirm(int unit)
{
    char title[96];
    mmu_unit_t *u;
    lv_obj_t *sheet;
    lv_obj_t *body;

    if(unit < 0 || unit >= g_mmu.unit_count) return;
    u = &g_mmu.units[unit];
    s_bind_target = unit;

    vs_format(VS_ORDER_BIND_TITLE, title, sizeof title,
              (vs_arg_t[]){ VS_ARG_T("unit", u->name), VS_ARG_T("variant", mmu_variant_name(u->variant)) }, 2);
    /* docs/VISUAL_SPEC_480.md, "This unit": h = 148, body "This display will
     * show unit B." at local (16, 52). */
    sheet = ui_sheet_open(title, 148);
    if(!sheet) return;   /* memory is short: no sheet, nothing to fill */
    body = ui_sheet_body(sheet);
    if(body) {
        lv_obj_t *l = lv_label_create(body);

        lv_obj_remove_style_all(l);
        lv_obj_set_style_text_font(l, UI_FONT_S, 0);
        lv_obj_set_style_text_color(l, UI_COL_TEXT_DIM, 0);
        lv_obj_set_style_pad_hor(l, UI_PAD_XXL, 0);
        lv_label_set_long_mode(l, LV_LABEL_LONG_MODE_WRAP);
        lv_obj_set_width(l, lv_pct(100));
        {
            char body_text[96];

            vs_format(VS_ORDER_BIND_BODY, body_text, sizeof body_text,
                      (vs_arg_t[]){ VS_ARG_T("unit", u->name) }, 1);
            lv_label_set_text(l, body_text);
        }
    }
    ui_sheet_button(sheet, view_str(VS_BTN_BIND), on_confirm_bind, NULL, true);
}

/* ---------------------------------------------------------------- re-scan */

static void on_confirm_rescan(lv_event_t *e)
{
    (void)e;
    actions_log("rescan units");
    s_waiting = true;
    ui_nav_refresh_async();
}

static void open_rescan_confirm(void)
{
    lv_obj_t *sheet = ui_sheet_open(view_str(VS_ORDER_RESCAN_TITLE), 168);
    if(!sheet) return;   /* memory is short: no sheet, nothing to fill */

    ui_sheet_row(sheet, UI_ICON_REFRESH, view_str(VS_ORDER_RESCAN_HEAD),
                 view_str(VS_ORDER_RESCAN_NOTE),
                 NULL, NULL, false);
    ui_sheet_button(sheet, view_str(VS_BTN_RESCAN), on_confirm_rescan, NULL, true);
}

static void on_rescan_click(lv_event_t *e)
{
    (void)e;
    open_rescan_confirm();
}

static void on_cancel_wait(lv_event_t *e)
{
    (void)e;
    s_waiting = false;
    ui_nav_refresh_async();
}

/* ------------------------------------------------------------------- rows */

static void on_unit_click(lv_event_t *e)
{
    open_bind_confirm((int)(intptr_t)lv_event_get_user_data(e));
}

static void add_unit_row(lv_obj_t *col, int unit)
{
    mmu_unit_t *u = &g_mmu.units[unit];
    bool mine = mmu_is_this_unit(unit);
    view_unit_t *vu = malloc(sizeof *vu);
    char sub[64];
    lv_obj_t *row;
    int tw;

    if(!vu || !view_unit(&g_mmu, unit, vu)) {
        free(vu);
        return;
    }
    row = ui_kit_row(col, 52, mine ? NULL : on_unit_click, (void *)(intptr_t)unit);
    tw = ui_home_text_w(vu->title, UI_HOME_FONT_BODY);
    ui_home_label(row, vu->title, UI_HOME_FONT_BODY, UI_TOK_TEXT, 0, 0);
    ui_home_label(row, vu->subtitle, UI_HOME_FONT_META, UI_TOK_TEXT_MUTED, tw + 6, 2);
    vs_format(VS_ORDER_SLOTS, sub, sizeof sub, (vs_arg_t[]){ VS_ARG_N("n", (int)u->slot_count) }, 1);
    ui_home_label(row, sub, UI_HOME_FONT_META, UI_TOK_TEXT_MUTED, 0, 20);
    if(mine) {
        ui_kit_tag(row, view_str(VS_ORDER_THIS_DISPLAY), UI_KIT_TONE_ACCENT,
                   UI_HOME_INNER_W - 24 - ui_kit_tag_w(view_str(VS_ORDER_THIS_DISPLAY)), 4);
    }
    else {
        ui_home_label(row, UI_ICON_CHEVRON_RIGHT, UI_HOME_FONT_ICON, UI_TOK_TEXT_FAINT,
                      UI_HOME_INNER_W - 24 - 18, 6);
    }
    free(vu);
}

static void on_side(lv_event_t *e)
{
    int side = (int)(intptr_t)lv_event_get_user_data(e);

    mmu_this()->side = (mmu_side_t)side;
    actions_log("side %s %s", mmu_this()->name, side == MMU_SIDE_RIGHT ? "right" : "left");
    ui_nav_refresh_async();
}

static void on_side_locked(lv_event_t *e)
{
    (void)e;
    ui_sheet_toast(view_str(VS_ORDER_LOCKED));
}

static void add_side_row(lv_obj_t *col)
{
    mmu_unit_t *u = mmu_this();
    bool locked = (mmu_toolhead_source() == MMU_FEED_THIS);
    lv_obj_t *row = ui_kit_row(col, 52, NULL, NULL);
    int x = UI_HOME_INNER_W - 24 - 72 - 8 - 72;
    lv_obj_t *l, *r;

    ui_home_label(row, view_str(VS_ORDER_FEEDS), UI_HOME_FONT_BODY, UI_TOK_TEXT, 0, 0);
    ui_home_label(row, view_str(VS_ORDER_NOZZLES), UI_HOME_FONT_META, UI_TOK_TEXT_MUTED, 0, 20);
    l = ui_kit_chip(row, view_str(VS_SIDE_LEFT), x, 4, 72, u->side == MMU_SIDE_LEFT,
                    locked ? on_side_locked : on_side, (void *)(intptr_t)MMU_SIDE_LEFT);
    r = ui_kit_chip(row, view_str(VS_SIDE_RIGHT), x + 80, 4, 72, u->side == MMU_SIDE_RIGHT,
                    locked ? on_side_locked : on_side, (void *)(intptr_t)MMU_SIDE_RIGHT);
    (void)l;
    (void)r;
}

/* ------------------------------------------------------------------ build */

static void build_waiting(lv_obj_t *col)
{
    (void)ui_kit_text_row(col, view_str(VS_ORDER_WAITING),
                          view_str(VS_ORDER_WAITING_NOTE),
                          UI_ICON_REFRESH, UI_TOK_TEXT_FAINT, NULL, NULL);
    (void)ui_kit_text_row(col, view_str(VS_LABEL_CANCEL), NULL, UI_ICON_CLOSE, UI_TOK_TEXT_FAINT, on_cancel_wait, NULL);
}

static void build_units(lv_obj_t *col)
{
    int i;

    for(i = 0; i < g_mmu.unit_count; i++) add_unit_row(col, i);
    if(g_mmu.unit_count == 0)
        (void)ui_kit_text_row(col, view_str(VS_ORDER_NONE), NULL, NULL, UI_TOK_TEXT, NULL, NULL);

    if(g_mmu.dual_extruder) add_side_row(col);

    (void)ui_kit_text_row(col, view_str(VS_ORDER_RESCAN), view_str(VS_ORDER_RESCAN_SUB),
                          UI_ICON_CHEVRON_RIGHT, UI_TOK_TEXT_FAINT, on_rescan_click, NULL);
}

static lv_obj_t *build(void)
{
    lv_obj_t *scr = ui_kit_screen();
    lv_obj_t *col;

    (void)ui_kit_header(scr, ui_nav_screen_title(UI_SCREEN_ORDER), true);
    col = ui_kit_column(scr, UI_HOME_HEADER_H + 2, UI_HOME_H - UI_HOME_HEADER_H - 2);

    if(s_waiting) build_waiting(col);
    else build_units(col);

    return scr;
}

static int modal_count(void)
{
    return 2;
}

static void open_modal(int index)
{
    int i;
    int target = -1;

    if(index == 1) {
        open_rescan_confirm();
        return;
    }

    for(i = 0; i < g_mmu.unit_count; i++) {
        if(!mmu_is_this_unit(i)) {
            target = i;
            break;
        }
    }
    if(target < 0) target = 0;
    open_bind_confirm(target);
}

const ui_screen_ops_t screen_order_ops = { build, modal_count, open_modal, NULL };

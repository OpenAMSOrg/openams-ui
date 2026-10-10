/**
 * @file ui_spool_sheets.c
 * "New spool, full?" and "Assign spool" bottom sheets (D4a).
 */

#include "ui/ui_spool_sheets.h"

#include "app/actions.h"
#include "model/mmu_model.h"
#include "ui/ui_icons.h"
#include "ui/ui_kit.h"
#include "ui/ui_sheet.h"
#include "ui/ui_theme.h"
#include "view/strings.h"

#include <stdio.h>
#include <string.h>

/* ---------------------------------------------------------------- confirm */

/* Static state for the confirmation flow. */
static int    s_conf_unit = -1;
static int    s_conf_slot = -1;
static int32_t s_conf_full_g;   /* full weight to offer as "Yes, full" */
static int    s_conf_pct = 50;  /* stepper percentage, 10..100 step 10 */

static void confirm_send(void)
{
    char id[MMU_SLOT_ID_LEN];
    int32_t grams;

    if(s_conf_unit < 0 || s_conf_slot < 0) return;
    mmu_slot_id(s_conf_unit, s_conf_slot, id, sizeof id);
    grams = s_conf_full_g > 0 ? (s_conf_full_g * s_conf_pct / 100) : 0;
    actions_log("spool confirm %s %d", id, (int)grams);
    s_conf_unit = -1;
    ui_sheet_close(NULL);
}

static void on_conf_yes(lv_event_t *e)
{
    (void)e;
    s_conf_pct = 100;
    confirm_send();
}

static void on_conf_dec(lv_event_t *e)
{
    lv_obj_t *lbl = (lv_obj_t *)lv_event_get_user_data(e);

    if(s_conf_pct > 10) s_conf_pct -= 10;
    if(lbl) lv_label_set_text_fmt(lbl, "%d%%", s_conf_pct);
}

static void on_conf_inc(lv_event_t *e)
{
    lv_obj_t *lbl = (lv_obj_t *)lv_event_get_user_data(e);

    if(s_conf_pct < 100) s_conf_pct += 10;
    if(lbl) lv_label_set_text_fmt(lbl, "%d%%", s_conf_pct);
}

static void on_conf_save(lv_event_t *e)
{
    (void)e;
    confirm_send();
}

/* Open the stepper phase: "How full?" with -10% / +10% and "Save". */
static void open_stepper_sheet(void)
{
    char id[MMU_SLOT_ID_LEN];
    lv_obj_t *sheet, *body, *row, *pct_lbl, *btn;
    char title[96];

    if(s_conf_unit < 0) return;
    mmu_slot_id(s_conf_unit, s_conf_slot, id, sizeof id);
    vs_format(VS_SS_HOW_FULL, title, sizeof title,
              (vs_arg_t[]){ VS_ARG_T("spool", ui_kit_spool(s_conf_unit, s_conf_slot)) }, 1);

    sheet = ui_sheet_open(title, 200);
    if(!sheet) return;   /* memory is short: no sheet, nothing to fill */
    body  = ui_sheet_body(sheet);

    /* A flex row with -10% label +10% */
    row = lv_obj_create(body);
    lv_obj_remove_style_all(row);
    lv_obj_set_width(row, LV_PCT(100));
    lv_obj_set_height(row, 48);
    lv_obj_set_layout(row, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_BETWEEN,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_scrollable(row, false);
    lv_obj_set_style_pad_left(row,  16, 0);
    lv_obj_set_style_pad_right(row, 16, 0);

    /* "-10%" button */
    btn = lv_button_create(row);
    lv_obj_set_size(btn, 60, 36);
    lv_obj_set_style_bg_color(btn, UI_COL_PANEL_HI, 0);
    {
        lv_obj_t *l = lv_label_create(btn);
        lv_label_set_text(l, "-10%");
        lv_obj_set_style_text_font(l, UI_FONT_S, 0);
        lv_obj_set_style_text_color(l, UI_COL_TEXT, 0);
        lv_obj_center(l);
    }

    /* percentage label — passed to the -/+ callbacks as user_data */
    pct_lbl = lv_label_create(row);
    lv_obj_set_style_text_font(pct_lbl, UI_FONT_XL, 0);
    lv_obj_set_style_text_color(pct_lbl, UI_COL_TEXT, 0);
    lv_label_set_text_fmt(pct_lbl, "%d%%", s_conf_pct);

    /* "+10%" button */
    {
        lv_obj_t *b2 = lv_button_create(row);
        lv_obj_set_size(b2, 60, 36);
        lv_obj_set_style_bg_color(b2, UI_COL_PANEL_HI, 0);
        {
            lv_obj_t *l = lv_label_create(b2);
            lv_label_set_text(l, "+10%");
            lv_obj_set_style_text_font(l, UI_FONT_S, 0);
            lv_obj_set_style_text_color(l, UI_COL_TEXT, 0);
            lv_obj_center(l);
        }
        lv_obj_add_event_cb(b2, on_conf_inc, LV_EVENT_CLICKED, pct_lbl);
    }
    lv_obj_add_event_cb(btn, on_conf_dec, LV_EVENT_CLICKED, pct_lbl);

    ui_sheet_fit(sheet);
    ui_sheet_button(sheet, view_str(VS_BTN_SAVE), on_conf_save, NULL, true);
}

static void on_conf_no(lv_event_t *e)
{
    (void)e;
    ui_sheet_close(NULL);
    open_stepper_sheet();
}

void ui_spool_confirm_open(int unit, int slot)
{
    const mmu_unit_t *u;
    const mmu_slot_t *sl;
    lv_obj_t *sheet;
    char id[MMU_SLOT_ID_LEN];
    char title[96];
    int32_t swatch;

    if(unit < 0 || unit >= (int)g_mmu.unit_count) return;
    u = &g_mmu.units[unit];
    if(slot < 0 || slot >= (int)u->slot_count) return;
    sl = &u->slots[slot];

    s_conf_unit  = unit;
    s_conf_slot  = slot;
    s_conf_pct   = 50;
    s_conf_full_g = sl->remaining_g > 0 ? (int32_t)sl->remaining_g : 1000;

    mmu_slot_id(unit, slot, id, sizeof id);
    vs_format(VS_SS_NEW_SPOOL, title, sizeof title,
              (vs_arg_t[]){ VS_ARG_T("spool", ui_kit_spool(unit, slot)) }, 1);

    swatch = (sl->info_source != MMU_INFO_UNKNOWN && sl->color_known) ? (int32_t)sl->color : -1;
    sheet = ui_sheet_open(title, 160);
    if(!sheet) return;   /* memory is short: no sheet, nothing to fill */
    ui_sheet_header(sheet, swatch, NULL, false);

    if(sl->material[0]) {
        lv_obj_t *body = ui_sheet_body(sheet);
        lv_obj_t *lbl  = lv_label_create(body);
        lv_obj_remove_style_all(lbl);
        lv_label_set_text(lbl, sl->material);
        lv_obj_set_style_text_font(lbl, UI_FONT_M, 0);
        lv_obj_set_style_text_color(lbl, UI_COL_TEXT_DIM, 0);
        ui_sheet_fit(sheet);
    }

    ui_sheet_button(sheet, view_str(VS_BTN_YES_FULL),  on_conf_yes, NULL, true);
    ui_sheet_button(sheet, view_str(VS_BTN_NO), on_conf_no,  NULL, false);
}

/* ----------------------------------------------------------------- assign */

/* Demo spools shown in non-live mode or when the list is empty. */
static const mmu_spool_t k_demo_spools[] = {
    { .id = 1, .material = "PLA",  .vendor = "Bambu Lab", .color = 0xE63946, .color_known = true, .remaining_g = 800 },
    { .id = 2, .material = "PETG", .vendor = "Polymaker", .color = 0x457B9D, .color_known = true, .remaining_g = 1000 },
    { .id = 3, .material = "ABS",  .vendor = "eSUN",      .color = 0xA8DADC, .color_known = true, .remaining_g = 350 },
};
#define DEMO_SPOOL_COUNT 3

static int    s_assign_unit = -1;
static int    s_assign_slot = -1;

static void add_spool_row(lv_obj_t *body, const mmu_spool_t *sp, int spool_idx)
{
    char label[96], sub[32];
    lv_obj_t *row, *swatch, *text_col, *title_lbl, *sub_lbl;

    vs_format(VS_SPOOL_OPTION_NO_GRAMS, label, sizeof label,
              (vs_arg_t[]){ VS_ARG_T("vendor", sp->vendor[0] ? sp->vendor : view_str(VS_SS_UNKNOWN)),
                            VS_ARG_T("material", sp->material[0] ? sp->material : view_str(VS_UNKNOWN_MARK)) }, 2);
    if(sp->remaining_g >= 0) vs_format(VS_SS_GRAMS, sub, sizeof sub, (vs_arg_t[]){ VS_ARG_N("g", (int)sp->remaining_g) }, 1);
    else                     sub[0] = '\0';

    /* 44 px row with a swatch on the left */
    row = lv_obj_create(body);
    lv_obj_remove_style_all(row);
    lv_obj_set_size(row, LV_PCT(100), 44);
    lv_obj_set_style_bg_color(row, UI_COL_PANEL, 0);
    lv_obj_set_style_bg_opa(row, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(row, 10, 0);
    lv_obj_set_style_bg_color(row, UI_COL_PANEL_HI, LV_STATE_PRESSED);
    lv_obj_set_scrollable(row, false);
    lv_obj_set_clickable(row, true);
    lv_obj_add_event_cb(row, NULL, LV_EVENT_CLICKED,
                        (void *)(intptr_t)(spool_idx + 1));

    /* color swatch circle */
    swatch = lv_obj_create(row);
    lv_obj_remove_style_all(swatch);
    lv_obj_set_pos(swatch, 12, 12);
    lv_obj_set_size(swatch, 20, 20);
    lv_obj_set_style_radius(swatch, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(swatch, sp->color_known ? lv_color_hex(sp->color) : UI_COL_TEXT_FAINT, 0);
    lv_obj_set_style_bg_opa(swatch, LV_OPA_COVER, 0);
    lv_obj_set_clickable(swatch, false);

    /* text column */
    text_col = lv_obj_create(row);
    lv_obj_remove_style_all(text_col);
    lv_obj_set_pos(text_col, 40, 4);
    lv_obj_set_size(text_col, 380, 36);
    lv_obj_set_scrollable(text_col, false);
    lv_obj_set_clickable(text_col, false);

    title_lbl = lv_label_create(text_col);
    lv_obj_remove_style_all(title_lbl);
    lv_label_set_text(title_lbl, label);
    lv_label_set_long_mode(title_lbl, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_set_style_text_font(title_lbl, UI_FONT_M, 0);
    lv_obj_set_style_text_color(title_lbl, UI_COL_TEXT, 0);
    lv_obj_set_size(title_lbl, sub[0] ? 290 : 380, 18);
    lv_obj_set_pos(title_lbl, 0, 2);

    if(sub[0]) {
        sub_lbl = lv_label_create(text_col);
        lv_obj_remove_style_all(sub_lbl);
        lv_label_set_text(sub_lbl, sub);
        lv_obj_set_style_text_font(sub_lbl, UI_FONT_S, 0);
        lv_obj_set_style_text_color(sub_lbl, UI_COL_TEXT_DIM, 0);
        lv_obj_set_pos(sub_lbl, 294, 4);
    }
    else {
        (void)sub_lbl;
    }

    return;
    /* spool_idx is unused after the cb is set; prevent warning */
    (void)spool_idx;
}

/* Event handler for spool rows — user_data is (spool_idx + 1). */
static void on_spool_tap(lv_event_t *e)
{
    int idx = (int)(intptr_t)lv_event_get_user_data(e) - 1;
    const mmu_spool_t *spools;
    int count;
    char slot_id[MMU_SLOT_ID_LEN];

    if(idx < 0 || s_assign_unit < 0) return;

    if(mmu_model_is_live() && g_mmu.spool_count > 0) {
        spools = g_mmu.spools;
        count  = (int)g_mmu.spool_count;
    } else {
        spools = k_demo_spools;
        count  = DEMO_SPOOL_COUNT;
    }
    if(idx >= count) return;

    mmu_slot_id(s_assign_unit, s_assign_slot, slot_id, sizeof slot_id);
    actions_log("link %s %d", slot_id, (int)spools[idx].id);
    ui_sheet_close(NULL);
}

/* Unlink row handler */
static void on_unlink_tap(lv_event_t *e)
{
    char slot_id[MMU_SLOT_ID_LEN];
    (void)e;

    if(s_assign_unit < 0) return;
    mmu_slot_id(s_assign_unit, s_assign_slot, slot_id, sizeof slot_id);
    actions_log("unlink %s", slot_id);
    ui_sheet_close(NULL);
}

void ui_spool_assign_open(int unit, int slot)
{
    const mmu_spool_t *spools;
    int count, i;
    const mmu_unit_t *u;
    const mmu_slot_t *sl;
    lv_obj_t *sheet, *body;
    char title[96];
    char slot_id[MMU_SLOT_ID_LEN];

    if(unit < 0 || unit >= (int)g_mmu.unit_count) return;
    u = &g_mmu.units[unit];
    if(slot < 0 || slot >= (int)u->slot_count) return;
    sl = &u->slots[slot];

    s_assign_unit = unit;
    s_assign_slot = slot;

    mmu_slot_id(unit, slot, slot_id, sizeof slot_id);

    /* Trigger a spool list refresh (RPC fires in platform_action). */
    actions_log("spool list");

    vs_format(VS_SS_ASSIGN, title, sizeof title, (vs_arg_t[]){ VS_ARG_T("spool", ui_kit_spool(unit, slot)) }, 1);
    sheet = ui_sheet_open(title, 220);
    if(!sheet) return;   /* memory is short: no sheet, nothing to fill */
    body  = ui_sheet_body(sheet);

    if(mmu_model_is_live() && g_mmu.spool_count > 0) {
        spools = g_mmu.spools;
        count  = (int)g_mmu.spool_count;
    } else {
        spools = k_demo_spools;
        count  = DEMO_SPOOL_COUNT;
    }

    for(i = 0; i < count; i++) {
        lv_obj_t *row_obj;

        add_spool_row(body, &spools[i], i);
        /* Wire the click on the row we just appended (last child of body). */
        row_obj = lv_obj_get_child(body, lv_obj_get_child_count(body) - 1);
        if(row_obj) {
            lv_obj_remove_event_cb(row_obj, NULL);
            lv_obj_add_event_cb(row_obj, on_spool_tap, LV_EVENT_CLICKED,
                                (void *)(intptr_t)(i + 1));
        }
    }

    /* "Unlink" row when the slot already has a Spoolman spool. */
    if(sl->spool_id > 0) {
        ui_sheet_row(sheet, UI_ICON_LINK_VARIANT, view_str(VS_SS_UNLINK), NULL,
                     on_unlink_tap, NULL, false);
    }

    ui_sheet_fit(sheet);
}

/**
 * @file ui_group_picker.c
 * The "change group" sheet: given a bay, list the groups it could join (every
 * group, the current one checked), "New group" and, when the bay is already
 * assigned, "Remove from group". Given a group, list this unit's bays that
 * could fill it. All three close the sheet, toast the move and refresh the
 * active screen. See docs/GROUPS.md, docs/VISUAL_SPEC_480.md "Change-group
 * sheet".
 */

#include "ui/ui_group_picker.h"

#include "app/actions.h"
#include "model/mmu_model.h"
#include "ui/ui_icons.h"
#include "ui/ui_input.h"
#include "ui/ui_nav.h"
#include "ui/ui_kit.h"
#include "view/strings.h"
#include "ui/ui_sheet.h"
#include "ui/ui_theme.h"
#include "ui/ui_widgets.h"

#include <stdio.h>
#include <string.h>

/* The bay a "change group" sheet targets; only one such sheet is ever open. */
static int s_bay_unit = -1;
static int s_bay_slot = -1;

/* The group an "assign a bay" sheet targets. */
static int s_assign_group = -1;

/* The candidate number ("T<n>") the New-group stepper is showing. */
static int s_new_num;
static lv_obj_t *s_new_val;

static void toast_move(int unit, int slot, int group)
{
    char buf[128];

    vs_format(VS_GP_MOVED, buf, sizeof buf,
              (vs_arg_t[]){ VS_ARG_T("spool", ui_kit_spool(unit, slot)),
                            VS_ARG_T("group", g_mmu.groups[group].name) }, 2);
    ui_sheet_close(NULL);
    ui_sheet_toast(buf);
    ui_nav_refresh_async();
}

/* ------------------------------------------------------------- divider row */

/* A hairline at the top of every row but the first, matching the slot
 * sheet's own rows (docs/VISUAL_SPEC_480.md "Slot sheet"). */
static void row_divider(lv_obj_t *row, bool first)
{
    lv_obj_t *l;

    if(first) return;
    l = lv_obj_create(row);
    lv_obj_remove_style_all(l);
    lv_obj_set_pos(l, UI_PX(16), 0);
    lv_obj_set_size(l, UI_SCREEN_W - 2 * UI_PX(16), UI_PX(1));
    lv_obj_set_style_bg_color(l, UI_COL_LINE, 0);
    lv_obj_set_style_bg_opa(l, LV_OPA_COVER, 0);
    lv_obj_set_scrollable(l, false);
}

/* --------------------------------------------------- bay -> pick a group */

static void on_pick_group(lv_event_t *e)
{
    int group = (int)(intptr_t)lv_event_get_user_data(e);
    char id[MMU_SLOT_ID_LEN];

    if(s_bay_unit < 0 || s_bay_slot < 0) return;
    mmu_group_assign(group, s_bay_unit, s_bay_slot);
    mmu_slot_id(s_bay_unit, s_bay_slot, id, sizeof id);
    actions_log("assign %s %s", g_mmu.groups[group].name, id);
    toast_move(s_bay_unit, s_bay_slot, group);
}

/* One 44 px row: group name, its member count, and a check on the group the
 * bay is already in (that row is not clickable - it is already the answer). */
static void group_row(lv_obj_t *sheet, int g, int current, bool first)
{
    lv_obj_t *body = ui_sheet_body(sheet);
    lv_obj_t *row, *l;
    char count[64];
    bool is_current = (g == current);
    lv_point_t sz;

    if(!body) return;

    row = lv_obj_create(body);
    lv_obj_remove_style_all(row);
    lv_obj_set_width(row, LV_PCT(100));
    lv_obj_set_height(row, UI_PX(44));
    lv_obj_set_scrollable(row, false);
    row_divider(row, first);

    if(!is_current) {
        lv_obj_set_clickable(row, true);
        lv_obj_set_style_bg_color(row, UI_COL_PANEL, LV_STATE_PRESSED);
        lv_obj_set_style_bg_opa(row, LV_OPA_COVER, LV_STATE_PRESSED);
        lv_obj_add_event_cb(row, on_pick_group, LV_EVENT_CLICKED, (void *)(intptr_t)g);
    }

    l = lv_label_create(row);
    lv_obj_remove_style_all(l);
    lv_label_set_text(l, g_mmu.groups[g].name);
    lv_obj_set_style_text_font(l, UI_FONT_M, 0);
    lv_obj_set_style_text_color(l, UI_COL_TEXT, 0);
    lv_obj_align(l, LV_ALIGN_LEFT_MID, UI_PX(16), 0);

    lv_text_get_size(&sz, g_mmu.groups[g].name, UI_FONT_M, 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
    vs_format(VS_GP_MEMBERS, count, sizeof count,
              (vs_arg_t[]){ VS_ARG_N("n", (int)g_mmu.groups[g].count) }, 1);
    l = lv_label_create(row);
    lv_obj_remove_style_all(l);
    lv_label_set_text(l, count);
    lv_obj_set_style_text_font(l, UI_FONT_S, 0);
    lv_obj_set_style_text_color(l, UI_COL_TEXT_DIM, 0);
    lv_obj_align(l, LV_ALIGN_LEFT_MID, UI_PX(16) + (int)sz.x + UI_PX(8), 0);

    if(is_current) {
        l = lv_label_create(row);
        lv_obj_remove_style_all(l);
        lv_label_set_text(l, UI_ICON_CHECK);
        lv_obj_set_style_text_font(l, UI_FONT_ICON, 0);
        lv_obj_set_style_text_color(l, UI_COL_ACCENT, 0);
        lv_obj_align(l, LV_ALIGN_RIGHT_MID, -UI_PX(16), 0);
    }
    ui_sheet_fit(sheet);
}

/* -------------------------------------------------------------- new group */

static bool group_num_taken(int n)
{
    char name[16];      /* "T" + up to 11 digits (INT_MIN) + NUL */
    int g;

    snprintf(name, sizeof name, "T%d", n);
    for(g = 0; g < (int)g_mmu.group_count; g++)
        if(strcmp(g_mmu.groups[g].name, name) == 0) return true;
    return false;
}

/* Step from @p n in direction @p dir (+-1) to the next name not already
 * taken; if none exists that way, stay put. */
static int step_free(int n, int dir)
{
    int c = n + dir;

    while(c >= 0 && group_num_taken(c)) c += dir;
    return (c < 0) ? n : c;
}

static void set_new_label(void)
{
    char name[16];      /* "T" + up to 11 digits (INT_MIN) + NUL */

    snprintf(name, sizeof name, "T%d", s_new_num);
    if(s_new_val) lv_label_set_text(s_new_val, name);
}

static void on_new_minus(lv_event_t *e)
{
    (void)e;
    s_new_num = step_free(s_new_num, -1);
    set_new_label();
}

static void on_new_plus(lv_event_t *e)
{
    (void)e;
    s_new_num = step_free(s_new_num, 1);
    set_new_label();
}

static void on_new_create(lv_event_t *e)
{
    char name[16];      /* "T" + up to 11 digits (INT_MIN) + NUL */
    char id[MMU_SLOT_ID_LEN];
    int group;

    (void)e;
    if(s_bay_unit < 0 || s_bay_slot < 0) return;
    snprintf(name, sizeof name, "T%d", s_new_num);
    actions_log("create group %s", name);
    if(mmu_model_is_live()) {
        /* The host creates and fills the group; its next status shows it. */
        mmu_slot_id(s_bay_unit, s_bay_slot, id, sizeof id);
        actions_log("assign %s %s", name, id);
        ui_sheet_close(NULL);
        return;
    }
    group = mmu_group_create(name);
    if(group < 0) {
        ui_sheet_close(NULL);
        return;
    }
    mmu_group_assign(group, s_bay_unit, s_bay_slot);
    mmu_slot_id(s_bay_unit, s_bay_slot, id, sizeof id);
    actions_log("assign %s %s", g_mmu.groups[group].name, id);
    toast_move(s_bay_unit, s_bay_slot, group);
}

/* docs/VISUAL_SPEC_480.md "Change-group sheet" -> "New group": a stepper
 * defaulting to the next free T{n}, one Create button, no keyboard. */
static void open_new_group(lv_event_t *e)
{
    lv_obj_t *sheet, *body, *btn, *l;

    (void)e;
    sheet = ui_sheet_open(view_str(VS_GP_NEW), UI_PX(160));
    if(!sheet) return;   /* memory is short: no sheet, nothing to fill */
    body = ui_sheet_body(sheet);
    if(!body) return;

    s_new_num = step_free(-1, 1);

    s_new_val = ui_stepper(body, UI_PX(16), UI_PX(8), UI_SCREEN_W - 2 * UI_PX(16), "", on_new_minus, on_new_plus, NULL);
    set_new_label();

    btn = lv_button_create(body);
    lv_obj_remove_style_all(btn);
    lv_obj_set_pos(btn, UI_PX(16), UI_PX(60));
    lv_obj_set_size(btn, UI_SCREEN_W - 2 * UI_PX(16), UI_PX(36));
    lv_obj_set_style_radius(btn, UI_RADIUS, 0);
    lv_obj_set_style_bg_color(btn, UI_COL_ACCENT, 0);
    lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(btn, lv_color_mix(lv_color_white(), UI_COL_ACCENT, LV_OPA_10),
                              LV_STATE_PRESSED);
    lv_obj_set_scrollable(btn, false);
    lv_obj_add_event_cb(btn, on_new_create, LV_EVENT_CLICKED, NULL);

    l = lv_label_create(btn);
    lv_obj_remove_style_all(l);
    lv_label_set_text(l, view_str(VS_BTN_CREATE));
    lv_obj_set_style_text_font(l, UI_FONT_L, 0);
    lv_obj_set_style_text_color(l, UI_COL_DARK_TEXT, 0);
    lv_obj_center(l);

    ui_sheet_fit(sheet);
}

/* ----------------------------------------------------------- remove group */

static void on_remove_confirm(lv_event_t *e)
{
    char id[MMU_SLOT_ID_LEN];
    char buf[96];

    (void)e;
    if(s_bay_unit < 0 || s_bay_slot < 0) return;
    mmu_slot_id(s_bay_unit, s_bay_slot, id, sizeof id);
    mmu_group_unassign(s_bay_unit, s_bay_slot);
    actions_log("unassign %s", id);

    vs_format(VS_GP_REMOVED, buf, sizeof buf,
              (vs_arg_t[]){ VS_ARG_T("spool", ui_kit_spool(s_bay_unit, s_bay_slot)) }, 1);
    ui_sheet_close(NULL);
    ui_sheet_toast(buf);
    ui_nav_refresh_async();
}

/* docs/VISUAL_SPEC_480.md "Change-group sheet" -> "Remove from T1": a single
 * destructive confirm button, no Cancel (the scrim dismisses). */
static void open_remove_confirm(lv_event_t *e)
{
    lv_obj_t *sheet, *body, *btn, *l;
    char title[128];
    char id[MMU_SLOT_ID_LEN];
    int current;

    (void)e;
    if(s_bay_unit < 0 || s_bay_slot < 0) return;
    current = mmu_slot_group(s_bay_unit, s_bay_slot);
    if(current < 0) return;

    mmu_slot_id(s_bay_unit, s_bay_slot, id, sizeof id);
    vs_format(VS_GP_REMOVE_TITLE, title, sizeof title,
              (vs_arg_t[]){ VS_ARG_T("spool", ui_kit_spool(s_bay_unit, s_bay_slot)),
                            VS_ARG_T("group", g_mmu.groups[current].name) }, 2);

    sheet = ui_sheet_open(title, UI_PX(148));
    if(!sheet) return;   /* memory is short: no sheet, nothing to fill */
    body = ui_sheet_body(sheet);
    if(!body) return;

    btn = lv_button_create(body);
    lv_obj_remove_style_all(btn);
    lv_obj_set_pos(btn, UI_PX(16), UI_PX(48));
    lv_obj_set_size(btn, UI_SCREEN_W - 2 * UI_PX(16), UI_PX(36));
    lv_obj_set_style_radius(btn, UI_RADIUS, 0);
    lv_obj_set_style_bg_color(btn, UI_COL_PANEL_HI, 0);
    lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(btn, lv_color_mix(lv_color_white(), UI_COL_PANEL_HI, LV_OPA_10),
                              LV_STATE_PRESSED);
    lv_obj_set_scrollable(btn, false);
    lv_obj_add_event_cb(btn, on_remove_confirm, LV_EVENT_CLICKED, NULL);

    l = lv_label_create(btn);
    lv_obj_remove_style_all(l);
    lv_label_set_text(l, view_str(VS_BTN_REMOVE));
    lv_obj_set_style_text_font(l, UI_FONT_L, 0);
    lv_obj_set_style_text_color(l, UI_COL_ERR, 0);
    lv_obj_center(l);

    ui_sheet_fit(sheet);
}

void ui_group_picker_open(int unit, int slot)
{
    lv_obj_t *sheet;
    char title[96];
    char id[MMU_SLOT_ID_LEN];
    int current;
    int g;

    s_bay_unit = unit;
    s_bay_slot = slot;

    mmu_slot_id(unit, slot, id, sizeof id);
    vs_format(VS_GP_FOR, title, sizeof title, (vs_arg_t[]){ VS_ARG_T("spool", ui_kit_spool(unit, slot)) }, 1);
    sheet = ui_sheet_open(title, UI_SHEET_MAX_H);
    if(!sheet) return;   /* memory is short: no sheet, nothing to fill */

    current = mmu_slot_group(unit, slot);

    for(g = 0; g < (int)g_mmu.group_count; g++) group_row(sheet, g, current, g == 0);

    {
        lv_obj_t *body = ui_sheet_body(sheet);
        lv_obj_t *row, *l;

        if(body) {
            row = lv_obj_create(body);
            lv_obj_remove_style_all(row);
            lv_obj_set_width(row, LV_PCT(100));
            lv_obj_set_height(row, UI_PX(44));
            lv_obj_set_scrollable(row, false);
            lv_obj_set_clickable(row, true);
            lv_obj_set_style_bg_color(row, UI_COL_PANEL, LV_STATE_PRESSED);
            lv_obj_set_style_bg_opa(row, LV_OPA_COVER, LV_STATE_PRESSED);
            row_divider(row, g_mmu.group_count == 0);
            lv_obj_add_event_cb(row, open_new_group, LV_EVENT_CLICKED, NULL);

            l = lv_label_create(row);
            lv_obj_remove_style_all(l);
            lv_label_set_text(l, UI_ICON_PLUS);
            lv_obj_set_style_text_font(l, UI_FONT_ICON, 0);
            lv_obj_set_style_text_color(l, UI_COL_TEXT_DIM, 0);
            lv_obj_align(l, LV_ALIGN_LEFT_MID, UI_PX(16), 0);

            l = lv_label_create(row);
            lv_obj_remove_style_all(l);
            lv_label_set_text(l, view_str(VS_GP_NEW));
            lv_obj_set_style_text_font(l, UI_FONT_M, 0);
            lv_obj_set_style_text_color(l, UI_COL_TEXT, 0);
            lv_obj_align(l, LV_ALIGN_LEFT_MID, UI_PX(44), 0);
        }
        ui_sheet_fit(sheet);
    }

    if(current >= 0) {
        lv_obj_t *body = ui_sheet_body(sheet);
        lv_obj_t *row, *l;

        if(body) {
            row = lv_obj_create(body);
            lv_obj_remove_style_all(row);
            lv_obj_set_width(row, LV_PCT(100));
            lv_obj_set_height(row, UI_PX(44));
            lv_obj_set_scrollable(row, false);
            lv_obj_set_clickable(row, true);
            lv_obj_set_style_bg_color(row, UI_COL_PANEL, LV_STATE_PRESSED);
            lv_obj_set_style_bg_opa(row, LV_OPA_COVER, LV_STATE_PRESSED);
            row_divider(row, false);
            lv_obj_add_event_cb(row, open_remove_confirm, LV_EVENT_CLICKED, NULL);

            l = lv_label_create(row);
            lv_obj_remove_style_all(l);
            vs_format(VS_GP_REMOVE_FROM, title, sizeof title,
                      (vs_arg_t[]){ VS_ARG_T("group", g_mmu.groups[current].name) }, 1);
            lv_label_set_text(l, title);
            lv_obj_set_style_text_font(l, UI_FONT_M, 0);
            lv_obj_set_style_text_color(l, UI_COL_ERR, 0);
            lv_obj_align(l, LV_ALIGN_LEFT_MID, UI_PX(16), 0);
        }
        ui_sheet_fit(sheet);
    }
}

/* --------------------------------------------------- group -> pick a bay */

static void on_pick_bay(lv_event_t *e)
{
    void *ud = lv_event_get_user_data(e);
    int unit = UI_DEC_A(ud);
    int slot = UI_DEC_B(ud);
    char id[MMU_SLOT_ID_LEN];

    if(s_assign_group < 0) return;
    mmu_group_assign(s_assign_group, unit, slot);
    mmu_slot_id(unit, slot, id, sizeof id);
    actions_log("assign %s %s", g_mmu.groups[s_assign_group].name, id);
    toast_move(unit, slot, s_assign_group);
}

void ui_group_assign_bay_open(int group)
{
    lv_obj_t *sheet;
    mmu_unit_t *u = mmu_this();
    int unit = mmu_this_unit();
    char title[96];
    int i;
    int shown = 0;

    if(group < 0 || group >= (int)g_mmu.group_count) return;
    s_assign_group = group;

    vs_format(VS_GP_ASSIGN_TITLE, title, sizeof title, (vs_arg_t[]){ VS_ARG_T("group", g_mmu.groups[group].name) }, 1);
    sheet = ui_sheet_open(title, UI_SHEET_MAX_H);
    if(!sheet) return;   /* memory is short: no sheet, nothing to fill */

    for(i = 0; i < (int)u->slot_count; i++) {
        mmu_slot_t *s = &u->slots[i];
        char id[MMU_SLOT_ID_LEN];
        char label[96];
        const char *why = NULL;

        if(s->state == MMU_SLOT_EMPTY) continue;
        if(s->info_source == MMU_INFO_UNKNOWN) continue;

        mmu_slot_id(unit, i, id, sizeof id);
        vs_format(VS_GP_ROW, label, sizeof label,
                  (vs_arg_t[]){ VS_ARG_T("spool", ui_kit_spool(unit, i)),
                                VS_ARG_T("material", mmu_slot_display_material(s)) }, 2);

        if(mmu_slot_group(unit, i) == group) why = view_str(VS_GP_ALREADY);

        ui_sheet_row(sheet, UI_ICON_SYNC, label, why, why ? NULL : on_pick_bay,
                    UI_ENC(unit, i), false);
        shown++;
    }

    if(shown == 0) {
        ui_sheet_row(sheet, UI_ICON_FILAMENT, view_str(VS_GP_NONE_AVAIL),
                    view_str(VS_GP_NONE_NOTE), NULL, NULL, false);
    }
}

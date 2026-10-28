/**
 * @file screen_mapping.c
 * Screen 10: print-start mapping, in the design language (ui_kit), scoped to
 * the unit this display is bound to. Each row is a slicer tool with its group's
 * bays as the view's "Spool N" (another unit's bay names its unit). Confirm is
 * dimmed, with the reason on tap, until every tool has a spool ready. Groups ARE the mapping: the slicer's tools (T0..T3) are group names, so
 * this screen just shows what each group currently has and lets this unit
 * fill a group that has nothing ready. See docs/GROUPS.md.
 */
#include "ui/ui_screens.h"

#include "app/actions.h"
#include "model/mmu_model.h"
#include "ui/ui_group_picker.h"
#include "ui/ui_kit.h"
#include "ui/ui_widgets.h"
#include "view/strings.h"
#include "view/view.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAP_PROJ_COUNT 4
#define MAP_COL_X      UI_PX(12)
#define MAP_COL_Y      UI_PX(48)
#define MAP_COL_W      (UI_SCREEN_W - 2 * MAP_COL_X)   /* 456 */
#define MAP_COL_H      UI_PX(228)   /* viewport y 48-276 */
#define MAP_ROW_GAP    UI_PX(6)
#define MAP_ROW_MIN_H  UI_PX(52)
#define MAP_CHIP_H     UI_PX(22)

typedef struct {
    const char *material;
    uint32_t color;
} map_proj_t;

/* The dummy project filaments the slicer would hand over: T0..T3. */
static const map_proj_t s_proj[MAP_PROJ_COUNT] = {
    { "PLA",     0xD23B3B },
    { "PETG",    0xE8E8E8 },
    { "PETG-CF", 0x22262B },
    { "TPU",     0x8A9099 },
};

/* ---------------------------------------------------------------- helpers */

/* The group named "T<p>", the same name the slicer's tool uses; -1 when no
 * such group exists yet. */
static int group_for_tool(int p)
{
    char name[8];
    int g;

    snprintf(name, sizeof name, "T%d", p);
    for(g = 0; g < (int)g_mmu.group_count; g++) {
        if(strcmp(g_mmu.groups[g].name, name) == 0) return g;
    }
    return -1;
}

/* A member counts as "ready to print from" whether it is the one already
 * loaded or merely waiting its turn. */
static bool group_has_ready(int group)
{
    const mmu_group_t *g;
    int m;

    if(group < 0 || group >= (int)g_mmu.group_count) return false;
    g = &g_mmu.groups[group];
    for(m = 0; m < (int)g->count; m++) {
        int u = g->members[m].unit;
        int s = g->members[m].slot;
        const mmu_slot_t *sl;

        if(u < 0 || u >= (int)g_mmu.unit_count) continue;
        if(s < 0 || s >= (int)g_mmu.units[u].slot_count) continue;
        sl = &g_mmu.units[u].slots[s];
        if(sl->state == MMU_SLOT_READY || sl->state == MMU_SLOT_LOADED) return true;
    }
    return false;
}

static void try_open_tool(int p)
{
    int group;

    if(p < 0 || p >= MAP_PROJ_COUNT) return;
    group = group_for_tool(p);
    if(group < 0 || !mmu_group_editable(group)) return;
    ui_group_assign_bay_open(group);
}

/* ---------------------------------------------------------------- actions */

static void on_row_click(lv_event_t *e)
{
    try_open_tool(UI_DEC_A(lv_event_get_user_data(e)));
}

static void on_confirm(lv_event_t *e)
{
    int p;
    int n = 0;

    (void)e;
    for(p = 0; p < MAP_PROJ_COUNT; p++) {
        if(group_has_ready(group_for_tool(p))) n++;
    }
    actions_log("confirm mapping: %d/%d ready", n, MAP_PROJ_COUNT);

    for(p = 0; p < MAP_PROJ_COUNT; p++) {
        int group = group_for_tool(p);

        if(group >= 0)
            actions_log("  T%d -> %s (%s)", p, g_mmu.groups[group].name,
                        group_has_ready(group) ? "ready" : "no spool");   /* lit-ok: the log line */
        else
            actions_log("  T%d -> no group", p);
    }
}

/* ------------------------------------------------------------------- rows */

#define ROW_H UI_PX(60)

static lv_obj_t *build_row(lv_obj_t *col, int p)
{
    const map_proj_t *proj = &s_proj[p];
    int group = group_for_tool(p);
    bool editable = group >= 0 && mmu_group_editable(group);
    lv_obj_t *row = ui_kit_row(col, ROW_H, editable ? on_row_click : NULL, UI_ENC(p, 0));
    bool ready = group_has_ready(group);
    char buf[64];
    int x = UI_PX(24);
    int rx = UI_HOME_INNER_W - UI_PX(24);

    ui_kit_swatch(row, 0, UI_PX(4), UI_PX(14), proj->color, false);
    snprintf(buf, sizeof buf, "T%d \xC2\xB7 %s", p, proj->material);
    ui_home_label(row, buf, UI_HOME_FONT_BODY,
                  ((g_mmu.job_short >> p) & 1u) ? UI_TOK_WARN : UI_TOK_TEXT, UI_PX(24), 0);

    {
        const char *st = view_str(ready ? VS_GROUP_READY : VS_MAP_NO_SPOOL);
        int tw = ui_kit_tag_w(st);

        ui_kit_tag(row, st, ready ? UI_KIT_TONE_ACCENT : UI_KIT_TONE_ERROR, rx - tw, 0);
        rx -= tw + UI_PX(8);
    }

    /* the group's bays: this unit's in the text color, another unit's dimmed
     * with its unit named */
    if(group < 0 || g_mmu.groups[group].count == 0) {
        ui_home_label(row, view_str(group < 0 ? VS_MAP_NO_GROUP : VS_MAP_EMPTY), UI_HOME_FONT_META, UI_TOK_TEXT_FAINT,
                      UI_PX(24), UI_PX(22));
    }
    else {
        const mmu_group_t *g = &g_mmu.groups[group];
        view_tile_t *t = malloc(sizeof *t);
        int m;

        for(m = 0; m < (int)g->count && t; m++) {
            int u = g->members[m].unit;
            int sl = g->members[m].slot;
            bool mine = mmu_is_this_unit(u);

            if(!view_tile(&g_mmu, u, sl, t)) continue;
            if(mine) snprintf(buf, sizeof buf, "%s", t->sublabel);
            else     vs_unit_spool(mmu_unit_name(u), sl, buf, sizeof buf);
            if(x + ui_home_text_w(buf, UI_HOME_FONT_META) > rx) break;
            ui_home_label(row, buf, UI_HOME_FONT_META, mine ? UI_TOK_TEXT : UI_TOK_TEXT_MUTED, x, UI_PX(22));
            x += ui_home_text_w(buf, UI_HOME_FONT_META) + UI_PX(14);
        }
        free(t);
    }
    return row;
}

/* ------------------------------------------------------------------- build */

static lv_obj_t *build(void)
{
    lv_obj_t *scr = ui_kit_screen();
    lv_obj_t *col;
    int first_missing = -1;
    char reason[96];
    int p;

    (void)ui_kit_header(scr, view_str(VS_SCREEN_MAPPING), true);
    col = ui_kit_column(scr, UI_HOME_HEADER_H + UI_PX(2), UI_PX(268) - UI_HOME_HEADER_H - UI_PX(2) - UI_PX(8));
    for(p = 0; p < MAP_PROJ_COUNT; p++) {
        build_row(col, p);
        if(first_missing < 0 && !group_has_ready(group_for_tool(p))) first_missing = p;
    }

    /* Confirm reads dimmed while any tool has no spool ready, and says which. */
    reason[0] = '\0';
    if(first_missing >= 0) vs_format(VS_MAP_MISSING, reason, sizeof reason, (vs_arg_t[]){ VS_ARG_N("n", first_missing) }, 1);
    ui_kit_button_r(scr, view_str(VS_BTN_CONFIRM), UI_KIT_BTN_PRIMARY, UI_HOME_HEADER_PAD, UI_PX(268), UI_HOME_INNER_W,
                    UI_PX(40), reason, on_confirm, NULL);
    return scr;
}

/* ----------------------------------------------------------------- modals */

static int modal_count(void)
{
    return 1;
}

static void open_modal(int index)
{
    int p;

    (void)index;
    for(p = 0; p < MAP_PROJ_COUNT; p++) {
        int group = group_for_tool(p);

        if(group >= 0 && mmu_group_editable(group)) {
            try_open_tool(p);
            return;
        }
    }
}

const ui_screen_ops_t screen_mapping_ops = { build, modal_count, open_modal, NULL };

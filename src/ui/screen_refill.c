/**
 * @file screen_refill.c
 * Screen 8: Groups, scoped to the unit this display is bound to, in the design
 * language (ui_kit).
 *
 * A group is a named, ordered list of bays (e.g. "T0"); infinite spooling
 * falls back through it in order. Only groups that hold at least one bay of
 * THIS unit are shown; a member on another unit is dimmed and named with its
 * unit ("B Spool 2") - it is a backup this unit may fall back to, never
 * something to manage from here. This unit's own bays that are in no group are
 * listed last, and both they and the bays inside a group open the change-group
 * sheet.
 *
 * Every row is what the view says it is: view_group_count() / view_group() for
 * the groups (their names, their members in order, each member's state and the
 * next spare) and view_ungrouped_bays() for the bays in no group. The one gap
 * that is left is that the change-group sheet itself still reads the model.
 * See docs/GROUPS.md.
 */
#include "ui/ui_screens.h"

#include "model/mmu_model.h"
#include "ui/ui_group_picker.h"
#include "ui/ui_icons.h"
#include "ui/ui_kit.h"
#include "ui/ui_widgets.h"
#include "view/strings.h"
#include "view/view.h"

#include <stdio.h>
#include <stdlib.h>

#define ROW_H UI_PX(46)
#define TRAIL_GAP UI_PX(10) /* what a trailing tag leaves before the word beside it */
#define CHEVRON_W UI_PX(18)

/* A bay of this unit, in a group or in none, opens the change-group sheet. */
static void on_change_group(lv_event_t *e)
{
    void *ud = lv_event_get_user_data(e);

    ui_group_picker_open(UI_DEC_A(ud), UI_DEC_B(ud));
}

/* The second line of a member's row: "PLA \xc2\xb7 Bambu". A bay of this unit
 * with no material falls back to its tile's tag, as the model-read rows did. */
static void meta_text(const view_group_member_t *mem, char *out, size_t n)
{
    view_tile_t *t;

    if(mem->material[0]) {
        snprintf(out, n, "%s%s%s", mem->material, mem->brand[0] ? " \xC2\xB7 " : "", mem->brand);
        return;
    }
    out[0] = '\0';
    if(!mem->mine) return;
    t = malloc(sizeof *t);
    if(!t) return;
    if(view_tile(&g_mmu, mmu_this_unit(), mem->bay, t) && t->has_tag)
        snprintf(out, n, "%s", t->tag);
    free(t);
}

/* What the member's state word says: a group with nothing to fall back to
 * says "No spare" rather than "Ready", the word the device has always shown
 * for a ready bay that is its group's only member. */
static const char *state_word(const view_group_member_t *mem, int member_count)
{
    if(mem->state_kind == VIEW_MEMBER_STATE_READY && member_count == 1)
        return view_str(VS_GROUP_NO_SPARE);
    return mem->state;
}

/* The color that state word is drawn in. */
static lv_color_t state_color(const view_group_member_t *mem, int member_count)
{
    switch(mem->state_kind) {
    case VIEW_MEMBER_STATE_LOADED: return UI_TOK_ACCENT;
    case VIEW_MEMBER_STATE_EMPTY:  return UI_TOK_TEXT_FAINT;
    case VIEW_MEMBER_STATE_ERROR:  return UI_TOK_ERROR;
    case VIEW_MEMBER_STATE_READY:  return member_count == 1 ? UI_TOK_TEXT_FAINT : UI_TOK_TEXT;
    default:                       return UI_TOK_TEXT;
    }
}

/* The chevron that marks a row tappable, when the row leaves it room: where a
 * state word or a tag stands, the row keeps the look it has and is only
 * tappable. @p left is how far left the row's own trailing content reaches. */
static void trail_chevron(lv_obj_t *row, int left)
{
    if(left > UI_HOME_INNER_W - UI_PX(24) - TRAIL_GAP)
        ui_home_label(row, UI_ICON_CHEVRON_RIGHT, UI_HOME_FONT_ICON, UI_TOK_TEXT_FAINT,
                      UI_HOME_INNER_W - UI_PX(24) - CHEVRON_W, UI_PX(4));
}

/* One member row: swatch, "Spool N" (dimmed, with its unit, on another unit),
 * material, and the state - joined by a "next on runout" chip for the spare.
 * A bay of this unit is tappable and opens the change-group sheet. */
static void member_row(lv_obj_t *col, const view_group_member_t *mem, int member_count)
{
    const char *state = state_word(mem, member_count);
    lv_color_t color = state_color(mem, member_count);
    char buf[64];
    int unit = mmu_this_unit();
    int rx = UI_HOME_INNER_W - UI_PX(24);
    lv_obj_t *row;

    row = ui_kit_row(col, ROW_H, mem->mine ? on_change_group : NULL,
                     mem->mine ? UI_ENC(unit, mem->bay) : NULL);
    ui_kit_swatch(row, 0, UI_PX(8), UI_PX(14), mem->color, !mem->color_known);
    ui_home_label(row, mem->label, UI_HOME_FONT_BODY,
                  mem->dim ? UI_TOK_TEXT_MUTED : UI_TOK_TEXT, UI_PX(24), 0);
    meta_text(mem, buf, sizeof buf);
    ui_home_label_fit(row, buf, UI_HOME_FONT_META, UI_TOK_TEXT_MUTED, UI_PX(24), UI_PX(18),
                      UI_PX(220));

    if(mem->spare) {
        int cw = ui_kit_tag_w(view_str(VS_GROUP_NEXT));

        ui_kit_tag(row, view_str(VS_GROUP_NEXT), UI_KIT_TONE_ACCENT, rx - cw, UI_PX(4));
        rx -= cw + TRAIL_GAP;
    }
    if(state[0]) {
        rx -= ui_home_text_w(state, UI_HOME_FONT_BODY);
        ui_home_label(row, state, UI_HOME_FONT_BODY, color, rx, UI_PX(4));
    }
    if(mem->mine) trail_chevron(row, rx);
}

/* One group of the view: its name as the section title, its members in order. */
static void build_group_section(lv_obj_t *col, int index)
{
    view_group_t *vg = malloc(sizeof *vg);
    lv_obj_t *l;
    uint8_t m;

    if(!vg) return;
    if(!view_group(&g_mmu, mmu_this_unit(), index, vg)) {
        free(vg);
        return;
    }
    l = ui_kit_section(col, vg->name);
    lv_obj_set_style_text_color(l, UI_TOK_TEXT, 0);
    for(m = 0; m < vg->member_count; m++) member_row(col, &vg->members[m], vg->member_count);
    free(vg);
}

/* This unit's bays in no group: a tappable row each (the change-group sheet). */
static bool build_unassigned(lv_obj_t *col)
{
    view_ungrouped_t *ug = malloc(sizeof *ug);
    int unit = mmu_this_unit();
    bool any = false;
    uint8_t i;

    if(!ug) return false;
    if(!view_ungrouped_bays(&g_mmu, unit, ug)) {
        free(ug);
        return false;
    }
    for(i = 0; i < ug->count; i++) {
        const view_group_member_t *b = &ug->bays[i];
        char buf[64];
        lv_obj_t *row;

        if(!any) ui_kit_section(col, view_str(VS_GROUP_NOT_IN));
        any = true;

        row = ui_kit_row(col, ROW_H, on_change_group, UI_ENC(unit, b->bay));
        ui_kit_swatch(row, 0, UI_PX(8), UI_PX(14), b->color, !b->color_known);
        ui_home_label(row, b->label, UI_HOME_FONT_BODY, UI_TOK_TEXT, UI_PX(24), 0);
        meta_text(b, buf, sizeof buf);
        ui_home_label_fit(row, buf, UI_HOME_FONT_META, UI_TOK_TEXT_MUTED, UI_PX(24), UI_PX(18),
                          UI_PX(260));
        trail_chevron(row, UI_HOME_INNER_W - UI_PX(24));
    }
    free(ug);
    return any;
}

/* ------------------------------------------------------------------- build */

static lv_obj_t *build(void)
{
    lv_obj_t *scr = ui_kit_screen();
    lv_obj_t *col;
    bool any_content = false;
    int count, g;

    (void)ui_kit_header(scr, view_str(VS_SCREEN_GROUPS), true);
    col = ui_kit_column(scr, UI_HOME_HEADER_H + UI_PX(2), UI_HOME_H - UI_HOME_HEADER_H - UI_PX(2));

    count = view_group_count(&g_mmu, mmu_this_unit());
    for(g = 0; g < count; g++) {
        any_content = true;
        build_group_section(col, g);
    }
    any_content = build_unassigned(col) || any_content;

    if(!any_content)
        (void)ui_kit_text_row(col, view_str(VS_GROUP_NO_USE), NULL, NULL, UI_TOK_TEXT, NULL, NULL);
    return scr;
}

/* ------------------------------------------------------------------- modal */

static int modal_count(void)
{
    return 1;                    /* the change-group sheet, for this unit's first real bay */
}

static void open_modal(int index)
{
    mmu_unit_t *u = mmu_this();
    int unit = mmu_this_unit();
    int i;

    (void)index;
    for(i = 0; i < (int)u->slot_count; i++) {
        if(u->slots[i].state != MMU_SLOT_EMPTY && u->slots[i].info_source != MMU_INFO_UNKNOWN) {
            ui_group_picker_open(unit, i);
            return;
        }
    }
}

const ui_screen_ops_t screen_refill_ops = { build, modal_count, open_modal, NULL };

/**
 * @file screen_print.c
 * Screen 7: in-print status, from the point of view of the unit this display
 * is mounted on, in the design language (ui_kit). The hero card carries the
 * bay the print is using as its tile (the view's label, "Spool N", material,
 * brand and remaining percent; another unit's bay says which unit), the
 * toolhead's own message under it (a runout or a fault, with its severity
 * icon) and the step of a load in progress from the view's activity. Pause and
 * load is always there: dimmed, with the reason on tap, when nothing has run out.
 */
#include "ui/ui_screens.h"

#include "app/actions.h"
#include "model/mmu_model.h"
#include "ui/ui_icons.h"
#include "ui/ui_kit.h"
#include "ui/ui_nav.h"
#include "ui/ui_update.h"
#include "view/strings.h"
#include "view/view.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CARD_Y  (UI_HOME_HEADER_H + UI_PX(4))
#define CARD_H  UI_PX(88)
/* The hero's material line: from x 56 to 16 short of the ring (320 on the grid). */
#define MATERIAL_W (UI_HOME_INNER_W - UI_PX(16) - UI_PX(48) - UI_PX(16) - UI_PX(56))

/* The bay the print is using: the printing bay while printing, otherwise
 * whatever is parked in the toolhead. */
static bool active_slot(int *unit, int *slot)
{
    if(g_mmu.printing && g_mmu.print_unit >= 0 && g_mmu.print_slot >= 0) {
        *unit = g_mmu.print_unit;
        *slot = g_mmu.print_slot;
        return true;
    }
    if(g_mmu.loaded && g_mmu.loaded_ext < 0 &&
       g_mmu.loaded_unit >= 0 && g_mmu.loaded_slot >= 0) {
        *unit = g_mmu.loaded_unit;
        *slot = g_mmu.loaded_slot;
        return true;
    }
    return false;
}

static void on_pause_load(lv_event_t *e)
{
    int unit = -1;
    int slot = -1;
    char id[MMU_SLOT_ID_LEN];

    (void)e;
    if(active_slot(&unit, &slot)) {
        mmu_slot_id(unit, slot, id, sizeof id);
        actions_log("pause and load %s", id);
        /* the runout banner is about the affected bay: reloading it clears it */
        if(g_mmu.runout_active) g_mmu.runout_active = false;
        mmu_model_start_load(unit, slot);   /* shows the step list on the next build */
        ui_nav_refresh_async();
    }
    else {
        actions_log("pause and load (no active slot)");
    }
}

/* ------------------------------------------------------- the live update */

/* What the last build() drew, so update() can write to the same objects. Only
 * the shape is kept, never the view structs: their strings are borrowed from
 * the model, so comparing them later could compare a buffer with itself. Every
 * value is compared against the object that holds it instead. */
static struct {
    lv_obj_t *hdr;
    lv_obj_t *alert;          /* the header's slot, NULL when it said nothing */
    lv_obj_t *card;
    lv_obj_t *swatch;
    lv_obj_t *label;
    lv_obj_t *sublabel;
    lv_obj_t *line;           /* material and brand */
    lv_obj_t *grams;          /* NULL when the spool carries no weight */
    lv_obj_t *ring;
    lv_obj_t *pct;            /* NULL when the ring has nothing to write in it */
    lv_obj_t *count;          /* the filament-change line */
    lv_obj_t *standby;        /* NULL when this unit is the one printing */
    lv_obj_t *message;        /* the message card, NULL for a plain tone */
    lv_obj_t *message_icon;
    lv_obj_t *message_text;
    lv_obj_t *button;
    lv_obj_t *button_label;
    int slot;                 /* the bay the card carries */
    bool dim;                 /* the button wears the disabled look */
    bool bound;
} s_shown;

static lv_obj_t *child_at(lv_obj_t *parent, int i)
{
    return (parent && i >= 0 && (uint32_t)i < lv_obj_get_child_count(parent))
               ? lv_obj_get_child(parent, (uint32_t)i) : NULL;
}

/* The one line the card's material row shows: the material, then the brand. */
static void material_line(const view_tile_t *t, char *buf, size_t n)
{
    snprintf(buf, n, "%s%s%s", t->material, t->brand[0] ? " \xC2\xB7 " : "", t->brand);
}

/* The line under the card: the filament changes so far, and the step of a load
 * that is running under them. */
static void change_line(const view_toolhead_t *th, char *buf, size_t n)
{
    const view_activity_t *a = &th->activity;

    if((a->kind == VIEW_ACTIVITY_LOADING || a->kind == VIEW_ACTIVITY_UNLOADING) &&
       a->index >= 0 && a->index < a->step_count)
        vs_format(VS_PRINT_CHANGE_STEP, buf, n,
                  (vs_arg_t[]){ VS_ARG_N("n", (int)g_mmu.filament_changes),
                                VS_ARG_T("step", a->steps[a->index]) }, 2);
    else
        vs_format(VS_PRINT_CHANGE, buf, n,
                  (vs_arg_t[]){ VS_ARG_N("n", (int)g_mmu.filament_changes) }, 1);
}

/* ------------------------------------------------------------------- build */

static void build_hero(lv_obj_t *scr)
{
    int unit = -1;
    int slot = -1;
    bool elsewhere = mmu_printing_elsewhere();
    lv_obj_t *card = ui_kit_card(scr, UI_HOME_HEADER_PAD, CARD_Y, UI_HOME_INNER_W, CARD_H);
    view_tile_t *t = malloc(sizeof *t);
    char buf[96];

    if(!t) return;
    s_shown.card = card;
    if(!active_slot(&unit, &slot) || !view_tile(&g_mmu, unit, slot, t)) {
        ui_home_label(card, view_str(VS_PRINT_NO_FILAMENT), UI_HOME_FONT_SECTION, UI_TOK_TEXT_MUTED,
                      UI_PX(16), UI_PX(32));
        free(t);
        return;                      /* a card that says "no filament" is a rebuild */
    }
    s_shown.slot = slot;
    s_shown.swatch = ui_kit_swatch(card, UI_PX(16), UI_PX(30), UI_PX(28), t->color, t->dim || !t->color_known);
    s_shown.label = ui_home_label(card, t->label, UI_HOME_FONT_LABEL, UI_TOK_TEXT, UI_PX(56), UI_PX(12));
    /* Another unit's bay names its unit: "B Spool 2". */
    if(elsewhere) vs_unit_spool(mmu_unit_name(unit), slot, buf, sizeof buf);
    else          snprintf(buf, sizeof buf, "%s", t->sublabel);
    s_shown.sublabel = ui_home_label(card, buf, UI_HOME_FONT_META, UI_TOK_TEXT_MUTED,
                                     UI_PX(56) + ui_home_text_w(t->label, UI_HOME_FONT_LABEL) + UI_PX(8),
                                     UI_PX(20));

    material_line(t, buf, sizeof buf);
    s_shown.line = ui_home_label_fit(card, buf, UI_HOME_FONT_BODY, UI_TOK_TEXT, UI_PX(56), UI_PX(44), MATERIAL_W);
    if(t->grams_text[0])
        s_shown.grams = ui_home_label(card, t->grams_text, UI_HOME_FONT_META, UI_TOK_TEXT_MUTED,
                                      UI_PX(56), UI_PX(64));

    s_shown.ring = ui_kit_ring(card, UI_HOME_INNER_W - UI_PX(16) - UI_PX(48), UI_PX(20), UI_PX(48),
                               t->pct < 0 ? 0 : t->pct, t->low ? UI_TOK_WARN : UI_TOK_ACCENT, t->pct_text);
    s_shown.pct = child_at(s_shown.ring, 0);
    free(t);
}

/* One line under the hero: the toolhead's own message when it is not just the
 * plain status (a runout, a fault), with its severity icon. */
static int build_message(lv_obj_t *scr, const view_toolhead_t *th, int y)
{
    lv_obj_t *row;

    if(th->message.tone == VIEW_TONE_NEUTRAL || !th->message.text[0]) return y;
    row = ui_kit_card(scr, UI_HOME_HEADER_PAD, y, UI_HOME_INNER_W, UI_PX(40));
    s_shown.message = row;
    s_shown.message_icon = ui_home_label(row,
                                         th->message.tone == VIEW_TONE_ERROR ? UI_HOME_ICON_ERROR
                                                                               : UI_HOME_ICON_INFO,
                                         UI_HOME_FONT_SEV, ui_home_tone_color(th->message.tone),
                                         UI_PX(12), UI_PX(12));
    s_shown.message_text = ui_home_label_fit(row, th->message.text, UI_HOME_FONT_BODY, UI_TOK_TEXT, UI_PX(40),
                                             (UI_PX(40) - lv_font_get_line_height(UI_HOME_FONT_BODY)) / 2,
                                             UI_HOME_INNER_W - UI_PX(40) - UI_PX(12));
    return y + UI_PX(40) + UI_PX(8);
}

static lv_obj_t *build(void)
{
    lv_obj_t *scr = ui_kit_screen();
    view_toolhead_t th;
    int unit = mmu_this_unit();
    int thi = (unit >= 0 && unit < (int)g_mmu.unit_count) ? g_mmu.units[unit].toolhead : -1;
    int y;

    memset(&th, 0, sizeof th);
    memset(&s_shown, 0, sizeof s_shown);
    if(thi >= 0) view_toolhead(&g_mmu, thi, &th);
    s_shown.hdr = ui_kit_header(scr, view_str(VS_SCREEN_PRINT), true);
    /* the header's back target, its title, and its alert slot when it has one */
    s_shown.alert = child_at(s_shown.hdr, 2);

    if(!g_mmu.printing) {
        /* the empty state is two lines that never change: a rebuild, always */
        ui_home_label(scr, UI_ICON_PRINTER_3D_NOZZLE, UI_HOME_FONT_ICON, UI_TOK_TEXT_FAINT,
                      UI_HOME_HEADER_PAD, UI_HOME_HEADER_H + UI_PX(20));
        ui_home_label(scr, view_str(VS_PRINT_NOT_PRINTING), UI_HOME_FONT_SECTION, UI_TOK_TEXT_MUTED,
                      UI_HOME_HEADER_PAD + UI_PX(28), UI_HOME_HEADER_H + UI_PX(20));
        return scr;
    }

    build_hero(scr);
    y = CARD_Y + CARD_H + UI_PX(8);

    /* the filament-change count, and the step of a load in progress */
    {
        char buf[128];

        change_line(&th, buf, sizeof buf);
        s_shown.count = ui_home_label_fit(scr, buf, UI_HOME_FONT_BODY, UI_TOK_TEXT_MUTED,
                                          UI_HOME_HEADER_PAD, y, UI_HOME_INNER_W);
        y += UI_PX(22);
    }
    if(!mmu_runout_concerns_this() && mmu_printing_elsewhere()) {
        s_shown.standby = ui_home_label(scr, view_str(VS_PRINT_STANDBY), UI_HOME_FONT_BODY,
                                        UI_TOK_TEXT_MUTED, UI_HOME_HEADER_PAD, y);
        y += UI_PX(22);
    }
    (void)build_message(scr, &th, y);

    s_shown.dim = !mmu_runout_concerns_this();
    s_shown.button = ui_kit_button_r(scr, view_str(VS_PRINT_PAUSE_LOAD), UI_KIT_BTN_PRIMARY,
                                     UI_HOME_HEADER_PAD, UI_SCREEN_H - UI_PX(12) - UI_PX(40),
                                     UI_HOME_INNER_W, UI_PX(40),
                                     s_shown.dim ? view_str(VS_PRINT_NO_RUNOUT) : NULL,
                                     on_pause_load, NULL);
    s_shown.button_label = child_at(s_shown.button, s_shown.dim ? 1 : 0);

    s_shown.bound = s_shown.card != NULL && s_shown.swatch != NULL && s_shown.label != NULL
                    && s_shown.sublabel != NULL && s_shown.line != NULL
                    && s_shown.ring != NULL && s_shown.count != NULL
                    && s_shown.button != NULL && s_shown.button_label != NULL;
    return scr;
}

/** This screen's own toolhead, or false when the display is bound to nothing. */
static bool this_toolhead(view_toolhead_t *th)
{
    int unit = mmu_this_unit();
    int thi = (unit >= 0 && unit < (int)g_mmu.unit_count) ? g_mmu.units[unit].toolhead : -1;

    memset(th, 0, sizeof *th);
    if(thi < 0) return false;
    return view_toolhead(&g_mmu, thi, th);
}

/* Bring the card, the lines and the button up to date without rebuilding the
 * screen. False - having written nothing - when the shape would differ from a
 * fresh build: the print starting or stopping, another bay in the hero, a
 * message card or a standby line that would appear or go, a button that would
 * lose the disabled look it wears. */
static bool update(void)
{
    view_tile_t t;
    view_toolhead_t th;
    char buf[128];
    int unit = -1;
    int slot = -1;
    int count = 0;
    bool clear = false;
    int tone = VIEW_TONE_INFO;
    bool dim, card;

    if(!s_shown.bound) return false;

    /* --- what would have to be built again, decided before anything is written */

    if(!g_mmu.printing) return false;
    if(!active_slot(&unit, &slot) || slot != s_shown.slot) return false;
    if(!view_tile(&g_mmu, unit, slot, &t)) return false;
    if((t.grams_text[0] != 0) != (s_shown.grams != NULL)) return false;
    if(!this_toolhead(&th)) return false;

    card = (th.message.tone != VIEW_TONE_NEUTRAL && th.message.text[0]);
    dim = !mmu_runout_concerns_this();
    if(card != (s_shown.message != NULL)) return false;
    if(dim != s_shown.dim) return false;
    if((!mmu_runout_concerns_this() && mmu_printing_elsewhere()) != (s_shown.standby != NULL))
        return false;
    ui_update_alert_totals(&count, &tone, &clear);
    if(ui_update_alert_slot_objects(count, clear) !=
       (s_shown.alert ? (int)lv_obj_get_child_count(s_shown.alert) : 0)) return false;

    /* --- from here on the screen on screen is only being written to */

    if(count > 0 && s_shown.alert) ui_update_alert_mark(s_shown.alert, tone, count);

    /* the hero: the bay the print is drawing from, as far as it can change */
    ui_update_swatch(s_shown.swatch, t.color, t.dim || !t.color_known);
    if(strcmp(lv_label_get_text(s_shown.label), t.label) != 0) {
        /* the sublabel sits after the tool label, so it moves with it */
        lv_obj_set_x(s_shown.sublabel,
                     UI_PX(56) + ui_home_text_w(t.label, UI_HOME_FONT_LABEL) + UI_PX(8));
        ui_update_text(s_shown.label, t.label);
    }
    if(mmu_printing_elsewhere()) vs_unit_spool(mmu_unit_name(unit), slot, buf, sizeof buf);
    else                         snprintf(buf, sizeof buf, "%s", t.sublabel);
    ui_update_text(s_shown.sublabel, buf);

    material_line(&t, buf, sizeof buf);
    ui_update_text_fit(s_shown.line, buf, UI_HOME_FONT_BODY, MATERIAL_W);
    if(s_shown.grams) ui_update_text(s_shown.grams, t.grams_text);

    ui_update_arc(s_shown.ring, t.pct < 0 ? 0 : t.pct, t.low ? UI_TOK_WARN : UI_TOK_ACCENT);
    if(s_shown.pct && strcmp(lv_label_get_text(s_shown.pct), t.pct_text) != 0) {
        lv_obj_set_x(s_shown.pct, (UI_PX(48) - ui_home_text_w(t.pct_text, UI_HOME_FONT_META)) / 2);
        ui_update_text(s_shown.pct, t.pct_text);
    }

    /* the lines under the card */
    change_line(&th, buf, sizeof buf);
    ui_update_text_fit(s_shown.count, buf, UI_HOME_FONT_BODY, UI_HOME_INNER_W);
    if(card) {
        ui_update_text(s_shown.message_icon,
                       th.message.tone == VIEW_TONE_ERROR ? UI_HOME_ICON_ERROR : UI_HOME_ICON_INFO);
        ui_update_text_color(s_shown.message_icon, ui_home_tone_color(th.message.tone));
        ui_update_text_fit(s_shown.message_text, th.message.text, UI_HOME_FONT_BODY,
                           UI_HOME_INNER_W - UI_PX(40) - UI_PX(12));
    }
    return true;
}

static int modal_count(void)
{
    return 0;
}

static void open_modal(int index)
{
    (void)index;
}

const ui_screen_ops_t screen_print_ops = { build, modal_count, open_modal, update };

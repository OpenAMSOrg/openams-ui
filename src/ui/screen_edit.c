/**
 * @file screen_edit.c
 * Screen 5: the slot edit dialog for THIS unit, in the design language
 * (ui_kit). The bay is named as its tile in the view ("T0", "Spool 3"); the
 * fields (vendor, material, color, PA and the two weights) are cards with a
 * stepper, dimmed (the one disabled look, with the reason on tap) when the
 * info was read from a tag (demo only), the bay is printing, or - against a
 * live host - Spoolman cannot take the edit.
 *
 * Save is one line, `spool edit <slot> material=.. color=.. vendor=.. remaining=..
 * initial=.. pa=..` (backend/actions_map.h): the same line the web panel's form
 * fills in, whose RPC is server.openams_spoolman.edit. The per-tap `edit <slot>
 * <field> <value>` lines are a trace, not host actions. A refusal is toasted
 * here with the host's reason; the host's own words later arrive as a toast
 * too (app_live.c).
 *
 * The slot edited is g_mmu.selected_slot when the selected unit is the one
 * this display is bound to, otherwise slot 0 of the bound unit - never another
 * unit's slot. A tag-read spool shows its fields disabled rather than hiding
 * the screen, and the slot in the running print is blocked with an
 * explanation instead.
 *
 * In its save-and-load mode (screen_edit_save_and_load(), the Load row of a
 * bay nobody has identified) the same screen does one job that finishes: the
 * material starts unset and has to be chosen, the line is the same edit line
 * with ` then load`, and the load is sent only once the host has answered.
 *
 * See docs/VISUAL_SPEC_480.md, "Edit" and "Colour sheet".
 *
 * modal_count() == 2: 0 = the color palette sheet, 1 = the reset confirm.
 */

#include "ui/ui_screens.h"

#include "app/actions.h"
#include "backend/actions_map.h"
#include "lib/cjson/cJSON.h"
#include "model/mmu_model.h"
#include "ui/ui_display.h"
#include "ui/ui_editenv_helpers.h"
#include "ui/ui_icons.h"
#include "ui/ui_kit.h"
#include "ui/ui_nav.h"
#include "ui/ui_print_common.h"
#include "ui/ui_sheet.h"
#include "ui/ui_theme.h"
#include "view/strings.h"
#include "view/view.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ------------------------------------------------------------------ tables */

/* ------------------------------------------------------------------ tables */

/* The materials and vendors are the model's (mmu_material_name(),
 * mmu_edit_vendor_name()): Spoolman's own vendor list, and the same indices
 * the web panel's form sends, so the one `spool edit` line means the same to
 * the host from either. A bay's own material or vendor, when it is on neither
 * list, is one more choice at the end (s_other_*). */

/* --------------------------------------------------------------- layout */

/* Six cards in three rows: vendor | material, PA | color, remaining | initial. */
#define ED_HERO_Y      (UI_HOME_HEADER_H + UI_PX(4))
#define ED_HERO_H      UI_PX(44)
#define ED_ROW0_Y      UI_PX(98)
#define ED_ROW1_Y      UI_PX(154)
#define ED_ROW2_Y      UI_PX(210)
#define ED_CARD_H      UI_PX(50)
#define ED_COL0_X      UI_HOME_HEADER_PAD
#define ED_COL_W       ((UI_HOME_INNER_W - UI_PX(8)) / 2)   /* 224: two columns, 8 apart */
#define ED_COL1_X      (UI_HOME_HEADER_PAD + UI_HOME_INNER_W - ED_COL_W)
#define ED_NOTE_Y      UI_PX(261)
#define ED_BTN_Y       UI_PX(276)

/* ------------------------------------------------------------------ state */

enum { F_VENDOR, F_MATERIAL, F_PA, F_REMAINING, F_INITIAL, F_COUNT };

/* How far one tap moves a weight and PA, and one repeat while held. */
#define ED_GRAMS_TAP     10
#define ED_GRAMS_HOLD    50
#define ED_PA_TAP        1
#define ED_PA_HOLD       10
#define ED_GRAMS_MAX     10000
#define ED_PA_MAX        2000

/* The wait for the host's answer to a save-and-load edit: as long as the load
 * screen gives a host to report a load, and the tick is the load screen's. */
#define ED_ANSWER_MS     10000

/* Pending edit values.
 *
 * The material is an index of the model's own fixed table, which nothing can
 * move. The vendor is a NAME, because the vendor list is not ours: Spoolman
 * answers `vendor list` some time after this screen opens (and again after
 * every edit that may have added one), and an index taken when the screen was
 * built names a different vendor once the answer arrives - so editing the
 * material would silently change the vendor. The action line carries an index,
 * so the name is resolved against the list at Save time instead (vendor_index()).
 *
 * color is 0xRRGGBB, or -1 while nobody told us. The slicer owns the nozzle
 * temperature range - this screen never shows or edits it. */
typedef struct {
    char vendor[MMU_VENDOR_LEN];
    int material;
    int32_t color;
    int remaining_g;
    int initial_g;
    int pa_x1000;
} ed_values_t;

static ed_values_t s_e;

static ed_values_t s_saved;

static int s_unit;
static int s_slot;
static bool s_blocked;
static bool s_readonly;     /* info read from a tag (demo only): fields shown, disabled */
static const char *s_host_why; /* against a live host: why it cannot take an edit, else NULL */
static int s_material_n;
static bool s_create;       /* the host has no record of this spool: Save adds it */
static bool s_editable;
static bool s_dirty;
static bool s_prepared;

/* Save and load: the Load row of a bay nobody has identified comes here
 * instead of loading a spool the printer would have to guess at.
 * screen_edit_save_and_load() arms the next entry, and build() takes the arm
 * over: every way into this screen is a fresh build from another screen, so
 * this entry's mode is the one the setter asked for and the next Edit is the
 * editor it always was.
 *
 * The load is what the host's answer releases: the edit goes out on the tap,
 * and the load only once Spoolman has taken it (the same two fields
 * app_live.c's poll_edit_result() reads), so nothing is ever heated on a spool
 * the host refused to store. */
static bool s_armed;
static bool s_load_after;
static bool s_waiting;      /* the edit is out and its answer is not in yet */
static uint32_t s_since;    /* when the wait began */
static uint8_t s_edit_seq;  /* the answer this screen is waiting for */
static lv_timer_t *s_wait_timer;

static char s_other_material[MMU_TEXT_LEN];

static lv_obj_t *s_val[F_COUNT];
static lv_obj_t *s_swatch;
static lv_obj_t *s_color_sw;
static lv_obj_t *s_color_val;
static lv_obj_t *s_pill;
static lv_obj_t *s_scr;
static lv_obj_t *s_btns;       /* Reset and Save, rebuilt when the dirty flag changes */

static void build_buttons(void);

static void open_color(void);
static void open_reset(void);
static void edit_refresh(void);

/* ---------------------------------------------------------------- helpers */

static int wrap_idx(int v, int n)
{
    if(n <= 0) return 0;
    while(v < 0) v += n;
    while(v >= n) v -= n;
    return v;
}

static void slot_id_str(char *out, size_t out_len)
{
    mmu_slot_id(s_unit, s_slot, out, out_len);
}

/* Spoolman keeps a vendor as typed, so "generic" and "Generic" are one choice;
 * the model's own mmu_edit_defaults() compares the same way. */
static bool vendor_same(const char *a, const char *b)
{
    for(; *a && *b; a++, b++) {
        char x = *a, y = *b;

        if(x >= 'A' && x <= 'Z') x = (char)(x - 'A' + 'a');
        if(y >= 'A' && y <= 'Z') y = (char)(y - 'A' + 'a');
        if(x != y) return false;
    }
    return *a == *b;
}

/** The index of vendor @p name in the model's list right now, or -1. */
static int vendor_find(const char *name)
{
    int n = mmu_edit_vendor_count(&g_mmu);
    int i;

    for(i = 0; i < n; i++) {
        if(vendor_same(name, mmu_edit_vendor_name(&g_mmu, i))) return i;
    }
    return -1;
}

/** How many choices the vendor stepper walks: the model's list, plus the bay's
 *  own vendor when it is on neither (the one past the list, as the material
 *  stepper has for a bay's own material). */
static int vendor_count(void)
{
    int n = mmu_edit_vendor_count(&g_mmu);

    if(g_mmu.unit_count > 0 && s_unit >= 0 && s_unit < (int)g_mmu.unit_count &&
       vendor_find(g_mmu.units[s_unit].slots[s_slot].brand) < 0)
        n++;
    return n;
}

/** The @p i'th choice of the vendor stepper: the model's list, then the bay's
 *  own vendor. */
static const char *vendor_at(int i)
{
    int n = mmu_edit_vendor_count(&g_mmu);

    if(i < 0 || i >= vendor_count()) return "";
    return (i < n) ? mmu_edit_vendor_name(&g_mmu, i) : g_mmu.units[s_unit].slots[s_slot].brand;
}

static const char *vendor_name(void)
{
    return s_e.vendor;
}

/** The vendor's index for the action line, against the list as it stands now:
 *  its place on the list, or the one past it (the bay's own vendor) when the
 *  host's list has changed under this screen. */
static int vendor_index(void)
{
    int i = vendor_find(s_e.vendor);

    return (i >= 0) ? i : mmu_edit_vendor_count(&g_mmu);
}

static const char *material_name(void)
{
    /* Below 0 is the one choice that is not a material: an unidentified bay's
     * "Choose a material", which no save may carry. */
    if(s_e.material < 0) return view_str(VS_MATERIAL_CHOOSE);
    return (s_e.material >= MMU_MATERIAL_COUNT) ? s_other_material : mmu_material_name(s_e.material);
}

/** The color's name, or "Not set" while nobody told us. */
static const char *color_text(void)
{
    return (s_e.color < 0) ? view_str(VS_EDIT_COLOR_UNSET) : print_color_name((uint32_t)s_e.color);
}

/** The bay's own name as the view gives it ("Spool 3"). */
static void spool_name(char *out, size_t n)
{
    view_tile_t *t = malloc(sizeof *t);

    out[0] = '\0';
    if(t && view_tile(&g_mmu, s_unit, s_slot, t)) snprintf(out, n, "%s", t->sublabel);
    free(t);
}

/** A label that wraps to @p w px and grows to fit its text. */
static lv_obj_t *wrap_label(lv_obj_t *parent, const char *text, int x, int y, int w,
                            const lv_font_t *font, lv_color_t color)
{
    lv_obj_t *l = lv_label_create(parent);

    lv_obj_remove_style_all(l);
    lv_obj_set_style_text_font(l, font, 0);
    lv_obj_set_style_text_color(l, color, 0);
    lv_obj_set_width(l, w);
    lv_label_set_long_mode(l, LV_LABEL_LONG_MODE_WRAP);
    lv_obj_set_pos(l, x, y);
    lv_label_set_text(l, text);
    return l;
}

/** Resolve the bound unit's edit slot and load the saved and pending values. */
static bool edit_prepare(void)
{
    const mmu_slot_t *sp;
    mmu_edit_defaults_t d;
    int tu, sl;

    if(g_mmu.unit_count == 0) return false;

    tu = mmu_this_unit();
    s_unit = tu;

    sl = 0;
    if(g_mmu.selected_unit == tu && g_mmu.selected_slot >= 0 &&
       g_mmu.selected_slot < (int)g_mmu.units[tu].slot_count)
        sl = g_mmu.selected_slot;
    s_slot = sl;

    sp = &g_mmu.units[tu].slots[sl];
    s_blocked = g_mmu.printing && g_mmu.print_unit == tu && g_mmu.print_slot == sl;
    /* a spool read from its tag is Spoolman's to edit, so only the demo (which
     * has no Spoolman) keeps it read-only */
    s_readonly = !mmu_model_is_live() && (sp->info_source == MMU_INFO_TAG);
    /* against a live host: the view's own check of the ring's "Edit spool" (no
     * edit endpoint, Spoolman offline, an empty bay) */
    s_host_why = mmu_model_is_live() ? view_tile_edit_reason(&g_mmu, tu, sl) : NULL;
    s_editable = !s_blocked && !s_readonly && !s_host_why;
    s_create = mmu_model_is_live() && sp->state != MMU_SLOT_EMPTY && sp->spool_id < 0;

    mmu_edit_defaults(&g_mmu, tu, sl, &d);
    /* the vendor by name: what the form starts from is either a name on the
     * list or the bay's own, and only the name survives the host's list
     * arriving later */
    mmu_copy_text(s_e.vendor, sizeof s_e.vendor,
                  (d.vendor < mmu_edit_vendor_count(&g_mmu)) ? mmu_edit_vendor_name(&g_mmu, d.vendor)
                                                           : sp->brand);
    s_e.material = d.material;
    s_e.color = d.color;
    s_e.remaining_g = d.remaining_g;
    s_e.initial_g = d.initial_g;
    s_e.pa_x1000 = d.pa_x1000;

    snprintf(s_other_material, sizeof s_other_material, "%s", sp->material);
    s_material_n = MMU_MATERIAL_COUNT + (d.material >= MMU_MATERIAL_COUNT ? 1 : 0);

    /* Save and load: nobody has said what this bay holds, so the material
     * starts on the one choice that is not one and the button waits for an
     * answer (the same first option and the same rule as the panel's form). */
    if(s_load_after) {
        s_e.material = -1;
        s_material_n++;
    }

    s_saved = s_e;
    s_dirty = false;
    s_prepared = true;
    return true;
}

/** Reset is live only while the slot holds something to clear. */
static bool reset_enabled(void)
{
    const mmu_slot_t *sp;

    if(!s_editable || g_mmu.unit_count == 0) return false;
    sp = &g_mmu.units[s_unit].slots[s_slot];
    return sp->state != MMU_SLOT_EMPTY && sp->info_source != MMU_INFO_UNKNOWN;
}

/** Recompute the dirty flag, then refresh the buttons and the pill in place
 * (the screen is never rebuilt). */
static void update_dirty(void)
{
    bool d = false;

    if(!vendor_same(s_e.vendor, s_saved.vendor)) d = true;
    if(s_e.material != s_saved.material) d = true;
    if(s_e.color != s_saved.color) d = true;
    if(s_e.pa_x1000 != s_saved.pa_x1000) d = true;
    if(s_e.remaining_g != s_saved.remaining_g) d = true;
    if(s_e.initial_g != s_saved.initial_g) d = true;
    s_dirty = d;

    build_buttons();
    if(s_pill != NULL) lv_obj_set_hidden(s_pill, !s_dirty);
}

static void val_color(lv_obj_t *l, bool changed)
{
    if(l != NULL) lv_obj_set_style_text_color(l, changed ? UI_TOK_ACCENT : UI_TOK_TEXT, 0);
}

/* A color swatch: the color, or an empty outline while nobody told us. */
static void paint_swatch(lv_obj_t *sw)
{
    if(sw == NULL) return;
    if(s_e.color < 0) {
        lv_obj_set_style_bg_opa(sw, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(sw, UI_PX(1), 0);
        lv_obj_set_style_border_color(sw, UI_TOK_TEXT_FAINT, 0);
        lv_obj_set_style_border_opa(sw, LV_OPA_COVER, 0);
    }
    else {
        lv_obj_set_style_bg_color(sw, lv_color_hex((uint32_t)s_e.color), 0);
        lv_obj_set_style_bg_opa(sw, LV_OPA_COVER, 0);
        /* the hairline ui_kit_swatch() gives a color, so black or white reads on a card */
        lv_obj_set_style_border_color(sw, UI_TOK_LINE, 0);
        lv_obj_set_style_border_opa(sw, 60, 0);
    }
}

static void edit_refresh(void)
{
    char buf[24];

    if(s_val[F_VENDOR] != NULL) {
        lv_label_set_text(s_val[F_VENDOR], vendor_name());
        val_color(s_val[F_VENDOR], !vendor_same(s_e.vendor, s_saved.vendor));

        lv_label_set_text(s_val[F_MATERIAL], material_name());
        val_color(s_val[F_MATERIAL], s_e.material != s_saved.material);

        snprintf(buf, sizeof buf, "%d.%03d", s_e.pa_x1000 / 1000, s_e.pa_x1000 % 1000);
        lv_label_set_text(s_val[F_PA], buf);
        val_color(s_val[F_PA], s_e.pa_x1000 != s_saved.pa_x1000);

        vs_format(VS_GRAMS, buf, sizeof buf, (vs_arg_t[]){ VS_ARG_N("g", s_e.remaining_g) }, 1);
        lv_label_set_text(s_val[F_REMAINING], buf);
        val_color(s_val[F_REMAINING], s_e.remaining_g != s_saved.remaining_g);

        vs_format(VS_GRAMS, buf, sizeof buf, (vs_arg_t[]){ VS_ARG_N("g", s_e.initial_g) }, 1);
        lv_label_set_text(s_val[F_INITIAL], buf);
        val_color(s_val[F_INITIAL], s_e.initial_g != s_saved.initial_g);
    }

    paint_swatch(s_swatch);
    paint_swatch(s_color_sw);
    if(s_color_val != NULL) {
        lv_label_set_text(s_color_val, color_text());
        val_color(s_color_val, s_e.color != s_saved.color);
    }

    update_dirty();
}

/* -------------------------------------------------------------- callbacks */

static int clamp_int(int v, int lo, int hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}

static void step_field(int field, int dir, bool hold)
{
    char id[MMU_SLOT_ID_LEN];

    if(!s_editable) return;
    slot_id_str(id, sizeof id);

    switch(field) {
    case F_VENDOR:
        /* one step along the list as it stands now, by name: the host may have
         * replaced it since this screen opened, and the choice the user sees is
         * the one that has to be kept */
        mmu_copy_text(s_e.vendor, sizeof s_e.vendor,
                      vendor_at(wrap_idx(vendor_index() + dir, vendor_count())));
        actions_log("edit %s vendor %s", id, vendor_name());
        break;
    case F_MATERIAL:
        /* save and load counts the unset choice (-1) as the list's first stop */
        s_e.material = wrap_idx(s_e.material + (s_load_after ? 1 : 0) + dir, s_material_n)
                       - (s_load_after ? 1 : 0);
        actions_log("edit %s material %s", id, material_name());
        break;
    case F_PA:
        s_e.pa_x1000 = clamp_int(s_e.pa_x1000 + dir * (hold ? ED_PA_HOLD : ED_PA_TAP), 0, ED_PA_MAX);
        actions_log("edit %s pa %d.%03d", id, s_e.pa_x1000 / 1000, s_e.pa_x1000 % 1000);
        break;
    case F_REMAINING:
        s_e.remaining_g = clamp_int(s_e.remaining_g + dir * (hold ? ED_GRAMS_HOLD : ED_GRAMS_TAP), 0,
                                    ED_GRAMS_MAX);
        actions_log("edit %s remaining %d", id, s_e.remaining_g);
        break;
    case F_INITIAL:
        s_e.initial_g = clamp_int(s_e.initial_g + dir * (hold ? ED_GRAMS_HOLD : ED_GRAMS_TAP), 1,
                                  ED_GRAMS_MAX);
        actions_log("edit %s initial %d", id, s_e.initial_g);
        break;
    default:
        break;
    }
    edit_refresh();
}

/* The field is in the user data (make_stepper() hands it over). A repeat while
 * held takes the bigger step. */
static void on_minus_cb(lv_event_t *e)
{
    step_field((int)(intptr_t)lv_event_get_user_data(e), -1,
               lv_event_get_code(e) == LV_EVENT_LONG_PRESSED_REPEAT);
}

static void on_plus_cb(lv_event_t *e)
{
    step_field((int)(intptr_t)lv_event_get_user_data(e), +1,
               lv_event_get_code(e) == LV_EVENT_LONG_PRESSED_REPEAT);
}

static void on_color_click(lv_event_t *e)
{
    (void)e;
    if(s_editable) open_color();
}

static void on_pick(lv_event_t *e)
{
    int idx = (int)(intptr_t)lv_event_get_user_data(e);
    char id[MMU_SLOT_ID_LEN], col[10];

    if(s_editable && idx >= 0 && idx < print_palette_count()) {
        s_e.color = (int32_t)print_palette_color(idx);
        ui_editenv_color_text((uint32_t)s_e.color, col, sizeof col);
        slot_id_str(id, sizeof id);
        actions_log("edit %s color %s", id, col);
        edit_refresh();
    }
    ui_sheet_close(NULL);
}

static void on_reset_prompt(lv_event_t *e)
{
    (void)e;
    open_reset();
}

static void on_reset_confirm(lv_event_t *e)
{
    mmu_slot_t *sp;
    char id[MMU_SLOT_ID_LEN];

    (void)e;
    if(!s_editable || g_mmu.unit_count == 0) {
        ui_sheet_close(NULL);
        return;
    }

    sp = &g_mmu.units[s_unit].slots[s_slot];
    sp->brand[0] = '\0';
    sp->material[0] = '\0';
    sp->color = 0;
    sp->color_known = false;
    sp->remaining_pct = -1;
    sp->nozzle_min_c = -1;
    sp->nozzle_max_c = -1;
    sp->k_value_x1000 = -1;
    mmu_group_unassign(s_unit, s_slot);
    sp->info_source = MMU_INFO_UNKNOWN;

    slot_id_str(id, sizeof id);
    actions_log("reset %s", id);

    ui_sheet_close(NULL);
    {
        char tbuf[64];

        char sp[40];

        spool_name(sp, sizeof sp);
        vs_format(VS_EDIT_WAS_RESET, tbuf, sizeof tbuf, (vs_arg_t[]){ VS_ARG_T("spool", sp) }, 1);
        ui_sheet_toast(tbuf);
    }
    ui_nav_goto_async(UI_SCREEN_HOME);
}

/** The load the editor was asked for instead of sending: the bay's own line and
 * the bay's own screen, exactly as the sheet's Load row sends them. */
static void send_load(void)
{
    char id[MMU_SLOT_ID_LEN];

    slot_id_str(id, sizeof id);
    actions_log("load %s", id);
    mmu_model_start_load(s_unit, s_slot);
    ui_sheet_close(NULL);
    ui_nav_goto_async(UI_SCREEN_LOAD);
}

static void wait_end(void)
{
    s_waiting = false;
    if(s_wait_timer) {
        lv_timer_delete(s_wait_timer);
        s_wait_timer = NULL;
    }
}

/** The host's answer is what releases the load, so this screen waits for it
 * rather than sending one and hoping. The tick is the load screen's own: a
 * model change carries no clock, so a deadline needs one (app_live.c reads the
 * same two fields the moment they move). */
static void on_answer(lv_timer_t *t)
{
    char text[320];

    (void)t;
    if(!s_scr) return;

    if(g_mmu.edit_seq == s_edit_seq) {
        if(lv_tick_elaps(s_since) < ED_ANSWER_MS) return;
        /* Nothing came at all: say so and stay, with the values kept, rather
         * than loading a spool that was never stored. */
        wait_end();
        ui_sheet_toast(view_str(VS_LABEL_NO_RESPONSE));
        return;
    }

    s_edit_seq = g_mmu.edit_seq;
    if(g_mmu.edit_refusal[0]) {
        /* The host's words, after the display's lead, as app_live.c says them
         * when no screen is waiting on this answer itself. */
        snprintf(text, sizeof text, "%s %s", view_str(VS_LABEL_EDIT_FAILED), g_mmu.edit_refusal);
        wait_end();
        ui_sheet_toast(text);
        return;
    }

    wait_end();
    send_load();
}

static void wait_for_answer(void)
{
    s_waiting = true;
    s_edit_seq = g_mmu.edit_seq;
    s_since = lv_tick_get();
    s_wait_timer = lv_timer_create(on_answer, 400, NULL);
}

static void on_save(lv_event_t *e)
{
    mmu_slot_t *sp;
    char id[MMU_SLOT_ID_LEN], line[VIEW_ACTION_LINE_LEN], method[64];
    const char *why = "";
    cJSON *params = NULL;

    (void)e;
    if(!s_editable || s_waiting || g_mmu.unit_count == 0) return;

    slot_id_str(id, sizeof id);
    /* the action line, not text (lit-ok) */
    snprintf(line, sizeof line, "spool edit %s material=%d color=%d vendor=%d remaining=%d initial=%d pa=%d.%03d", /* lit-ok */
             id, s_e.material, (int)s_e.color, vendor_index(), s_e.remaining_g, s_e.initial_g,
             s_e.pa_x1000 / 1000, s_e.pa_x1000 % 1000);
    /* The one job that finishes: what this save is for is a load, so the line
     * says so, and the mapper hands the load back to be sent once Spoolman
     * has taken the spool. */
    if(s_load_after) {
        size_t n = strlen(line);

        snprintf(line + n, sizeof line - n, " then load"); /* lit-ok */
    }

    if(mmu_model_is_live()) {
        /* One call to the host for the whole save. Say a refusal here, with its
         * reason, instead of leaving it in the log: nothing was sent, and the
         * screen stays so the values are not lost. */
        if(actions_rpc_why(line, &g_mmu, method, sizeof method, &params, &why) < 0) {
            ui_sheet_toast(why);
            return;
        }
        cJSON_Delete(params);
        if(s_load_after) {
            /* Out and waiting: the load follows the answer, not the tap. */
            actions_log("%s", line);
            wait_for_answer();
            return;
        }
    }
    else {
        /* The demo has no host to answer: the model takes the edit itself. */
        sp = &g_mmu.units[s_unit].slots[s_slot];
        if(s_e.color >= 0) {
            sp->color = (uint32_t)s_e.color;
            sp->color_known = true;
        }
        mmu_copy_text(sp->brand, sizeof sp->brand, vendor_name());
        mmu_copy_text(sp->material, sizeof sp->material, material_name());
        sp->k_value_x1000 = (int16_t)s_e.pa_x1000;
        sp->remaining_g = (int16_t)s_e.remaining_g;
        sp->remaining_pct = (int16_t)clamp_int((s_e.remaining_g * 100 + s_e.initial_g / 2) / s_e.initial_g, 0, 100);
        sp->info_source = MMU_INFO_MANUAL;
    }

    actions_log("%s", line);

    if(s_load_after) {
        /* The demo has no host to answer, so nothing is being waited for: the
         * spool is in the model and the load goes out with it. */
        send_load();
        return;
    }

    {
        char tbuf[64];

        char sp[40];

        spool_name(sp, sizeof sp);
        vs_format(VS_EDIT_SAVED, tbuf, sizeof tbuf, (vs_arg_t[]){ VS_ARG_T("spool", sp) }, 1);
        ui_sheet_toast(tbuf);
    }
    ui_nav_goto_async(UI_SCREEN_HOME);
}

/* ----------------------------------------------------------------- modals */

/** modal 0: the color palette. 18 colors, six columns by three rows, each a
 * 44 px square swatch; the current color's border is the accent, and a
 * near-white or near-black entry keeps a faint hairline so it does not melt
 * into the sheet. */
static void open_color(void)
{
    lv_obj_t *sheet, *body;
    const int cols = 6;
    const int cell = UI_PX(44);
    const int gap = UI_PX(12);
    const int gx = (UI_SCREEN_W - (cols * cell + (cols - 1) * gap)) / 2; /* 78: the grid centered */
    const int gy = UI_PX(8); /* local to the body, which already starts at sheet y 48 */
    int i;

    if(!s_prepared && !edit_prepare()) return;

    sheet = ui_sheet_open(view_str(VS_EDIT_COLOR), UI_PX(240));
    if(sheet == NULL) return;
    body = ui_sheet_body(sheet);
    if(body == NULL) return;

    for(i = 0; i < print_palette_count(); i++) {
        int col = i % cols;
        int row = i / cols;
        uint32_t rgb = print_palette_color(i);
        bool current = (s_e.color >= 0 && rgb == (uint32_t)s_e.color);
        bool edge = ui_filament_needs_outline(rgb);
        lv_obj_t *sw = lv_obj_create(body);

        lv_obj_remove_style_all(sw);
        lv_obj_set_pos(sw, gx + col * (cell + gap), gy + row * (cell + gap));
        lv_obj_set_size(sw, cell, cell);
        lv_obj_set_style_radius(sw, UI_PX(2), 0);
        lv_obj_set_style_bg_color(sw, lv_color_hex(rgb), 0);
        lv_obj_set_style_bg_opa(sw, LV_OPA_COVER, 0);
        lv_obj_set_scrollable(sw, false);
        /* The body is a flex column (every other sheet's rows); this grid
         * places its own children explicitly, so each swatch opts out. */
        lv_obj_set_floating(sw, true);

        if(current) {
            lv_obj_set_style_border_width(sw, UI_PX(2), 0);
            lv_obj_set_style_border_color(sw, UI_COL_ACCENT, 0);
            lv_obj_set_style_border_opa(sw, LV_OPA_COVER, 0);
        }
        else if(edge) {
            lv_obj_set_style_border_width(sw, UI_PX(1), 0);
            lv_obj_set_style_border_color(sw, UI_COL_TEXT_FAINT, 0);
            lv_obj_set_style_border_opa(sw, LV_OPA_COVER, 0);
        }

        if(s_editable) {
            lv_obj_set_clickable(sw, true);
            lv_obj_add_event_cb(sw, on_pick, LV_EVENT_CLICKED, (void *)(intptr_t)i);
        }
    }

    if(!s_editable) {
        lv_obj_t *note = wrap_label(body, view_str(VS_EDIT_COLOR_RO),
                                    UI_PAD_XXL, gy + 3 * cell + 2 * gap + UI_PX(8),
                                    UI_SCREEN_W - 2 * UI_PAD_XXL, UI_FONT_S, UI_COL_TEXT_DIM);
        lv_obj_set_floating(note, true);
    }
    /* The spec fixes this sheet at h = 240 regardless of content, unlike a
     * row-built sheet that fits itself - no ui_sheet_fit() here. */
}

/** modal 1: the reset confirmation. */
static void open_reset(void)
{
    lv_obj_t *sheet, *body;
    char id[40], title[64];

    if(!s_prepared && !edit_prepare()) return;

    spool_name(id, sizeof id);
    vs_format(VS_EDIT_RESET_TITLE, title, sizeof title, (vs_arg_t[]){ VS_ARG_T("spool", id) }, 1);

    sheet = ui_sheet_open(title, UI_PX(160));
    if(sheet == NULL) return;
    body = ui_sheet_body(sheet);
    if(body == NULL) return;

    /* The body is a flex column, which drops every child at its left edge, so
     * the 16 px margin the sheet's title keeps has to be padding on the label. */
    {
        lv_obj_t *note = wrap_label(body,
                                    view_str(VS_EDIT_RESET_NOTE),
                                    0, 0, UI_SCREEN_W, UI_FONT_S, UI_COL_TEXT_DIM);

        lv_obj_set_style_pad_hor(note, UI_PAD_XXL, 0);
    }

    ui_sheet_button(sheet, view_str(VS_BTN_RESET), on_reset_confirm, NULL, true);
}

/* ------------------------------------------------------------- the screen */

static const char *why_locked(void)
{
    if(s_blocked)  return view_str(VS_EDIT_LOCKED_PRINT);
    if(s_readonly) return view_str(VS_EDIT_LOCKED_TAG);
    if(s_host_why) return s_host_why;
    return NULL;
}

static void on_locked(lv_event_t *e)
{
    (void)e;
    ui_sheet_toast(why_locked() ? why_locked() : "");
}

/** A field card: a caption and its stepper. Locked cards wear the hatch, and
 * a tap says why. */
static lv_obj_t *field_card(lv_obj_t *scr, int x, int y, const char *caption)
{
    lv_obj_t *c = ui_kit_card(scr, x, y, ED_COL_W, ED_CARD_H);

    if(!s_editable) {
        ui_home_hatch(c, ED_COL_W, ED_CARD_H);
        lv_obj_set_clickable(c, true);
        lv_obj_add_event_cb(c, on_locked, LV_EVENT_CLICKED, NULL);
    }
    ui_home_label(c, caption, UI_HOME_FONT_META, UI_TOK_TEXT_MUTED, UI_PX(12), 0);
    return c;
}

static lv_obj_t *make_stepper(lv_obj_t *scr, int x, int y, const char *caption, const char *value,
                              int field)
{
    lv_obj_t *c = field_card(scr, x, y, caption);
    lv_obj_t *v = ui_kit_stepper_hold(c, UI_PX(12), UI_PX(15), ED_COL_W - UI_PX(24), value,
                                      s_editable ? on_minus_cb : NULL, s_editable ? on_plus_cb : NULL,
                                      (void *)(intptr_t)field);

    if(!s_editable) lv_obj_set_style_text_color(v, UI_TOK_TEXT_FAINT, 0);
    return v;
}

/** The color field: a tappable card with a swatch, the color's name and a
 * chevron; it opens the same palette sheet as the hero swatch. */
static void build_color_field(lv_obj_t *scr, int y)
{
    lv_obj_t *c = field_card(scr, ED_COL1_X, y, view_str(VS_EDIT_COLOR));

    if(s_editable) {
        lv_obj_set_clickable(c, true);
        lv_obj_add_event_cb(c, on_color_click, LV_EVENT_CLICKED, NULL);
    }
    s_color_sw = ui_kit_swatch(c, UI_PX(12), UI_PX(22), UI_PX(20), s_e.color < 0 ? 0 : (uint32_t)s_e.color, s_e.color < 0);
    s_color_val = ui_home_label(c, color_text(), UI_HOME_FONT_BODY,
                                s_editable ? UI_TOK_TEXT : UI_TOK_TEXT_FAINT, UI_PX(40),
                                UI_PX(15) + (UI_PX(34) - lv_font_get_line_height(UI_HOME_FONT_BODY)) / 2);
    lv_obj_set_width(s_color_val, UI_PX(140));
    if(s_editable)
        ui_home_label(c, UI_ICON_CHEVRON_RIGHT, UI_HOME_FONT_ICON, UI_TOK_TEXT_FAINT,
                      ED_COL_W - UI_PX(12) - UI_PX(18), UI_PX(15) + (UI_PX(34) - UI_PX(18)) / 2);
}

/** Reset and Save along the bottom; each dimmed, with the reason on tap, when
 * it cannot act. Rebuilt whenever a field changes. */
static void build_buttons(void)
{
    int half = (UI_HOME_INNER_W - UI_PX(8)) / 2;
    const char *reset_why = NULL;
    const char *save_why = NULL;

    if(s_scr == NULL) return;
    if(s_btns) lv_obj_delete(s_btns);
    s_btns = lv_obj_create(s_scr);
    lv_obj_remove_style_all(s_btns);
    lv_obj_set_pos(s_btns, 0, ED_BTN_Y);
    lv_obj_set_size(s_btns, UI_HOME_W, UI_PX(40));
    lv_obj_set_scrollable(s_btns, false);

    if(!s_editable)          reset_why = save_why = why_locked();
    else {
        if(!reset_enabled()) reset_why = view_str(VS_EDIT_NOTHING_CLEAR);
        if(s_load_after && s_e.material < 0) save_why = view_str(VS_MATERIAL_CHOOSE);
        else if(!s_dirty && !s_create)      save_why = view_str(VS_EDIT_NO_CHANGES);
    }
    ui_kit_button_r(s_btns, view_str(VS_BTN_RESET), UI_KIT_BTN_NORMAL, UI_HOME_HEADER_PAD, 0, half, UI_PX(40),
                    reset_why, on_reset_prompt, NULL);
    ui_kit_button_r(s_btns, s_load_after ? view_str(VS_SAVE_AND_LOAD) : view_str(VS_ACTION_SAVE_EDIT),
                    UI_KIT_BTN_PRIMARY, UI_HOME_HEADER_PAD + half + UI_PX(8), 0, half, UI_PX(40),
                    save_why, on_save, NULL);
}

/** Hero row: the filament swatch (the color button), the tile's "T0 · PLA"
 * title with its "Spool N" and vendor beneath, the NFC mark for a tag-read
 * spool and the "Edited" chip that wakes on the first change. */
static void build_hero(lv_obj_t *scr)
{
    view_tile_t *t = malloc(sizeof *t);
    lv_obj_t *card = ui_kit_card(scr, UI_HOME_HEADER_PAD, ED_HERO_Y, UI_HOME_INNER_W, ED_HERO_H);
    char buf[80]; /* sublabel and vendor are each up to 31 whole, plus " · " */
    int tw;

    s_swatch = ui_kit_swatch(card, UI_PX(12), UI_PX(4), UI_PX(36), s_e.color < 0 ? 0 : (uint32_t)s_e.color, s_e.color < 0);
    if(s_editable) {
        lv_obj_set_clickable(s_swatch, true);
        lv_obj_add_event_cb(s_swatch, on_color_click, LV_EVENT_CLICKED, NULL);
    }
    if(!t || !view_tile(&g_mmu, s_unit, s_slot, t)) {
        free(t);
        return;
    }

    snprintf(buf, sizeof buf, "%s \xC2\xB7 %s", t->label, material_name());
    ui_home_label_fit(card, buf, UI_HOME_FONT_TITLE, UI_TOK_TEXT, UI_PX(60), 0, UI_PX(280));
    tw = ui_home_text_w(buf, UI_HOME_FONT_TITLE);
    snprintf(buf, sizeof buf, "%s \xC2\xB7 %s", t->sublabel, vendor_name());
    ui_home_label_fit(card, buf, UI_HOME_FONT_META, UI_TOK_TEXT_MUTED, UI_PX(60), UI_PX(25), UI_PX(300));
    if(s_readonly)
        ui_home_label(card, UI_ICON_NFC_VARIANT, UI_HOME_FONT_ICON, UI_TOK_TEXT_MUTED,
                      UI_HOME_INNER_W - UI_PX(12) - UI_PX(18), UI_PX(4));
    s_pill = ui_kit_tag(card, view_str(VS_EDIT_EDITED), UI_KIT_TONE_ACCENT, UI_PX(60) + tw + UI_PX(10), UI_PX(4));
    lv_obj_set_hidden(s_pill, true);
    free(t);
}

static void on_screen_delete(lv_event_t *e)
{
    if(lv_event_get_target_obj(e) == s_scr) {
        wait_end();
        s_scr = NULL;
        s_btns = NULL;
    }
}

static lv_obj_t *build(void)
{
    lv_obj_t *scr = ui_kit_screen();
    char title[96];
    char nb[16];
    view_tile_t *t;
    int i;

    for(i = 0; i < F_COUNT; i++) s_val[i] = NULL;
    s_swatch = NULL;
    s_color_sw = NULL;
    s_color_val = NULL;
    s_pill = NULL;
    s_btns = NULL;
    s_scr = scr;
    lv_obj_add_event_cb(scr, on_screen_delete, LV_EVENT_DELETE, NULL);
    s_prepared = false;
    /* the mode the entry was armed with, taken over from the arm */
    s_load_after = s_armed;
    s_armed = false;

    if(!edit_prepare()) {
        (void)ui_kit_header(scr, view_str(VS_SCREEN_EDIT_NONE), true);
        ui_home_label(scr, view_str(VS_EDIT_NONE), UI_HOME_FONT_SECTION, UI_TOK_TEXT_MUTED,
                      UI_HOME_HEADER_PAD, ED_ROW0_Y);
        return scr;
    }

    t = malloc(sizeof *t);
    snprintf(title, sizeof title, "%s", view_str(VS_SCREEN_EDIT));
    if(t && view_tile(&g_mmu, s_unit, s_slot, t))
        vs_format(VS_SCREEN_EDIT_SPOOL, title, sizeof title, (vs_arg_t[]){ VS_ARG_T("spool", t->sublabel) }, 1);
    free(t);
    (void)ui_kit_header(scr, title, true);

    build_hero(scr);
    s_val[F_VENDOR] = make_stepper(scr, ED_COL0_X, ED_ROW0_Y, view_str(VS_EDIT_VENDOR), vendor_name(), F_VENDOR);
    s_val[F_MATERIAL] = make_stepper(scr, ED_COL1_X, ED_ROW0_Y, view_str(VS_EDIT_MATERIAL), material_name(), F_MATERIAL);
    snprintf(nb, sizeof nb, "%d.%03d", s_e.pa_x1000 / 1000, s_e.pa_x1000 % 1000);
    s_val[F_PA] = make_stepper(scr, ED_COL0_X, ED_ROW1_Y, view_str(VS_EDIT_PA), nb, F_PA);
    build_color_field(scr, ED_ROW1_Y);
    s_val[F_REMAINING] = make_stepper(scr, ED_COL0_X, ED_ROW2_Y, view_str(VS_FIELD_REMAINING_LABEL), "", F_REMAINING);
    s_val[F_INITIAL] = make_stepper(scr, ED_COL1_X, ED_ROW2_Y, view_str(VS_FIELD_INITIAL_LABEL), "", F_INITIAL);

    if(s_readonly)
        ui_home_label(scr, view_str(VS_EDIT_FROM_TAG), UI_HOME_FONT_META, UI_TOK_TEXT_MUTED, UI_HOME_HEADER_PAD,
                      ED_NOTE_Y);
    edit_refresh();
    return scr;
}

static int modal_count(void)
{
    return 2;   /* 0 = color palette, 1 = reset confirmation */
}

static void open_modal(int index)
{
    if(index == 0) open_color();
    else if(index == 1) open_reset();
}

const ui_screen_ops_t screen_edit_ops = { build, modal_count, open_modal, NULL };

/** Build the next entry of this screen in its save-and-load mode: the material
 *  starts unset, the button says "Save and load", and the load is sent only
 *  once the host has taken the spool. Call before ui_nav_goto_async()
 *  (UI_SCREEN_EDIT), which is what builds the screen that takes it over. */
void screen_edit_save_and_load(void)
{
    s_armed = true;
}

/**
 * @file test_screen_home_sheet.c
 * The device's slot sheet for a bay whose tag the host attributes (a reader
 * that several bays share): the note the core gives it, "It is this slot" for a
 * candidate and for a conflict, and a Re-read row dimmed by the same predicate
 * the web panel's own Re-read is enabled by, whatever the bay's `tg` says.
 *
 * Real LVGL with the dummy SDL driver, as test_screen_update.c: the demo binds
 * the display to unit A (A1 loaded, A2 ready, A3 empty, A4 unidentified), the
 * model is made live and given the host's reader, and the home screen's own
 * modal walk opens A2's sheet (its first "normal" bay).
 */

#include "app/platform.h"
#include "lvgl.h"

#include "model/mmu_model.h"
#include "ui/ui_language.h"
#include "ui/ui_nav.h"
#include "ui/ui_screens.h"
#include "ui/ui_theme.h"
#include "view/strings.h"
#include "view/view.h"

#include <stdbool.h>
#include <stdio.h>
#include <string.h>

/* Lengths on the 480x320 grid; a build that draws the grid at another scale
 * defines UI_PX (ui_theme.h) and the sheet's are checked at that scale. */
#ifndef UI_PX
#define UI_PX(v) (v)
#endif

/* --------------------------------------------------------------- the hooks */

/** The sheet's rows send nothing here, but actions_log() is compiled in. */
void platform_action(const char *line)
{
    (void)line;
}

platform_link_t platform_link_status(char *detail, size_t n)
{
    if(detail && n) detail[0] = '\0';
    return PLATFORM_LINK_READY;
}

void platform_touch_calibrate(void)
{
}

/* ----------------------------------------------------------------- harness */

static int s_failures;

static void check(bool ok, const char *what)
{
    printf("%s %s\n", ok ? "ok  " : "FAIL", what);
    if(!ok) s_failures++;
}

/** The label under @p root whose text is @p text, depth first, or NULL. */
static lv_obj_t *find_label(lv_obj_t *root, const char *text)
{
    uint32_t i, n;

    if(lv_obj_check_type(root, &lv_label_class) && strcmp(lv_label_get_text(root), text) == 0)
        return root;
    n = lv_obj_get_child_count(root);
    for(i = 0; i < n; i++) {
        lv_obj_t *hit = find_label(lv_obj_get_child(root, (int32_t)i), text);

        if(hit) return hit;
    }
    return NULL;
}

/** The sheet row labelled @p label: present, and whether it can be tapped (a
 * dimmed row is not clickable and carries its reason). */
static bool row_enabled(const char *label, bool *present)
{
    lv_obj_t *l = find_label(lv_layer_top(), label);

    *present = l != NULL;
    return l && lv_obj_is_clickable(lv_obj_get_parent(l));
}

/* -------------------------------------------------------------- the model */

#define A2 1

static mmu_unit_t *the_unit(void)
{
    return &g_mmu.units[mmu_this_unit()];
}

static mmu_slot_t *the_bay(void)
{
    return &the_unit()->slots[A2];
}

/** Unit A as a live host reports a unit whose readers its bays share. */
static void setup(void)
{
    mmu_unit_t *u;

    mmu_model_set_scenario("idle");
    mmu_model_set_live(true);
    ui_theme_init();
    ui_nav_init();
    u = the_unit();
    u->rfid = true;
    u->host_idx = 1;
    u->act_known = true;
    u->acts |= MMU_UNIT_ACT_RFID_SCAN;
    the_bay()->info_source = MMU_INFO_MANUAL;       /* a manual link: no tag read */
    the_bay()->tag_read = MMU_TAG_READ_NONE;
    the_bay()->tag_reason = MMU_TAG_REASON_OTHER;
    the_bay()->tag_candidate_count = 0;
}

/** A2's sheet, from the home screen's own modal walk (its first "normal" bay). */
static void open_sheet(void)
{
    ui_nav_goto(UI_SCREEN_HOME);
    screen_home_ops.open_modal(0);
    lv_obj_update_layout(lv_layer_top());
}

/* ------------------------------------------------------------------- tests */

/* A conflict: the amber note, wrapped on the sheet's width, and both ways out. */
static void test_conflict(void)
{
    view_tag_read_t r;
    lv_obj_t *note;
    bool present;
    char what[160];
    int lang;

    for(lang = 0; lang < view_lang_count(); lang++) {
        const char *code = view_lang_code(lang);
        int32_t line_h;

        setup();
        check(ui_set_language(code), "the language is built in");
        the_bay()->tag_read = MMU_TAG_READ_CONFLICT;
        the_bay()->tag_reason = MMU_TAG_REASON_CHANGED_IN_PLACE;
        open_sheet();

        view_tile_tag_read(&g_mmu, mmu_this_unit(), A2, &r);
        note = find_label(lv_layer_top(), r.text);
        snprintf(what, sizeof what, "%s: the conflict's note is on the sheet", code);
        check(note != NULL, what);
        if(!note) continue;
        line_h = lv_font_get_line_height(lv_obj_get_style_text_font(note, 0));
        snprintf(what, sizeof what, "%s: it wraps on the sheet's inset, never past it (x %d, w %d)",
                 code, (int)lv_obj_get_x(note), (int)lv_obj_get_width(note));
        check(lv_label_get_long_mode(note) == LV_LABEL_LONG_MODE_WRAP &&
              lv_obj_get_x(note) == UI_PX(16) &&
              lv_obj_get_width(note) == UI_SCREEN_W - 2 * UI_PX(16), what);
        snprintf(what, sizeof what, "%s: whole, in at most three lines (%d px, lines of %d)",
                 code, (int)lv_obj_get_height(note), (int)line_h);
        check(lv_obj_get_height(note) >= line_h && lv_obj_get_height(note) <= 3 * line_h, what);

        snprintf(what, sizeof what, "%s: \"It is this slot\" is offered", code);
        check(row_enabled(view_str(VS_ACTION_TAG_ASSERT), &present) && present, what);
        snprintf(what, sizeof what, "%s: and Re-read", code);
        check(row_enabled(view_str(VS_HS_REREAD), &present) && present, what);
        ui_nav_modal_close_all();
    }
    check(ui_set_language("en"), "back to English");
}

/* A candidate: its tag, one row the height of its line, and both rows. */
static void test_candidate(void)
{
    lv_obj_t *mat;
    bool present;
    char what[96];

    setup();
    the_bay()->tag_read = MMU_TAG_READ_CANDIDATE;
    the_bay()->tag_candidate_count = 1;
    the_bay()->tag_candidates[0].color = 0xD23B3B;
    snprintf(the_bay()->tag_candidates[0].material, MMU_TAG_MATERIAL_LEN, "PETG-CF");
    open_sheet();

    mat = find_label(lv_layer_top(), "PETG-CF");
    check(mat != NULL, "candidate: its material is on the sheet");
    if(mat) {
        int32_t line_h = lv_font_get_line_height(lv_obj_get_style_text_font(mat, 0));

        snprintf(what, sizeof what, "candidate: its row holds its line (%d px for %d)",
                 (int)lv_obj_get_height(lv_obj_get_parent(mat)), (int)line_h);
        check(lv_obj_get_height(lv_obj_get_parent(mat)) >= line_h, what);
    }
    check(row_enabled(view_str(VS_ACTION_TAG_ASSERT), &present) && present,
          "candidate: \"It is this slot\" is offered");
    check(row_enabled(view_str(VS_HS_REREAD), &present) && present, "candidate: and Re-read");
    ui_nav_modal_close_all();
}

/* No `tg`: the host found no tag after a turn and says nothing more. The spool
 * can be turned by hand and read again, as the web panel offers. */
static void test_no_tag_after_a_turn(void)
{
    bool present;

    setup();
    open_sheet();
    check(row_enabled(view_str(VS_HS_REREAD), &present) && present,
          "no `tg`: Re-read can be tapped");
    check(find_label(lv_layer_top(), view_str(VS_HS_NO_TAG)) == NULL,
          "  and is not dimmed as \"No tag on this spool\"");
    row_enabled(view_str(VS_ACTION_TAG_ASSERT), &present);
    check(!present, "  no \"It is this slot\" without a tag to name");
    ui_nav_modal_close_all();

    /* a unit without a reader still dims it */
    setup();
    the_unit()->rfid = false;
    open_sheet();
    check(!row_enabled(view_str(VS_HS_REREAD), &present) && present,
          "no reader: Re-read is dimmed");
    check(find_label(lv_layer_top(), view_str(VS_HS_NO_TAG)) != NULL, "  as \"No tag on this spool\"");
    ui_nav_modal_close_all();
}

int main(void)
{
    lv_init();
    {
        lv_display_t *disp = lv_sdl_window_create(UI_SCREEN_W, UI_SCREEN_H);

        (void)disp;
        lv_sdl_mouse_create();
    }

    mmu_model_init();

    test_conflict();
    test_candidate();
    test_no_tag_after_a_turn();

    printf("%s: %d failure(s)\n", s_failures ? "FAILED" : "PASSED", s_failures);
    return s_failures ? 1 : 0;
}

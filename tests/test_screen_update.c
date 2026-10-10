/**
 * @file test_screen_update.c
 * The screens' in-place update, held to the one rule that makes it safe: after
 * update() answers true, what is on screen has to be the very same pixels a
 * fresh build() of the same model would draw (src/ui/ui_screens.h).
 *
 * So each case builds the screen, changes the model the way the host does
 * mid-print, asks for an update, and compares two snapshots of the screen: the
 * one updated in place, and the one a fresh build draws from the same model.
 * A change that cannot be made in place - another number of bays, a row or a
 * tag that would appear - must answer false instead, which is the caller's cue
 * to rebuild as before.
 *
 * Setup is test_screen_edit.c's: real LVGL, the dummy SDL driver, the theme and
 * the nav, a scenario, one ui_nav_goto. Only the platform hooks differ - the
 * screens under test here never send an action.
 */

#include "lvgl.h"

#include "model/mmu_model.h"
#include "ui/ui_display.h"
#include "ui/ui_home.h"
#include "ui/ui_nav.h"
#include "ui/ui_screens.h"
#include "ui/ui_theme.h"
#include "view/strings.h"

#include <stdbool.h>
#include <stdio.h>
#include <string.h>

/* --------------------------------------------------------------- the hooks */

/** The screens here never send an action, but actions_log() is compiled in. */
void platform_action(const char *line)
{
    (void)line;
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

/* ------------------------------------------------------------- snapshots */

/* lv_snapshot_take() would draw its 480x320 RGB565 buffer (300 KB) out of
 * LVGL's own 96 KB pool, which cannot hold it; the simulator hands
 * lv_snapshot_take_to_draw_buf() statically allocated buffers for the same
 * reason, and this does too. Two of them: the screen updated in place, and the
 * one a fresh build draws. */
static uint8_t s_pixels_a[UI_SCREEN_W * UI_SCREEN_H * 2];
static uint8_t s_pixels_b[UI_SCREEN_W * UI_SCREEN_H * 2];
static lv_draw_buf_t s_buf_a;
static lv_draw_buf_t s_buf_b;

static bool snapshot_into(lv_obj_t *obj, uint8_t *pixels, lv_draw_buf_t *buf)
{
    uint32_t stride = lv_draw_buf_width_to_stride(UI_SCREEN_W, LV_COLOR_FORMAT_RGB565);

    lv_draw_buf_init(buf, UI_SCREEN_W, UI_SCREEN_H, LV_COLOR_FORMAT_RGB565, stride,
                     pixels, (uint32_t)sizeof s_pixels_a);
    return lv_snapshot_take_to_draw_buf(obj, LV_COLOR_FORMAT_RGB565, buf) == LV_RESULT_OK;
}

/** The box the two images first differ in, as "x0,y0-x1,y1", or "-" when they
 * are the same image. */
static const char *difference_box(void)
{
    static char box[32];
    int x0 = UI_SCREEN_W, y0 = UI_SCREEN_H, x1 = -1, y1 = -1;
    int i;

    for(i = 0; i < UI_SCREEN_W * UI_SCREEN_H; i++) {
        int x = i % UI_SCREEN_W;
        int y = i / UI_SCREEN_W;

        if(s_pixels_a[i * 2] != s_pixels_b[i * 2] || s_pixels_a[i * 2 + 1] != s_pixels_b[i * 2 + 1]) {
            if(x < x0) x0 = x;
            if(y < y0) y0 = y;
            if(x > x1) x1 = x;
            if(y > y1) y1 = y;
        }
    }
    if(x1 < 0) return "-";
    snprintf(box, sizeof box, "%d,%d-%d,%d", x0, y0, x1, y1);
    return box;
}

/* -------------------------------------------------------------- the model */

static mmu_unit_t *the_unit(void)
{
    return &g_mmu.units[mmu_this_unit()];
}

static mmu_slot_t *the_bay(int i)
{
    return &the_unit()->slots[i];
}

static mmu_toolhead_t *the_lane(void)
{
    return &g_mmu.toolheads[the_unit()->toolhead];
}

/** The screen as it stands, against a fresh build of the same model. */
static void expect_identical(ui_screen_id_t id, const char *what)
{
    char line[160];

    if(!ui_nav_update()) {
        check(false, what);
        return;
    }
    check(true, what);

    if(!snapshot_into(lv_screen_active(), s_pixels_a, &s_buf_a)) {
        check(false, "  the updated screen can be snapshotted");
        return;
    }

    ui_nav_goto(id);
    if(!snapshot_into(lv_screen_active(), s_pixels_b, &s_buf_b)) {
        check(false, "  a fresh build can be snapshotted");
        return;
    }

    snprintf(line, sizeof line, "  %s is what a fresh build draws (differs in %s)",
             ui_nav_screen_name(id), difference_box());
    check(memcmp(s_pixels_a, s_pixels_b, sizeof s_pixels_a) == 0, line);
}

/** A change the screen cannot take in place: update() must say so, and the
 * caller rebuilds exactly as before. */
static void expect_rebuild(ui_screen_id_t id, void (*setup)(void), void (*change)(void),
                           const char *what)
{
    char line[160];

    setup();
    ui_nav_goto(id);
    change();
    check(!ui_nav_update(), what);
    snprintf(line, sizeof line, "  and %s still builds after the rebuild", ui_nav_screen_name(id));
    check(ui_nav_goto(id), line);
}

/** A change the screen takes in place: the model moves, update() answers true,
 * and the screen has to land on the same pixels a fresh build would. */
static void expect_in_place(ui_screen_id_t id, void (*setup)(void), void (*change)(void),
                            const char *what)
{
    setup();
    ui_nav_goto(id);
    change();
    expect_identical(id, what);
}

/* ------------------------------------------------------------- home: in place */

/* The demo binds the display to unit A: A1 loaded and in tool T0, A2 in T1,
 * A3 empty, A4 unidentified. */
static void home_setup(void)
{
    mmu_model_set_scenario("idle");
    mmu_model_set_live(true);
    ui_theme_init();
    ui_nav_init();
}

static void pct_drops(void)
{
    the_bay(0)->remaining_pct = 37;         /* the spool is being drawn from */
}

static void pct_runs_low(void)
{
    the_bay(0)->remaining_pct = 8;          /* under 15: the ring turns amber */
}

static void spool_is_edited(void)
{
    snprintf(the_bay(1)->material, MMU_TEXT_LEN, "ABS-GF");
    snprintf(the_bay(1)->brand, MMU_TEXT_LEN, "Prusament");
    the_bay(1)->remaining_g = 320;
    the_bay(1)->initial_g = 1000;
}

static void spool_changes_color(void)
{
    the_bay(1)->color = 0x1E90FF;
    the_bay(1)->color_known = true;
}

static void loaded_spool_goes_dark(void)
{
    /* the loaded bay's filament turns black: the path, frame and chip gain
     * their light outline, in place (UNIFIED_UI 4g) */
    the_bay(0)->color = 0x000000;
    the_bay(0)->color_known = true;
}


static void room_moves(void)
{
    the_unit()->humidity_pct = 44;
    the_unit()->temp_c = 27;
}

static void one_more_alert(void)
{
    mmu_model_push_alert("A2: filament info read from tag", MMU_SEV_INFO, mmu_this_unit());
}

static void group_is_renamed(void)
{
    /* T1 is the only group with A2 alone in it */
    snprintf(g_mmu.groups[1].name, sizeof g_mmu.groups[1].name, "T7");
}

static void loaded_material_changes(void)
{
    /* the strip's words beside the extruder read "T0 · <unit>:1 · <material>" */
    snprintf(the_bay(0)->material, MMU_TEXT_LEN, "PETG-CF");
}

/* -------------------------------------------------------- home: must rebuild */

static void bays_go_away(void)
{
    the_unit()->slot_count = 2;
}

static void unit_goes_offline(void)
{
    the_unit()->connected = false;
}

static void bay_is_emptied(void)
{
    the_bay(1)->state = MMU_SLOT_EMPTY;
}

static void bay_is_unloaded(void)
{
    the_bay(0)->state = MMU_SLOT_READY;     /* the tile loses its accent ring */
}

static void reading_goes_away(void)
{
    the_unit()->has_humidity_pct = false;
    the_unit()->has_temp = false;
}

static void test_home_updates_in_place(void)
{
    expect_in_place(UI_SCREEN_HOME, home_setup, pct_drops,
                    "home: the ring follows the spool down");
    expect_in_place(UI_SCREEN_HOME, home_setup, pct_runs_low,
                    "home: a low spool turns the ring amber");
    expect_in_place(UI_SCREEN_HOME, home_setup, spool_is_edited,
                    "home: an edited spool changes name and meta");
    expect_in_place(UI_SCREEN_HOME, home_setup, spool_changes_color,
                    "home: another filament color, gradient and ink");
    expect_in_place(UI_SCREEN_HOME, home_setup, loaded_spool_goes_dark,
                    "home: a dark loaded filament outlines the path, the frame and the chip");
    expect_in_place(UI_SCREEN_HOME, home_setup, room_moves,
                    "home: the environment reading moves");
    expect_in_place(UI_SCREEN_HOME, home_setup, group_is_renamed,
                    "home: the group's tool label moves the spare badge");
    expect_in_place(UI_SCREEN_HOME, home_setup, loaded_material_changes,
                    "home: the path's words follow the loaded spool's material");
}

static void test_home_needs_rebuild(void)
{
    expect_rebuild(UI_SCREEN_HOME, home_setup, one_more_alert,
                   "home: an unread alert takes the header's check away");
    expect_rebuild(UI_SCREEN_HOME, home_setup, bays_go_away,
                   "home: fewer bays is a rebuild");
    expect_rebuild(UI_SCREEN_HOME, home_setup, unit_goes_offline,
                   "home: an offline unit is a rebuild");
    expect_rebuild(UI_SCREEN_HOME, home_setup, bay_is_emptied,
                   "home: a tile that gains its hatch and tag is a rebuild");
    expect_rebuild(UI_SCREEN_HOME, home_setup, bay_is_unloaded,
                   "home: a tile that loses its accent ring is a rebuild");
    expect_rebuild(UI_SCREEN_HOME, home_setup, reading_goes_away,
                   "home: a pill that disappears is a rebuild");
}

/* -------------------------------------------------- home: the path's bay */

/* The strip under the tiles (screen_home.c's 222..320 band): the path from
 * the loaded tile down to the extruder. */
#define STRIP_Y 222

static bool strip_matches(void)
{
    size_t row = (size_t)UI_SCREEN_W * 2;

    return memcmp(s_pixels_a + STRIP_Y * row, s_pixels_b + STRIP_Y * row,
                  (size_t)(UI_SCREEN_H - STRIP_Y) * row) == 0;
}

/* A4 joins T0, the group of the loaded A1, so both tiles carry T0. The path
 * must still run from A1, the bay the lane holds, exactly as it did before A4
 * joined: it once ran from the last tile labeled with the loaded tool. */
static void test_home_path_runs_from_the_loaded_bay(void)
{
    mmu_group_t *t0;

    home_setup();
    ui_nav_goto(UI_SCREEN_HOME);
    check(snapshot_into(lv_screen_active(), s_pixels_a, &s_buf_a), "home: snapshot with A1 alone in T0");

    home_setup();
    t0 = &g_mmu.groups[0];
    t0->members[t0->count].unit = (int8_t)mmu_this_unit();
    t0->members[t0->count].slot = 3;
    t0->count++;
    ui_nav_goto(UI_SCREEN_HOME);
    check(snapshot_into(lv_screen_active(), s_pixels_b, &s_buf_b), "home: snapshot with A4 in T0 too");

    check(strip_matches(), "home: the path runs from the loaded bay, not the group's last tile");
}

/* The outline of a dark filament is really drawn: a black loaded filament puts
 * faint-text pixels (the outline's color) in the strip that a mid color does
 * not. */
static int faint_pixels_in_strip(void)
{
    /* UI_TOK_TEXT_FAINT 0x898F99 as RGB565, little endian in the buffer */
    uint16_t want = (uint16_t)(((0x89 >> 3) << 11) | ((0x8F >> 2) << 5) | (0x99 >> 3));
    int n = 0, i;

    for(i = STRIP_Y * UI_SCREEN_W; i < UI_SCREEN_W * UI_SCREEN_H; i++) {
        uint16_t px = (uint16_t)(s_pixels_a[i * 2] | (s_pixels_a[i * 2 + 1] << 8));

        if(px == want) n++;
    }
    return n;
}

static void test_home_outline_is_drawn(void)
{
    int dark, mid;

    home_setup();
    the_bay(0)->color = 0x000000;
    the_bay(0)->color_known = true;
    ui_nav_goto(UI_SCREEN_HOME);
    check(snapshot_into(lv_screen_active(), s_pixels_a, &s_buf_a), "home: snapshot with black loaded");
    dark = faint_pixels_in_strip();

    home_setup();
    the_bay(0)->color = 0xD32F2F;
    the_bay(0)->color_known = true;
    ui_nav_goto(UI_SCREEN_HOME);
    check(snapshot_into(lv_screen_active(), s_pixels_a, &s_buf_a), "home: snapshot with red loaded");
    mid = faint_pixels_in_strip();

    check(dark > mid + 100, "home: black filament gets a light outline on the path, frame and chip");
}

/* ------------------------------------------- home: the alert slots' check */

/* The message row's alert slot sits at the row's right end, the header's at
 * the top right. "No alerts" there is a claim about the whole scope: it must
 * not appear beside the very error the row is showing. */
static bool region_inked(int x0, int y0, int x1, int y1)
{
    const uint8_t *bg = s_pixels_a + (60 * UI_SCREEN_W + 476) * 2;
    int x, y;

    for(y = y0; y <= y1; y++)
        for(x = x0; x <= x1; x++) {
            const uint8_t *px = s_pixels_a + (y * UI_SCREEN_W + x) * 2;

            if(px[0] != bg[0] || px[1] != bg[1]) return true;
        }
    return false;
}

static void home_snapshot(const char *scenario)
{
    mmu_model_set_scenario(scenario);
    mmu_model_set_live(true);
    ui_theme_init();
    ui_nav_init();
    ui_nav_goto(UI_SCREEN_HOME);
    check(snapshot_into(lv_screen_active(), s_pixels_a, &s_buf_a), "home: snapshot for the alert slots");
}

static void test_home_check_only_when_nothing_alerts(void)
{
    uint8_t u, i;

    /* an error in the message row: no check beside it in its own slot */
    home_snapshot("idle");
    check(!region_inked(432, 50, 444, 64), "home: no check beside the message row's error (idle)");
    home_snapshot("error");
    check(!region_inked(432, 50, 444, 64), "home: no check beside the message row's error (error)");

    /* the same home with every faulted bay put right: the lane has nothing to
     * report, and its slot rests on the check */
    mmu_model_set_scenario("idle");
    mmu_model_set_live(true);
    for(u = 0; u < g_mmu.unit_count; u++)
        for(i = 0; i < g_mmu.units[u].slot_count; i++)
            if(g_mmu.units[u].slots[i].state == MMU_SLOT_ERROR) g_mmu.units[u].slots[i].state = MMU_SLOT_READY;
    g_mmu.alert_count = 0;                      /* and no history entry of the demo's either */
    ui_theme_init();
    ui_nav_init();
    ui_nav_goto(UI_SCREEN_HOME);
    check(snapshot_into(lv_screen_active(), s_pixels_a, &s_buf_a), "home: snapshot of the calm home");
    check(region_inked(432, 50, 444, 64), "home: the check rests in the message row's slot when nothing alerts");
    check(region_inked(432, 14, 444, 34), "home: and in the header's");
}

/* ---------------------------------------------------------- print: in place */

/* Printing, drawing from A2, with a filament change running on the lane. */
static void print_setup(void)
{
    mmu_model_set_scenario("printing");
    mmu_model_set_live(true);
    ui_theme_init();
    ui_nav_init();
}

/* The same print, with a lane fault on it: the screen then carries a message
 * card under the hero. */
static void print_setup_with_fault(void)
{
    print_setup();
    the_lane()->has_error = true;
    snprintf(the_lane()->error_text, sizeof the_lane()->error_text,
             "Hotend heater not reaching temperature");
}

static void print_pct_drops(void)
{
    the_bay(1)->remaining_pct = 31;        /* the bay the print draws from */
}

static void print_pct_runs_low(void)
{
    the_bay(1)->remaining_pct = 9;         /* under 15: the ring turns amber */
}

static void print_spool_is_edited(void)
{
    snprintf(the_bay(1)->material, MMU_TEXT_LEN, "ABS-GF");
    snprintf(the_bay(1)->brand, MMU_TEXT_LEN, "Prusament");
}

static void print_spool_changes_color(void)
{
    the_bay(1)->color = 0x35C4A0;
    the_bay(1)->color_known = true;
}

static void print_group_is_renamed(void)
{
    snprintf(g_mmu.groups[1].name, sizeof g_mmu.groups[1].name, "T7");
}

static void another_filament_change(void)
{
    g_mmu.filament_changes = 8;
}

static void next_step_of_the_change(void)
{
    g_mmu.step_current = 3;
}

static void print_one_more_alert(void)
{
    mmu_model_push_alert("A2: RFID tag read again", MMU_SEV_INFO, mmu_this_unit());
}

static void fault_text_changes(void)
{
    snprintf(the_lane()->error_text, sizeof the_lane()->error_text,
             "Hotend thermistor reading out of range");
}

/* -------------------------------------------------------- print: must rebuild */

static void print_stops(void)
{
    g_mmu.printing = false;
}

static void a_message_card_appears(void)
{
    the_lane()->has_error = true;
    snprintf(the_lane()->error_text, sizeof the_lane()->error_text,
             "Hotend heater not reaching temperature");
}

static void the_print_moves_to_another_unit(void)
{
    g_mmu.print_unit = 1;                  /* the standby line has to appear */
}

static void the_hero_loses_its_bay(void)
{
    g_mmu.print_unit = -1;
    g_mmu.print_slot = -1;
    g_mmu.loaded = false;
}

static void a_runout_wakes_the_button(void)
{
    g_mmu.runout_active = true;
    g_mmu.runout_from_unit = mmu_this_unit();
}

static void a_spool_gains_its_weight(void)
{
    the_bay(1)->remaining_g = 260;         /* the card grows a line of grams */
}

static void test_print_updates_in_place(void)
{
    expect_in_place(UI_SCREEN_PRINT, print_setup, print_pct_drops,
                    "print: the ring follows the printed spool down");
    expect_in_place(UI_SCREEN_PRINT, print_setup, print_pct_runs_low,
                    "print: a low spool turns the ring amber");
    expect_in_place(UI_SCREEN_PRINT, print_setup, print_spool_is_edited,
                    "print: an edited spool changes material and brand");
    expect_in_place(UI_SCREEN_PRINT, print_setup, print_spool_changes_color,
                    "print: another filament color, swatch and ink");
    expect_in_place(UI_SCREEN_PRINT, print_setup, print_group_is_renamed,
                    "print: the group's tool label moves the bay's sublabel");
    expect_in_place(UI_SCREEN_PRINT, print_setup, another_filament_change,
                    "print: another filament change counts up");
    expect_in_place(UI_SCREEN_PRINT, print_setup, next_step_of_the_change,
                    "print: the step of the change in progress");
    expect_in_place(UI_SCREEN_PRINT, print_setup_with_fault, fault_text_changes,
                    "print: the message card's text follows the fault");
}

static void test_print_needs_rebuild(void)
{
    /* The unit's history entry is shown by its tile, not by a badge: the
     * header's check ("No alerts") must go, and an empty slot is another shape. */
    expect_rebuild(UI_SCREEN_PRINT, print_setup, print_one_more_alert,
                   "print: an alert that is not a badge takes the header's check away");

    expect_rebuild(UI_SCREEN_PRINT, print_setup, print_stops,
                   "print: the print stopping is a rebuild");
    expect_rebuild(UI_SCREEN_PRINT, print_setup, a_message_card_appears,
                   "print: a message card that appears is a rebuild");
    expect_rebuild(UI_SCREEN_PRINT, print_setup, the_print_moves_to_another_unit,
                   "print: a standby line that appears is a rebuild");
    expect_rebuild(UI_SCREEN_PRINT, print_setup, the_hero_loses_its_bay,
                   "print: a hero with no bay in it is a rebuild");
    expect_rebuild(UI_SCREEN_PRINT, print_setup, a_runout_wakes_the_button,
                   "print: a button that loses its disabled look is a rebuild");
    expect_rebuild(UI_SCREEN_PRINT, print_setup, a_spool_gains_its_weight,
                   "print: a line of grams that appears is a rebuild");
}

/* ------------------------------------------------------------ load: in place */

/* A load on this unit, drawing A3, at its first step. */
static void load_setup(void)
{
    mmu_model_set_scenario("loading");
    mmu_model_set_live(true);
    ui_theme_init();
    ui_nav_init();
}

/* The same load with a step failed, which is what puts the host's words under
 * the filament line. */
static void load_setup_failed(void)
{
    mmu_model_set_scenario("error");
    mmu_model_set_live(true);
    ui_theme_init();
    ui_nav_init();
}

/** What the host's report does to the model: the toolhead is the truth, and
 * the globals the older code reads are copied out of it. */
static void host_reports(void)
{
    mmu_model_refresh_this(&g_mmu);
}

static void the_run_advances(void)
{
    the_lane()->step_current = 2;          /* "feed" is running now */
    host_reports();
}

static void a_step_is_renamed(void)
{
    snprintf(the_lane()->steps[1], sizeof the_lane()->steps[1], "cool");
    host_reports();
}

static void a_fault_arrives_mid_load(void)
{
    the_lane()->has_error = true;
    snprintf(the_lane()->error_text, sizeof the_lane()->error_text,
             "Extruder cold: the load cannot start until the hotend is above 100 C");
    host_reports();
}

static void the_spool_is_edited(void)
{
    snprintf(the_bay(2)->material, MMU_TEXT_LEN, "ABS-GF");   /* the bay being loaded */
    snprintf(the_bay(2)->brand, MMU_TEXT_LEN, "Prusament");
}

static void the_spool_changes_color(void)
{
    the_bay(2)->color = 0x35C4A0;
    the_bay(2)->color_known = true;
}

static void load_one_more_alert(void)
{
    mmu_model_push_alert("A3: RFID tag read again", MMU_SEV_INFO, mmu_this_unit());
}

static void the_fault_speaks_again(void)
{
    snprintf(the_lane()->error_text, sizeof the_lane()->error_text,
             "A3 failed to send out PLA. The extruder might be clogged.");
    host_reports();
}

/* ---------------------------------------------------------- load: must rebuild */

static void the_run_ends(void)
{
    the_lane()->busy = MMU_BUSY_NONE;
    the_lane()->busy_unit = -1;
    the_lane()->busy_slot = -1;
    the_lane()->step_current = -1;
    host_reports();
}

static void a_step_fails(void)
{
    the_lane()->step_failed = 2;           /* a line of the host's words appears */
    the_lane()->has_error = true;
    snprintf(the_lane()->error_text, sizeof the_lane()->error_text,
             "A3 failed to send out PLA. The extruder might be clogged.");
    host_reports();
}

static void the_run_loses_a_step(void)
{
    the_lane()->step_count = 4;            /* one row fewer, and a rail fewer */
    host_reports();
}

static void the_run_moves_to_another_unit(void)
{
    the_lane()->busy_unit = 1;
    host_reports();
}

static void the_host_refuses(void)
{
    snprintf(g_mmu.action_refusal, sizeof g_mmu.action_refusal,
             "Heat the extruder before an OpenAMS toolchange");
    g_mmu.action_refusal_kind = MMU_ACTION_LOAD;
}

static void test_load_updates_in_place(void)
{
    expect_in_place(UI_SCREEN_LOAD, load_setup, the_run_advances,
                    "load: the run advances a step");
    expect_in_place(UI_SCREEN_LOAD, load_setup, a_step_is_renamed,
                    "load: a step's own name changes");
    expect_in_place(UI_SCREEN_LOAD, load_setup, the_spool_is_edited,
                    "load: the bay being loaded is edited");
    expect_in_place(UI_SCREEN_LOAD, load_setup, the_spool_changes_color,
                    "load: the bay being loaded changes color");
    expect_in_place(UI_SCREEN_LOAD, load_setup_failed, the_fault_speaks_again,
                    "load: the failure's own line follows the host");
}

static void test_load_needs_rebuild(void)
{
    /* the header's check ("No alerts") cannot stay beside an alert: the slot
     * changes shape, so the screen is built again */
    expect_rebuild(UI_SCREEN_LOAD, load_setup, a_fault_arrives_mid_load,
                   "load: a fault takes the header's check away");
    expect_rebuild(UI_SCREEN_LOAD, load_setup, load_one_more_alert,
                   "load: an unread alert takes the header's check away");
    expect_rebuild(UI_SCREEN_LOAD, load_setup, the_run_ends,
                   "load: the run ending is a rebuild");
    expect_rebuild(UI_SCREEN_LOAD, load_setup, a_step_fails,
                   "load: a step that fails is a rebuild");
    expect_rebuild(UI_SCREEN_LOAD, load_setup, the_run_loses_a_step,
                   "load: a row that goes away is a rebuild");
    expect_rebuild(UI_SCREEN_LOAD, load_setup, the_run_moves_to_another_unit,
                   "load: another unit's run is a rebuild");
    expect_rebuild(UI_SCREEN_LOAD, load_setup, the_host_refuses,
                   "load: the host's refusal is a rebuild");
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

    test_home_updates_in_place();
    test_home_needs_rebuild();
    test_home_path_runs_from_the_loaded_bay();
    test_home_outline_is_drawn();
    test_home_check_only_when_nothing_alerts();
    test_print_updates_in_place();
    test_print_needs_rebuild();
    test_load_updates_in_place();
    test_load_needs_rebuild();

    printf("%s: %d failure(s)\n", s_failures ? "FAILED" : "PASSED", s_failures);
    return s_failures ? 1 : 0;
}
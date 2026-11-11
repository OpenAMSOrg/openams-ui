/**
 * @file test_screen_environment.c
 * The device's drying screen: a running dry can be stopped from it, whoever
 * started it. The host lists `dryer_start` only while a start is possible
 * (never during a dry) and keeps `dryer_stop`; the screen used to build its
 * controls, Stop included, only when a start was possible, so a dry started
 * from the printer's LCD menu (or from the screen itself) showed a readout
 * with no way to stop it.
 *
 * Real LVGL with the dummy SDL driver, as test_screen_home_sheet.c: the demo's
 * "drying" scenario binds the screen to unit A, the model is made live and
 * given the host's actions.
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

/* --------------------------------------------------------------- the hooks */

static char s_last_line[192];

/** The screen's Stop sends its action line here. (A ui_kit button that is
 * greyed out is simply not clickable.) */
void platform_action(const char *line)
{
    snprintf(s_last_line, sizeof s_last_line, "%s", line);
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

/* -------------------------------------------------------------- the model */

static mmu_unit_t *the_unit(void)
{
    return &g_mmu.units[mmu_this_unit()];
}

/** Unit A drying, as a live host reports it: `act` lists `dryer_stop` and,
 * since a dry is running, no `dryer_start` (the model then has no start to
 * offer, as status_map.c makes it). */
static void setup_drying(uint8_t acts)
{
    mmu_unit_t *u;

    mmu_model_set_scenario("drying");
    mmu_model_set_live(true);
    ui_theme_init();
    ui_nav_init();
    u = the_unit();
    u->host_idx = 1;
    u->dry_state = MMU_DRY_DEHUMIDIFYING;      /* the host's timed_dry */
    u->dry_target_c = 55;
    u->dry_remaining_min = 237;
    u->act_known = true;
    u->acts = acts;
    u->dry_unlisted = !(acts & MMU_UNIT_ACT_DRYER_START);
    s_last_line[0] = '\0';
}

static lv_obj_t *open_screen(void)
{
    ui_nav_goto(UI_SCREEN_ENVIRONMENT);
    lv_obj_update_layout(lv_screen_active());
    return lv_screen_active();
}

/* ------------------------------------------------------------------- tests */

static void test_a_running_dry_can_be_stopped(void)
{
    lv_obj_t *stop;
    char want[96];

    setup_drying(MMU_UNIT_ACT_DRYER_STOP | MMU_UNIT_ACT_CLEAR_FAULT | MMU_UNIT_ACT_RFID_SCAN);
    stop = find_label(open_screen(), view_str(VS_ENV_STOP));
    check(stop != NULL, "a dry the host lists no start for (it is running) shows Stop");
    if(!stop) return;
    check(lv_obj_is_clickable(lv_obj_get_parent(stop)),
          "  and Stop can be pressed");
    lv_obj_send_event(lv_obj_get_parent(stop), LV_EVENT_CLICKED, NULL);
    snprintf(want, sizeof want, "dry %s stop", the_unit()->name);
    check(strcmp(s_last_line, want) == 0, "  pressing it sends the unit's stop");
}

static void test_no_stop_when_the_host_offers_none(void)
{
    setup_drying(MMU_UNIT_ACT_CLEAR_FAULT);
    check(find_label(open_screen(), view_str(VS_ENV_STOP)) == NULL,
          "a dry the host offers no stop for shows no Stop");
}

static void test_an_idle_unit_offers_start(void)
{
    setup_drying(MMU_UNIT_ACT_DRYER_START | MMU_UNIT_ACT_DRYER_STOP);
    the_unit()->dry_state = MMU_DRY_IDLE;
    the_unit()->dry_remaining_min = 0;
    check(find_label(open_screen(), view_str(VS_ENV_START)) != NULL,
          "an idle unit that may start a dry shows Start");
}

/* The AMS 2 Pro reports `adapter: null` (no supply monitor): unknown, never
 * "unplugged". Start must not stay greyed out for it; a unit whose adapter is
 * known to be unplugged still cannot start. */
static void test_start_with_no_adapter_to_report(void)
{
    lv_obj_t *start;

    setup_drying(MMU_UNIT_ACT_DRYER_START | MMU_UNIT_ACT_DRYER_STOP);
    the_unit()->dry_state = MMU_DRY_IDLE;
    the_unit()->dry_remaining_min = 0;
    the_unit()->has_power_adapter = false;
    the_unit()->power_adapter = false;
    start = find_label(open_screen(), view_str(VS_ENV_START));
    check(start != NULL && lv_obj_is_clickable(lv_obj_get_parent(start)),
          "no adapter to report (AMS 2 Pro): Start can be pressed");

    setup_drying(MMU_UNIT_ACT_DRYER_START | MMU_UNIT_ACT_DRYER_STOP);
    the_unit()->dry_state = MMU_DRY_IDLE;
    the_unit()->dry_remaining_min = 0;
    the_unit()->has_power_adapter = true;
    the_unit()->power_adapter = false;
    start = find_label(open_screen(), view_str(VS_ENV_START));
    check(start == NULL || !lv_obj_is_clickable(lv_obj_get_parent(start)),
          "an adapter known to be unplugged: Start stays greyed out");
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

    test_a_running_dry_can_be_stopped();
    test_no_stop_when_the_host_offers_none();
    test_an_idle_unit_offers_start();
    test_start_with_no_adapter_to_report();

    printf("%s: %d failure(s)\n", s_failures ? "FAILED" : "PASSED", s_failures);
    return s_failures ? 1 : 0;
}

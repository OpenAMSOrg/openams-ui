/**
 * @file ui_nav.c
 * Screen factory, back stack and the one-modal-at-a-time registry.
 */

#include "ui/ui_nav.h"
#include "view/strings.h"

#include "ui/ui_screens.h"
#include "ui/ui_theme.h"
#include "ui/ui_widgets.h"

static ui_screen_id_t s_current = UI_SCREEN_HOME;
static bool s_have_screen;
static bool s_blank;   /**< the shown screen is the empty one: the previous was released and the next not built */
static lv_obj_t *s_modal;
static int s_alert_count;

/* Deferred nav: one pending request at a time. A goto (is_goto) always wins
 * over a plain refresh, and the most recent goto target wins over an
 * earlier one - see ui_nav_goto_async()/ui_nav_refresh_async() below. */
static bool s_async_pending;
static bool s_async_is_goto;
static ui_screen_id_t s_async_target;

static const char *const s_names[UI_SCREEN_COUNT] = {
    "home", "load", "alerts", "edit", "environment", "print", "refill",   /* lit-ok: screen ids, for logs */
    "settings", "mapping", "order", "unitinfo", "debug",   /* lit-ok */
#if defined(MMU_FEATURE_TAG_WRITER)
    "tagwriter",   /* lit-ok */
#endif
};

/* The screens' titles, in the current language (design/strings). */
static const view_str_id_t s_titles[UI_SCREEN_COUNT] = {
    VS_SCREEN_HOME, VS_SCREEN_LOAD_FULL, VS_LABEL_ALERTS, VS_SCREEN_EDIT_NONE, VS_SCREEN_ENV,
    VS_SCREEN_PRINT, VS_SCREEN_GROUPS, VS_LABEL_SETTINGS, VS_SCREEN_PRINT_MAPPING,
    VS_SCREEN_ORDER, VS_SCREEN_UNITINFO, VS_SCREEN_DEBUG,
#if defined(MMU_FEATURE_TAG_WRITER)
    VS_SCREEN_TAG_WRITER,
#endif
};

static lv_obj_t *build_screen(ui_screen_id_t id)
{
    const ui_screen_ops_t *ops = ui_screen_ops(id);
    return ops && ops->build ? ops->build() : NULL;
}

void ui_nav_init(void)
{
    s_current = UI_SCREEN_HOME;
    s_have_screen = false;
    s_blank = false;
    s_modal = NULL;
    s_alert_count = 0;
    s_async_pending = false;
    s_async_is_goto = false;
    s_async_target = UI_SCREEN_HOME;
}

/* Screen builds, for the device's `perf` console command. */
static uint32_t s_builds, s_build_ms_total, s_build_ms_max;

void ui_nav_build_stats(uint32_t *builds, uint32_t *ms_total, uint32_t *ms_max, bool reset)
{
    if(builds) *builds = s_builds;
    if(ms_total) *ms_total = s_build_ms_total;
    if(ms_max) *ms_max = s_build_ms_max;
    if(reset) s_builds = s_build_ms_total = s_build_ms_max = 0;
}

bool ui_nav_mem_ok(size_t need)
{
    lv_mem_monitor_t mon;

    lv_mem_monitor(&mon);
    if(mon.total_size == 0) return true;   /* an allocator that cannot report: no verdict */
    if(mon.free_size >= need) return true;
    LV_LOG_WARN("not enough memory (%lu free, %lu needed)", (unsigned long)mon.free_size,
                (unsigned long)need);
    return false;
}

bool ui_nav_goto(ui_screen_id_t id)
{
    lv_obj_t *scr;
    uint32_t t0 = lv_tick_get(), took;

    if(id < 0 || id >= UI_SCREEN_COUNT) return false;

    ui_nav_modal_close_all();
    ui_theme_init();

    /* One screen of the pool at a time. A screen is up to ~45 KB of a 96 KB
     * pool, so building the next one while the previous is still alive needed
     * two of them at once, and a screen change in some languages ran the pool
     * out. The previous screen is released first, by loading an empty one in
     * its place; nothing is drawn until this function returns, so no frame
     * shows it. */
    if(s_have_screen && !s_blank) {
        lv_obj_t *blank = lv_obj_create(NULL);

        if(blank) {
            lv_screen_load_anim(blank, LV_SCREEN_LOAD_ANIM_NONE, 0, 0, true);
            s_blank = true;
        }
    }

    /* A failed allocation inside LVGL is not survivable (it writes through
     * the NULL it gets back), so the build only starts with room to finish.
     * When there is none the empty screen stays, and the next change retries. */
    if(!ui_nav_mem_ok(UI_NAV_SCREEN_NEED)) return false;

    scr = build_screen(id);
    if(!scr) {
        LV_LOG_WARN("screen %d did not build", (int)id);
        return false;
    }

    /* auto-delete hands the previous screen (the empty one, or the old one
     * when it could not be released) to LVGL. */
    lv_screen_load_anim(scr, LV_SCREEN_LOAD_ANIM_NONE, 0, 0, true);
    s_current = id;
    s_have_screen = true;
    s_blank = false;
    took = lv_tick_elaps(t0);
    s_builds++;
    s_build_ms_total += took;
    if(took > s_build_ms_max) s_build_ms_max = took;
    return true;
}

void ui_nav_back(void)
{
    if(s_current != UI_SCREEN_HOME) ui_nav_goto(UI_SCREEN_HOME);
}

void ui_nav_refresh(void)
{
    ui_nav_goto(s_current);
}

bool ui_nav_update(void)
{
    const ui_screen_ops_t *ops;

    if(s_blank) return false;   /* the screen's widgets are gone: nothing to update */
    ops = ui_screen_ops(s_current);

    return ops && ops->update ? ops->update() : false;
}

static void async_run(void *user)
{
    bool is_goto = s_async_is_goto;
    ui_screen_id_t target = s_async_target;

    (void)user;
    s_async_pending = false;

    if(is_goto) ui_nav_goto(target);
    else        ui_nav_refresh();
}

void ui_nav_goto_async(ui_screen_id_t id)
{
    /* A goto always overwrites whatever is pending (even a refresh, or an
     * earlier goto target): it is scheduled either way, so only skip
     * lv_async_call() itself when a request is already queued. */
    s_async_is_goto = true;
    s_async_target = id;
    if(s_async_pending) return;
    s_async_pending = true;
    lv_async_call(async_run, NULL);
}

void ui_nav_back_async(void)
{
    if(s_current != UI_SCREEN_HOME) ui_nav_goto_async(UI_SCREEN_HOME);
}

void ui_nav_refresh_async(void)
{
    if(s_async_pending) return;   /* a goto already queued beats a refresh */
    s_async_pending = true;
    s_async_is_goto = false;
    lv_async_call(async_run, NULL);
}

ui_screen_id_t ui_nav_current(void)
{
    return s_current;
}

bool ui_nav_at_home(void)
{
    return s_current == UI_SCREEN_HOME;
}

const char *ui_nav_screen_name(ui_screen_id_t id)
{
    if(id < 0 || id >= UI_SCREEN_COUNT) return "?";
    return s_names[id];
}

const char *ui_nav_screen_title(ui_screen_id_t id)
{
    if(id < 0 || id >= UI_SCREEN_COUNT) return "?";
    return view_str(s_titles[id]);
}

void ui_nav_modal_add(lv_obj_t *modal)
{
    ui_nav_modal_close_all();
    s_modal = modal;
}

void ui_nav_modal_close_all(void)
{
    ui_toast_close();
    if(s_modal) {
        lv_obj_delete(s_modal);
        s_modal = NULL;
    }
}

int ui_nav_modal_count(void)
{
    return s_modal ? 1 : 0;
}

void ui_nav_set_alert_count(int count)
{
    s_alert_count = count < 0 ? 0 : count;
}

int ui_nav_alert_count(void)
{
    return s_alert_count;
}

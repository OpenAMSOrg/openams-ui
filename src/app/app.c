/**
 * @file app.c
 */
#include "app/app.h"

#include "lvgl.h"
#include "app/actions.h"
#include "app/platform.h"
#include "model/mmu_model.h"
#include "ui/ui_language.h"
#include "ui/ui_nav.h"
#include "ui/ui_theme.h"

/* The spec's scenarios advance load/unload steps every ~1.5s (three ticks at
 * TICKS_PER_STEP=3), so the model timer itself runs at 500ms. */
#define MODEL_TICK_MS 500

/* Long enough for every code in design/strings, short enough to be a static
 * buffer. */
#define LANG_MAX 16

static lv_timer_t *s_model_timer;

static void model_timer_cb(lv_timer_t *t)
{
    (void)t;
    g_mmu.now_ms = lv_tick_get();      /* the model's clock: alert times, the badge's ageing */
    mmu_model_tick();
}

void app_init(const char *scenario, const char *unit_name)
{
    char lang[LANG_MAX];

    /* Before anything is drawn: the strings and the font slots both have to
     * be in the stored language by the time the first screen asks for a
     * glyph. A device with nothing stored (a first boot) lands on English. */
    platform_language_get(lang, sizeof lang);
    if(!ui_set_language(lang)) (void)ui_set_language("en");

    mmu_model_init();
    if(scenario && scenario[0]) mmu_model_set_scenario(scenario);
    if(unit_name) mmu_set_this_unit_by_name(unit_name);

    ui_theme_init();
    ui_nav_init();

    if(!s_model_timer) {
        s_model_timer = lv_timer_create(model_timer_cb, MODEL_TICK_MS, NULL);
    }

    ui_nav_goto(UI_SCREEN_HOME);
}

/* Both of these are reached from a click handler (the "This unit" bind
 * confirm, the debug scenario picker), so the nav runs deferred: the
 * screen whose click is still dispatching is the very one being replaced. */
void app_set_this_unit(int unit_idx)
{
    mmu_set_this_unit(unit_idx);
    actions_log("bind display to %s", mmu_unit_name(unit_idx));
    ui_nav_goto_async(UI_SCREEN_HOME);
}

void app_set_scenario(const char *name)
{
    mmu_model_set_scenario(name);
    ui_nav_goto_async(UI_SCREEN_HOME);
}

int app_scenario_count(void)
{
    return mmu_model_scenario_count();
}

const char *app_scenario_name(int index)
{
    return mmu_model_scenario_name(index);
}

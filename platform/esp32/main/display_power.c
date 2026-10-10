/**
 * @file display_power.c
 */
#include "display_power.h"

#include "app/platform.h"
#include "display.h"
#include "touch.h"

#include "esp_log.h"

static const char *TAG = "display_power";

typedef enum {
    PWR_AWAKE,
    PWR_DIMMED,
    PWR_OFF,
} power_state_t;

#define DIM_FADE_MS 300
#define DIM_PCT 10

static lv_display_t *s_disp;
static power_state_t s_state = PWR_AWAKE;
static bool s_prev_touching;

void display_power_init(lv_display_t *disp)
{
    s_disp = disp;
}

void display_power_tick(void)
{
    if(!s_disp) return;

    bool touching = touch_is_pressed_raw();
    bool fresh_touch = touching && !s_prev_touching;
    s_prev_touching = touching;

    if(fresh_touch && s_state != PWR_AWAKE) {
        platform_display_cfg_t cfg;
        platform_display_get(&cfg);

        display_set_brightness(cfg.brightness_pct, 0);
        touch_swallow_current_press();
        /* The swallowed tap reports RELEASED to LVGL, so its own
         * inactivity clock never saw it (lv_indev.c only stamps
         * last_activity_time on a PRESSED read) — reset it ourselves so
         * the very next tick doesn't immediately re-dim. */
        lv_display_trigger_activity(s_disp);

        ESP_LOGI(TAG, "display woke on tap (from %s)", s_state == PWR_OFF ? "off" : "dim");
        s_state = PWR_AWAKE;
        return;
    }

    platform_display_cfg_t cfg;
    platform_display_get(&cfg);
    uint32_t idle_ms = lv_display_get_inactive_time(s_disp);

    if(s_state != PWR_OFF && cfg.off_after_s && idle_ms >= (uint32_t)cfg.off_after_s * 1000) {
        display_set_brightness(0, 0);
        ESP_LOGI(TAG, "display backlight off (idle %ums >= %us)", (unsigned)idle_ms,
                 (unsigned)cfg.off_after_s);
        s_state = PWR_OFF;
    }
    else if(s_state == PWR_AWAKE && cfg.dim_after_s &&
            idle_ms >= (uint32_t)cfg.dim_after_s * 1000) {
        display_set_brightness(DIM_PCT, DIM_FADE_MS);
        ESP_LOGI(TAG, "display dimmed to %u%% (idle %ums >= %us)", DIM_PCT, (unsigned)idle_ms,
                 (unsigned)cfg.dim_after_s);
        s_state = PWR_DIMMED;
    }
}

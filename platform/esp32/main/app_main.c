/**
 * @file app_main.c
 * Entry point: bring up the panel + touch, hand LVGL its tick source, run
 * the portable app (src/app/app.c) and pump lv_timer_handler() forever from
 * a dedicated task.
 */
#include "lvgl.h"

#include <stdio.h>
#include <string.h>

#include "board.h"
#include "display.h"
#include "display_cfg_store.h"
#include "display_power.h"
#include "net_config.h"
#include "net_console.h"
#include "net_link.h"
#include "net_wifi.h"
#include "ota.h"
#if defined(MMU_FEATURE_TAG_WRITER)
#include "tag_writer.h"
#endif
#include "touch.h"
#if BOARD_TOUCH_NEEDS_CALIBRATION
#include "touch_calib_store.h"
#include "touch_calib_ui.h"
#endif
#include "ui_diag.h"

#include "app/app.h"
#include "app/app_live.h"
#include "ui/ui_nav.h"
#include "model/mmu_model.h"

#include <stdbool.h>

#include "driver/gpio.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "app_main";

#define LVGL_TASK_STACK_BYTES (12 * 1024)
#define LVGL_TASK_PRIORITY 5
#define LVGL_TASK_CORE 1

/* The UI task is created before Wi-Fi starts and waits for app_main() to
 * release it (see lvgl_task). */
static TaskHandle_t s_lvgl_task;

/* Onboard RGB LED, active low. Left floating it can power up lit; force
 * all three channels off before anything else runs. Only on a board that
 * has one: on the ES3C50P these pins are the panel's data lines. */
#if BOARD_HAS_RGB_LED
#define PIN_LED_R 22
#define PIN_LED_G 16
#define PIN_LED_B 17

static void rgb_led_off(void)
{
    gpio_config_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.pin_bit_mask = (1ULL << PIN_LED_R) | (1ULL << PIN_LED_G) | (1ULL << PIN_LED_B);
    cfg.mode = GPIO_MODE_OUTPUT;
    gpio_config(&cfg);

    gpio_set_level(PIN_LED_R, 1);
    gpio_set_level(PIN_LED_G, 1);
    gpio_set_level(PIN_LED_B, 1);
}
#else
static void rgb_led_off(void)
{
}
#endif

static uint32_t tick_cb(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000);
}

/* Set once in app_main() before the UI task starts; read every loop
 * iteration by ota_rollback_poll() (see ota.c). */
static bool s_wifi_configured;

/* The `ui` console command (ui_diag.h): a request flag the UI task answers,
 * and the startup task's stack headroom, measured once app_main() is done
 * building the first screen on it. */
static volatile bool s_diag_wanted;
static unsigned s_main_stack_left;

void ui_diag_request(void)
{
    s_diag_wanted = true;
}

/* ------------------------------------------------------------------ perf */

/* Timing since the last `perf` (ui_diag.h), all in microseconds and all
 * written by the UI task alone. A refresh is LVGL's REFR_START..REFR_READY:
 * rendering every invalid area and flushing it to the panel. */
static volatile bool s_perf_wanted;
static volatile int s_nav_wanted = -1;
static int64_t s_perf_since;
static int64_t s_refr_t0;
static uint32_t s_refr_n, s_refr_us_total, s_refr_us_max;
static uint32_t s_loop_n, s_loop_us_total, s_loop_us_max;
static uint32_t s_poll_us_total, s_poll_us_max;
static uint32_t s_sync_n;
static uint32_t s_lat_n, s_lat_us_total, s_lat_us_max;
static int64_t s_lat_press;            /* a press waiting for its refresh */
static uint32_t s_flush_px0;

void ui_perf_request(void)
{
    s_perf_wanted = true;
}

/* ------------------------------------------------------------------- tap */

/* A second pointer for the console's `tap`: pressed at (x, y) until the hold
 * runs out, released otherwise. Only the UI task touches it. */
static volatile bool s_tap_wanted;
static volatile int s_tap_x, s_tap_y, s_tap_hold;
static int64_t s_tap_until;          /* press ends (us); 0 when no tap is on */
static int64_t s_watch_until;        /* print the top layer until (us) */
static int64_t s_watch_t0;
static char s_watch_last[256];

void ui_tap_request(int x, int y, int hold_ms)
{
    s_tap_x = x;
    s_tap_y = y;
    s_tap_hold = hold_ms > 0 ? hold_ms : 60;
    s_tap_wanted = true;
}

static void tap_read_cb(lv_indev_t *indev, lv_indev_data_t *data)
{
    (void)indev;
    data->point.x = s_tap_x;
    data->point.y = s_tap_y;
    data->state = (s_tap_until && esp_timer_get_time() < s_tap_until) ? LV_INDEV_STATE_PRESSED
                                                                     : LV_INDEV_STATE_RELEASED;
}

/* The top layer, two levels deep, as one line: "x,y wxh" per object. */
static void watch_top_layer(void)
{
    char line[256];
    size_t n = 0;
    lv_obj_t *top = lv_layer_top();
    uint32_t i, j;

    line[0] = '\0';
    for(i = 0; i < lv_obj_get_child_count(top) && n < sizeof line - 40; i++) {
        lv_obj_t *a = lv_obj_get_child(top, (int32_t)i);
        lv_area_t r;

        lv_obj_get_coords(a, &r);
        n += (size_t)snprintf(line + n, sizeof line - n, "[%ld,%ld %ldx%ld", (long)r.x1, (long)r.y1,
                              (long)lv_area_get_width(&r), (long)lv_area_get_height(&r));
        for(j = 0; j < lv_obj_get_child_count(a) && n < sizeof line - 40; j++) {
            lv_obj_t *b = lv_obj_get_child(a, (int32_t)j);

            lv_obj_get_coords(b, &r);
            n += (size_t)snprintf(line + n, sizeof line - n, " (%ld,%ld %ldx%ld ty%ld)", (long)r.x1,
                                  (long)r.y1, (long)lv_area_get_width(&r), (long)lv_area_get_height(&r),
                                  (long)lv_obj_get_style_translate_y(b, 0));
        }
        n += (size_t)snprintf(line + n, sizeof line - n, "]");
    }
    if(strcmp(line, s_watch_last) != 0) {
        printf("tap: +%lldms top=%s\n", (long long)((esp_timer_get_time() - s_watch_t0) / 1000),
               line[0] ? line : "(empty)");
        fflush(stdout);
        snprintf(s_watch_last, sizeof s_watch_last, "%s", line);
    }
}

void ui_nav_request(int id)
{
    s_nav_wanted = id;
}

static void refr_event_cb(lv_event_t *e)
{
    int64_t now = esp_timer_get_time();

    if(lv_event_get_code(e) == LV_EVENT_REFR_START) {
        s_refr_t0 = now;
        return;
    }
    if(s_refr_t0) {
        uint32_t us = (uint32_t)(now - s_refr_t0);

        s_refr_n++;
        s_refr_us_total += us;
        if(us > s_refr_us_max) s_refr_us_max = us;
        s_refr_t0 = 0;
    }
    /* The first refresh finished after a press is when the press can have
     * shown on the screen. */
    if(s_lat_press) {
        uint32_t us = (uint32_t)(now - s_lat_press);

        s_lat_n++;
        s_lat_us_total += us;
        if(us > s_lat_us_max) s_lat_us_max = us;
        s_lat_press = 0;
    }
}

static void perf_reset(void)
{
    uint32_t px;

    display_flush_stats(NULL, &px);
    s_flush_px0 = px;
    s_perf_since = esp_timer_get_time();
    s_refr_n = s_refr_us_total = s_refr_us_max = 0;
    s_loop_n = s_loop_us_total = s_loop_us_max = 0;
    s_poll_us_total = s_poll_us_max = 0;
    s_sync_n = 0;
    s_lat_n = s_lat_us_total = s_lat_us_max = 0;
    ui_nav_build_stats(NULL, NULL, NULL, true);
}

static void perf_print(void)
{
    uint32_t px = 0, builds = 0, build_ms = 0, build_max = 0;
    float secs = (float)(esp_timer_get_time() - s_perf_since) / 1e6f;

    if(secs <= 0.0f) secs = 1.0f;
    display_flush_stats(NULL, &px);
    ui_nav_build_stats(&builds, &build_ms, &build_max, false);
    printf("perf: %.1f s: refreshes %lu (%.1f/s) avg %.1f ms max %.1f ms; pixels %.0f/s\n", (double)secs,
           (unsigned long)s_refr_n, (double)(s_refr_n / secs),
           s_refr_n ? (double)s_refr_us_total / s_refr_n / 1000.0 : 0.0, (double)s_refr_us_max / 1000.0,
           (double)((px - s_flush_px0) / secs));
    printf("perf: loop %lu passes avg %.2f ms max %.1f ms; app poll avg %.2f ms max %.1f ms; syncs %lu\n",
           (unsigned long)s_loop_n, s_loop_n ? (double)s_loop_us_total / s_loop_n / 1000.0 : 0.0,
           (double)s_loop_us_max / 1000.0, s_loop_n ? (double)s_poll_us_total / s_loop_n / 1000.0 : 0.0,
           (double)s_poll_us_max / 1000.0, (unsigned long)s_sync_n);
    printf("perf: screen builds %lu avg %.1f ms max %lu ms; press-to-screen %lu avg %.1f ms max %.1f ms\n",
           (unsigned long)builds, builds ? (double)build_ms / builds : 0.0, (unsigned long)build_max,
           (unsigned long)s_lat_n, s_lat_n ? (double)s_lat_us_total / s_lat_n / 1000.0 : 0.0,
           (double)s_lat_us_max / 1000.0);
    fflush(stdout);
}

static void ui_diag_print(void)
{
    uint32_t calls = 0, px = 0;
    lv_mem_monitor_t mon;
    lv_obj_t *scr = lv_screen_active();

    display_flush_stats(&calls, &px);
    lv_mem_monitor(&mon);
    printf("ui: flushes=%lu pixels=%lu screen_children=%lu nav_screen=%d top=%lu sys=%lu\n",
           (unsigned long)calls, (unsigned long)px,
           (unsigned long)(scr ? lv_obj_get_child_count(scr) : 0), (int)ui_nav_current(),
           (unsigned long)lv_obj_get_child_count(lv_layer_top()),
           (unsigned long)lv_obj_get_child_count(lv_layer_sys()));
    printf("ui: model units=%u toolheads=%u live=%d\n", (unsigned)g_mmu.unit_count,
           (unsigned)g_mmu.toolhead_count, (int)mmu_model_is_live());
    printf("ui: lvgl mem used=%u%% free=%lu max_used=%lu frag=%u%%\n", (unsigned)mon.used_pct,
           (unsigned long)mon.free_size, (unsigned long)mon.max_used, (unsigned)mon.frag_pct);
    printf("ui: heap free=%u largest=%u\n",
           (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT),
           (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
    net_link_diag_print();
    printf("ui: stack left: ui task=%u bytes, startup task=%u bytes\n",
           (unsigned)uxTaskGetStackHighWaterMark(NULL), s_main_stack_left);
    fflush(stdout);
}

static void lvgl_task(void *arg)
{
    (void)arg;
    /* Nothing touches LVGL until app_main() has built the first screen. */
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
    perf_reset();
    for(;;) {
        int64_t t0 = esp_timer_get_time(), t1, press;
        uint32_t us;

        if(net_link_sync(&g_mmu)) {
            app_live_model_changed();
            s_sync_n++;
        }
        app_live_poll();
        t1 = esp_timer_get_time();
        us = (uint32_t)(t1 - t0);
        s_poll_us_total += us;
        if(us > s_poll_us_max) s_poll_us_max = us;

        press = touch_take_press_us();
        if(press) s_lat_press = press;
        if(s_tap_wanted) {
            s_tap_wanted = false;
            s_watch_t0 = esp_timer_get_time();
            s_tap_until = s_watch_t0 + (int64_t)s_tap_hold * 1000;
            s_watch_until = s_watch_t0 + 600000;
            s_watch_last[0] = '\0';
            s_lat_press = s_watch_t0;
        }
        if(s_nav_wanted >= 0) {
            int id = s_nav_wanted;

            s_nav_wanted = -1;
            ui_nav_goto((ui_screen_id_t)id);
        }
        lv_timer_handler();
        if(s_watch_until) {
            watch_top_layer();
            if(esp_timer_get_time() > s_watch_until) s_watch_until = 0;
        }
        if(s_tap_until && esp_timer_get_time() > s_tap_until + 100000) s_tap_until = 0;
        us = (uint32_t)(esp_timer_get_time() - t0);
        s_loop_n++;
        s_loop_us_total += us;
        if(us > s_loop_us_max) s_loop_us_max = us;
        if(s_perf_wanted) {
            s_perf_wanted = false;
            perf_print();
            perf_reset();
        }
        display_power_tick();
        ota_rollback_poll(s_wifi_configured, net_wifi_is_up());
        if(s_diag_wanted) {
            s_diag_wanted = false;
            ui_diag_print();
        }
        vTaskDelay(pdMS_TO_TICKS(5));
    }
}

void app_main(void)
{
    rgb_led_off();

    ESP_LOGI(TAG, "MMU display starting");
    ESP_LOGI(TAG, "free heap (internal): %u bytes",
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL));
    ESP_LOGI(TAG, "LVGL pool size: %u bytes", (unsigned)LV_MEM_SIZE);

    lv_init();
    lv_tick_set_cb(tick_cb);

    lv_display_t *disp = display_init();
    if(!disp) {
        ESP_LOGE(TAG, "display_init() failed — halting");
        return;
    }
    {
        lv_indev_t *tap = lv_indev_create();

        lv_indev_set_type(tap, LV_INDEV_TYPE_POINTER);
        lv_indev_set_read_cb(tap, tap_read_cb);
        lv_indev_set_display(tap, disp);
    }
    lv_display_add_event_cb(disp, refr_event_cb, LV_EVENT_REFR_START, NULL);
    lv_display_add_event_cb(disp, refr_event_cb, LV_EVENT_REFR_READY, NULL);
    const panel_info_t *panel = display_get_panel_info();
    ESP_LOGI(TAG, "board %s, panel %s, id_read_ok=%s", BOARD_NAME, panel->name,
             panel->id_read_ok ? "yes" : "no");

    if(!touch_init()) {
        ESP_LOGW(TAG, "touch_init() failed — continuing with no touch input");
    }

    /* Loads (or defaults) brightness/contrast/dim/off/flip and applies them
     * immediately — this is what turns the backlight on for the first
     * time, so it must run before the calibration screen (or anything
     * else) needs to be visible. */
    display_cfg_store_init();
    display_power_init(disp);

#if defined(MMU_FEATURE_TAG_WRITER)
    /* NVS is up: the spool being edited, the Bambu key and the tag library.
     * No hardware yet: the reader starts when its screen opens. */
    tag_writer_init();
#endif

    /* The UI task's 12 KB stack is the largest block the display asks for.
     * Taken after Wi-Fi starts, it raced the driver's own allocations for a
     * fragmented heap and could fail, leaving the screen blank with nothing
     * logged; taken here it comes from a heap nothing else is using yet. The
     * task waits until app_main() releases it at the end. */
    if(xTaskCreatePinnedToCore(lvgl_task, "lvgl", LVGL_TASK_STACK_BYTES, NULL, LVGL_TASK_PRIORITY,
                               &s_lvgl_task, LVGL_TASK_CORE) != pdPASS) {
        s_lvgl_task = NULL;
        ESP_LOGE(TAG, "no memory for the UI task (heap free %u, largest block %u) — the screen "
                      "will stay blank",
                 (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT),
                 (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
    }

    /* NVS is initialized by display_cfg_store_init() above. The serial setup
     * console is always available; Wi-Fi only starts if an SSID is stored, so
     * a factory-fresh display keeps behaving exactly as before (demo mode). */
    net_console_start();
    net_config_t net_cfg;
    bool live = false;
    s_wifi_configured = net_config_load(&net_cfg);
    if(s_wifi_configured) {
        net_wifi_start(&net_cfg);
        ESP_LOGI(TAG, "Wi-Fi started for ssid \"%s\"", net_cfg.ssid);

        /* A stored Moonraker URL turns the display live: the model then comes
         * from the link and the dummy scenario timer is never started. */
        live = net_cfg.moonraker_url[0] != '\0';
        if(live) net_link_start(&net_cfg);
    }
    else {
        ESP_LOGI(TAG, "no Wi-Fi credentials stored — demo mode");
    }

#if BOARD_TOUCH_NEEDS_CALIBRATION
    touch_calib_store_init();
    float coef[6];
    if(touch_calib_store_load(coef)) {
        touch_set_affine(coef);
        ESP_LOGI(TAG, "loaded touch calibration: x=%.6f*rx+%.6f*ry+%.3f y=%.6f*rx+%.6f*ry+%.3f",
                 (double)coef[0], (double)coef[1], (double)coef[2], (double)coef[3],
                 (double)coef[4], (double)coef[5]);
    }
    else {
        ESP_LOGW(TAG, "no valid touch calibration in NVS — running the calibration screen");
        touch_calib_run(disp);
    }
#endif

    if(live) app_live_init();
    else app_init(NULL, NULL);

    ESP_LOGI(TAG, "free heap (internal) after init: %u bytes",
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL));
    s_main_stack_left = (unsigned)uxTaskGetStackHighWaterMark(NULL);
    ESP_LOGI(TAG, "startup task stack left after init: %u bytes", s_main_stack_left);

    /* The UI is up as of here; arms rollback confirmation (see ota.c), which
     * lvgl_task then polls every iteration. */
    ota_rollback_init();

    if(s_lvgl_task) xTaskNotifyGive(s_lvgl_task);
}

/**
 * @file touch_calib_ui.c
 */
#include "touch_calib_ui.h"

#include "app/platform.h"
#include "display.h"
#include "touch.h"
#include "touch_calib_store.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define PANEL_W 480
#define PANEL_H 320

#define TARGET_COUNT 4
#define SAMPLES_PER_TARGET 5

/* z1 is position-dependent (much lower near panel edges/corners than mid-
 * panel) and noisier there too; touch.c's real accept/reject signal is R
 * (position-independent — see R_MAX_PRESS), applied unconditionally inside
 * acquire_sample_ex(), so z1_min here is only the same small noise/open-
 * circuit floor touch.c uses (Z1_NOISE_FLOOR), and spread stays relaxed
 * for the same corner-target reason as before. */
#define CAL_Z1_MIN 40
#define CAL_SPREAD_MAX 150

/* A target's averaged raw result is rejected as implausible (a stray press
 * mistaken for a real one, e.g. a ghost/leftover contact) unless it differs
 * from the PREVIOUS target's raw result by more than this on at least one
 * axis. Not applied to target 0, which has no previous target. */
#define PLAUSIBLE_MIN_DELTA 800.0f

#define COL_IDLE  0xFFFFFF
#define COL_AMBER 0xF5A524
#define COL_MINT  0x3DDC97

static const char *TAG = "touch_calib_ui";

/* Panel-space target order: TL, TR, BR, BL. Inset further from the true
 * corners than the first attempt (30/290) — the owner could not reliably
 * register a press right at 450,290 / 30,290. */
static const int32_t s_target_px[TARGET_COUNT] = { 40, 440, 440, 40 };
static const int32_t s_target_py[TARGET_COUNT] = { 40, 40, 280, 280 };

/* Crosshair points precomputed per target (two 20px lines crossing at the
 * target), so nothing here mutates a buffer another live widget still
 * references. */
static const lv_point_precise_t s_h_pts[TARGET_COUNT][2] = {
    { { 30, 40 }, { 50, 40 } },
    { { 430, 40 }, { 450, 40 } },
    { { 430, 280 }, { 450, 280 } },
    { { 30, 280 }, { 50, 280 } },
};
static const lv_point_precise_t s_v_pts[TARGET_COUNT][2] = {
    { { 40, 30 }, { 40, 50 } },
    { { 440, 30 }, { 440, 50 } },
    { { 440, 270 }, { 440, 290 } },
    { { 40, 270 }, { 40, 290 } },
};

static void pump(uint32_t ms)
{
    int64_t end_us = esp_timer_get_time() + (int64_t)ms * 1000;
    while(esp_timer_get_time() < end_us) {
        lv_timer_handler();
        vTaskDelay(pdMS_TO_TICKS(5));
    }
}

static lv_obj_t *make_dark_screen(const char *title)
{
    lv_obj_t *scr = lv_obj_create(NULL);
    lv_obj_remove_style_all(scr);
    lv_obj_set_style_bg_color(scr, lv_color_hex(0x0B0E12), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
    lv_obj_set_scrollable(scr, false);

    lv_obj_t *label = lv_label_create(scr);
    lv_label_set_text(label, title);
    lv_obj_set_style_text_color(label, lv_color_hex(0xFFFFFF), 0);
    lv_obj_align(label, LV_ALIGN_TOP_MID, 0, 12);

    return scr;
}

/* Returns the crosshair's ring object, so the caller can recolour it live
 * (idle white / amber while a press is detected but not yet stable / mint
 * for 300ms once accepted). */
static lv_obj_t *add_crosshair(lv_obj_t *scr, int target_idx)
{
    static lv_style_t line_style;
    static bool style_ready;
    if(!style_ready) {
        lv_style_init(&line_style);
        lv_style_set_line_color(&line_style, lv_color_hex(0xFFFFFF));
        lv_style_set_line_width(&line_style, 1);
        style_ready = true;
    }

    lv_obj_t *hl = lv_line_create(scr);
    lv_line_set_points(hl, s_h_pts[target_idx], 2);
    lv_obj_add_style(hl, &line_style, 0);

    lv_obj_t *vl = lv_line_create(scr);
    lv_line_set_points(vl, s_v_pts[target_idx], 2);
    lv_obj_add_style(vl, &line_style, 0);

    lv_obj_t *circ = lv_obj_create(scr);
    lv_obj_remove_style_all(circ);
    lv_obj_set_size(circ, 14, 14);
    lv_obj_set_style_radius(circ, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(circ, 1, 0);
    lv_obj_set_style_border_color(circ, lv_color_hex(COL_IDLE), 0);
    lv_obj_set_style_bg_opa(circ, LV_OPA_TRANSP, 0);
    lv_obj_set_pos(circ, s_target_px[target_idx] - 7, s_target_py[target_idx] - 7);

    return circ;
}

/* -------------------------------------------------------- affine fit (3x3) */

static float det3(const float m[3][3])
{
    return m[0][0] * (m[1][1] * m[2][2] - m[1][2] * m[2][1]) -
           m[0][1] * (m[1][0] * m[2][2] - m[1][2] * m[2][0]) +
           m[0][2] * (m[1][0] * m[2][1] - m[1][1] * m[2][0]);
}

/* Solves the 3x3 system ATA * coef = ATb via Cramer's rule. */
static bool solve3(const float ata[3][3], const float atb[3], float coef[3])
{
    float d = det3(ata);
    if(fabsf(d) < 1e-9f) return false;

    for(int col = 0; col < 3; col++) {
        float m[3][3];
        memcpy(m, ata, sizeof(m));
        for(int row = 0; row < 3; row++) m[row][col] = atb[row];
        coef[col] = det3(m) / d;
    }
    return true;
}

/* Least-squares fit of x = a*rx + b*ry + c and y = d*rx + e*ry + f over
 * TARGET_COUNT (rx,ry)->(px,py) pairs, via the normal equations (the two
 * fits share the same ATA, only the right-hand side differs). */
static bool fit_affine(const float rx[TARGET_COUNT], const float ry[TARGET_COUNT],
                       float coef[6])
{
    float ata[3][3] = { { 0 } };
    float atx[3] = { 0 }, aty[3] = { 0 };

    for(int i = 0; i < TARGET_COUNT; i++) {
        float row[3] = { rx[i], ry[i], 1.0f };
        for(int r = 0; r < 3; r++) {
            for(int c = 0; c < 3; c++) ata[r][c] += row[r] * row[c];
            atx[r] += row[r] * (float)s_target_px[i];
            aty[r] += row[r] * (float)s_target_py[i];
        }
    }

    float abc[3], def[3];
    if(!solve3(ata, atx, abc)) return false;
    if(!solve3(ata, aty, def)) return false;

    coef[0] = abc[0];
    coef[1] = abc[1];
    coef[2] = abc[2];
    coef[3] = def[0];
    coef[4] = def[1];
    coef[5] = def[2];
    return true;
}

/* --------------------------------------------------------------- targets */

/* Runs one target, retrying in place (same screen, same crosshair) until a
 * stable AND plausible result comes in. has_prev/prev_rx/prev_ry are the
 * previous target's accepted raw average, for the plausibility check. */
static void run_one_target(int idx, bool has_prev, float prev_rx, float prev_ry, float *out_rx,
                           float *out_ry)
{
    lv_obj_t *scr = make_dark_screen("Touch calibration -- tap the target");

    char sub[24];
    snprintf(sub, sizeof(sub), "Target %d of %d", idx + 1, TARGET_COUNT);
    lv_obj_t *sub_label = lv_label_create(scr);
    lv_label_set_text(sub_label, sub);
    lv_obj_set_style_text_color(sub_label, lv_color_hex(0xFFFFFF), 0);
    lv_obj_align(sub_label, LV_ALIGN_TOP_MID, 0, 32);

    lv_obj_t *msg_label = lv_label_create(scr);
    lv_label_set_text(msg_label, "");
    lv_obj_set_style_text_color(msg_label, lv_color_hex(COL_AMBER), 0);
    lv_obj_align(msg_label, LV_ALIGN_TOP_MID, 0, 54);

    lv_obj_t *ring = add_crosshair(scr, idx);
    lv_screen_load_anim(scr, LV_SCREEN_LOAD_ANIM_NONE, 0, 0, true);
    pump(50); /* let the flush land before we start polling touch */

    for(;;) {
        lv_obj_set_style_border_color(ring, lv_color_hex(COL_IDLE), 0);

        uint32_t sum_x = 0, sum_y = 0;
        int n = 0;
        bool showed_amber = false;
        while(n < SAMPLES_PER_TARGET) {
            if(touch_is_pressed_raw() && !showed_amber) {
                lv_obj_set_style_border_color(ring, lv_color_hex(COL_AMBER), 0);
                showed_amber = true;
            }
            uint16_t x, y, z;
            if(touch_acquire_raw_ex(&x, &y, &z, CAL_Z1_MIN, CAL_SPREAD_MAX)) {
                sum_x += x;
                sum_y += y;
                n++;
            }
            lv_timer_handler();
            vTaskDelay(pdMS_TO_TICKS(5));
        }

        float rx = (float)sum_x / (float)n;
        float ry = (float)sum_y / (float)n;

        bool plausible = true;
        if(has_prev) {
            float dx = fabsf(rx - prev_rx), dy = fabsf(ry - prev_ry);
            plausible = (dx > PLAUSIBLE_MIN_DELTA || dy > PLAUSIBLE_MIN_DELTA);
        }

        if(!plausible) {
            ESP_LOGW(TAG,
                     "target %d result implausible: raw=(%.0f,%.0f) too close to the previous "
                     "target's (%.0f,%.0f) — retrying",
                     idx, (double)rx, (double)ry, (double)prev_rx, (double)prev_ry);
            lv_label_set_text(msg_label, "Try again -- press firmly on the center of the target");
            while(touch_is_pressed_raw()) {
                lv_timer_handler();
                vTaskDelay(pdMS_TO_TICKS(5));
            }
            continue;
        }

        lv_label_set_text(msg_label, "");
        lv_obj_set_style_border_color(ring, lv_color_hex(COL_MINT), 0);
        pump(300);

        /* Wait for release before moving to the next target. */
        while(touch_is_pressed_raw()) {
            lv_timer_handler();
            vTaskDelay(pdMS_TO_TICKS(5));
        }

        ESP_LOGI(TAG, "target %d panel=(%ld,%ld): raw avg=(%.0f,%.0f)", idx,
                 (long)s_target_px[idx], (long)s_target_py[idx], (double)rx, (double)ry);
        *out_rx = rx;
        *out_ry = ry;
        return;
    }
}

static void run_targets(float rx[TARGET_COUNT], float ry[TARGET_COUNT])
{
    for(int i = 0; i < TARGET_COUNT; i++) {
        run_one_target(i, i > 0, i > 0 ? rx[i - 1] : 0.0f, i > 0 ? ry[i - 1] : 0.0f, &rx[i],
                       &ry[i]);
    }
}

static void run_tap_test(void)
{
    lv_obj_t *scr = make_dark_screen("Tap anywhere to test (5s)");
    lv_screen_load_anim(scr, LV_SCREEN_LOAD_ANIM_NONE, 0, 0, true);

    float coef[6];
    touch_get_affine(coef);

    int64_t end_us = esp_timer_get_time() + 5000 * 1000;
    while(esp_timer_get_time() < end_us) {
        uint16_t x, y, z;
        if(touch_acquire_raw(&x, &y, &z)) {
            float px = coef[0] * x + coef[1] * y + coef[2];
            float py = coef[3] * x + coef[4] * y + coef[5];

            lv_obj_t *dot = lv_obj_create(scr);
            lv_obj_remove_style_all(dot);
            lv_obj_set_size(dot, 8, 8);
            lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);
            lv_obj_set_style_bg_color(dot, lv_color_hex(0x22C55E), 0);
            lv_obj_set_style_bg_opa(dot, LV_OPA_COVER, 0);
            lv_obj_set_pos(dot, (int32_t)px - 4, (int32_t)py - 4);
        }
        lv_timer_handler();
        vTaskDelay(pdMS_TO_TICKS(5));
    }
}

/* Explicitly tears down every object the calibration overlay created (the
 * currently active screen — a per-target crosshair screen or the tap-test
 * screen — and everything under it: labels, dots, lines), rather than
 * counting on some LATER navigation to auto-delete it. Restores the
 * 320x240/offset display mode, repaints the border the overlay drew over
 * (the app's own screen, at 320x240, never touches that area), loads a
 * neutral blank screen and invalidates it so something is visible and
 * touchable the instant this returns, and only then deletes the old
 * overlay screen. app_main() calls app_init() right after this, which
 * replaces the blank screen with the real home screen. */
static void teardown_calibration_ui(lv_display_t *disp)
{
    lv_obj_t *old = lv_screen_active();

    display_exit_calibration_mode(disp);
    display_clear_panel_bg();

    lv_obj_t *blank = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(blank, lv_color_hex(0x0B0E12), 0);
    lv_obj_set_style_bg_opa(blank, LV_OPA_COVER, 0);
    lv_screen_load(blank);

    if(old && old != blank) lv_obj_delete(old);

    lv_obj_invalidate(lv_screen_active());
    ESP_LOGI(TAG, "calibration UI torn down, app visible");
}

void touch_calib_run(lv_display_t *disp)
{
    ESP_LOGI(TAG, "starting touch calibration");

    /* The affine fit is always computed in true (unflipped) panel space —
     * the crosshair targets are drawn at fixed panel coordinates that
     * assume that orientation. If flip is currently on, suspend it for
     * the duration of the calibration UI and restore it afterward; the
     * flip setting itself is untouched (it lives in platform_display_cfg_t,
     * not here), only its live application to the hardware is. */
    platform_display_cfg_t cfg;
    platform_display_get(&cfg);
    if(cfg.flip) {
        display_set_flip(false);
        touch_set_flip(false);
    }

    display_enter_calibration_mode(disp);

    float rx[TARGET_COUNT], ry[TARGET_COUNT];
    run_targets(rx, ry);

    float coef[6];
    if(!fit_affine(rx, ry, coef)) {
        ESP_LOGE(TAG, "calibration fit was degenerate (collinear samples?) — keeping the "
                      "previous mapping");
        teardown_calibration_ui(disp);
        if(cfg.flip) {
            display_set_flip(true);
            touch_set_flip(true);
        }
        return;
    }

    ESP_LOGI(TAG, "calibration fit: x=%.6f*rx+%.6f*ry+%.3f y=%.6f*rx+%.6f*ry+%.3f", (double)coef[0],
             (double)coef[1], (double)coef[2], (double)coef[3], (double)coef[4],
             (double)coef[5]);

    touch_set_affine(coef);
    touch_calib_store_save(coef);

    run_tap_test();

    teardown_calibration_ui(disp);
    if(cfg.flip) {
        display_set_flip(true);
        touch_set_flip(true);
    }
    ESP_LOGI(TAG, "touch calibration complete");
}

/**
 * @file touch_calib_store.c
 * Also implements this platform's platform_touch_calibrate() hook
 * (declared in src/app/platform.h): the simplest re-run mechanism is to
 * erase the stored fit and reboot, since app_main() already runs the
 * calibration screen whenever NVS has no valid one.
 */
#include "touch_calib_store.h"

#include "app/platform.h"

#include <math.h>
#include <string.h>

#include "esp_log.h"
#include "esp_system.h"
#include "nvs.h"
#include "nvs_flash.h"

#define NVS_NAMESPACE "touch"
/* Bumped from "affine": the corner-rejection bug (fixed corners accepted a
 * stray mid-panel press) could have written a bad fit under the old key.
 * This build ignores that key entirely and recalibrates fresh. */
#define NVS_KEY "affine2"

static const char *TAG = "touch_calib_store";

void touch_calib_store_init(void)
{
    esp_err_t err = nvs_flash_init();
    if(err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);
}

static bool coef_is_sane(const float c[6])
{
    for(int i = 0; i < 6; i++) {
        if(isnan(c[i]) || isinf(c[i]) || fabsf(c[i]) > 1.0e6f) return false;
    }
    /* Degenerate if the linear part's determinant is ~0 (a non-invertible
     * map — every raw point would land on the same line). */
    float det = c[0] * c[4] - c[1] * c[3];
    return fabsf(det) > 1.0e-6f;
}

bool touch_calib_store_load(float coef[6])
{
    nvs_handle_t h;
    if(nvs_open(NVS_NAMESPACE, NVS_READONLY, &h) != ESP_OK) return false;

    size_t len = 6 * sizeof(float);
    esp_err_t err = nvs_get_blob(h, NVS_KEY, coef, &len);
    nvs_close(h);

    if(err != ESP_OK || len != 6 * sizeof(float)) return false;

    if(!coef_is_sane(coef)) {
        ESP_LOGW(TAG, "stored calibration failed the sanity check, ignoring it");
        return false;
    }
    return true;
}

void touch_calib_store_save(const float coef[6])
{
    nvs_handle_t h;
    if(nvs_open(NVS_NAMESPACE, NVS_READWRITE, &h) != ESP_OK) {
        ESP_LOGE(TAG, "nvs_open (rw) failed, calibration not saved");
        return;
    }

    esp_err_t err = nvs_set_blob(h, NVS_KEY, coef, 6 * sizeof(float));
    if(err == ESP_OK) err = nvs_commit(h);
    nvs_close(h);

    if(err != ESP_OK) {
        ESP_LOGE(TAG, "failed to save calibration: %s", esp_err_to_name(err));
    }
    else {
        ESP_LOGI(TAG, "calibration saved to NVS (namespace \"%s\")", NVS_NAMESPACE);
    }
}

void platform_touch_calibrate(void)
{
    nvs_handle_t h;
    if(nvs_open(NVS_NAMESPACE, NVS_READWRITE, &h) == ESP_OK) {
        nvs_erase_key(h, NVS_KEY);
        nvs_commit(h);
        nvs_close(h);
    }
    ESP_LOGI(TAG, "calibration cleared — restarting to recalibrate");
    esp_restart();
}

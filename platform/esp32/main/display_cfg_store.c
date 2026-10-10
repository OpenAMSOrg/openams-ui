/**
 * @file display_cfg_store.c
 */
#include "display_cfg_store.h"

#include "app/platform.h"
#include "display.h"
#include "touch.h"

#include <stdio.h>
#include <string.h>

#include "esp_log.h"
#include "nvs.h"
#include "nvs_flash.h"

#define NVS_NAMESPACE "display"
#define NVS_KEY "cfg"

/* The language lives in the same namespace under its own key, never in the
 * cfg blob: a blob whose size changed would fail the length check in
 * display_cfg_store_init() and reset every stored display setting. */
#define NVS_LANG_KEY "lang"
#define NVS_LANG_LEN 16

static char s_lang[NVS_LANG_LEN];

static const char *TAG = "display_cfg";

static platform_display_cfg_t s_cfg = {
    .brightness_pct = 100,
    .contrast = 0,
    .dim_after_s = 300,
    .off_after_s = 900,
    .flip = false,
};

static bool cfg_is_sane(const platform_display_cfg_t *c)
{
    if(c->brightness_pct < 10 || c->brightness_pct > 100) return false;
    if(c->contrast > 1) return false;
    if(c->off_after_s != 0 && c->dim_after_s != 0 && c->off_after_s < c->dim_after_s) return false;
    return true;
}

static void clamp_cfg(platform_display_cfg_t *c)
{
    if(c->brightness_pct < 10) c->brightness_pct = 10;
    if(c->brightness_pct > 100) c->brightness_pct = 100;
    if(c->contrast > 1) c->contrast = 1;
    /* off must never be shorter than dim, per src/app/platform.h's contract */
    if(c->off_after_s != 0 && c->dim_after_s != 0 && c->off_after_s < c->dim_after_s) {
        c->off_after_s = c->dim_after_s;
    }
}

static void apply(const platform_display_cfg_t *cfg)
{
    display_set_contrast(cfg->contrast);
    display_set_flip(cfg->flip);
    touch_set_flip(cfg->flip);
    display_set_brightness(cfg->brightness_pct, 0);
}

static void save(void)
{
    nvs_handle_t h;
    if(nvs_open(NVS_NAMESPACE, NVS_READWRITE, &h) != ESP_OK) {
        ESP_LOGE(TAG, "nvs_open (rw) failed, display config not saved");
        return;
    }
    esp_err_t err = nvs_set_blob(h, NVS_KEY, &s_cfg, sizeof(s_cfg));
    if(err == ESP_OK) err = nvs_commit(h);
    nvs_close(h);
    if(err != ESP_OK) ESP_LOGE(TAG, "failed to save display config: %s", esp_err_to_name(err));
}

void display_cfg_store_init(void)
{
    esp_err_t err = nvs_flash_init();
    if(err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);

    bool loaded = false;
    nvs_handle_t h;
    if(nvs_open(NVS_NAMESPACE, NVS_READONLY, &h) == ESP_OK) {
        platform_display_cfg_t stored;
        size_t len = sizeof(stored);
        if(nvs_get_blob(h, NVS_KEY, &stored, &len) == ESP_OK && len == sizeof(stored) &&
           cfg_is_sane(&stored)) {
            s_cfg = stored;
            loaded = true;
        }
        len = sizeof(s_lang);
        if(nvs_get_str(h, NVS_LANG_KEY, s_lang, &len) != ESP_OK) s_lang[0] = '\0';
        nvs_close(h);
    }

    ESP_LOGI(TAG, "language %s", s_lang[0] ? s_lang : "(unset)");

    ESP_LOGI(TAG,
             "display config %s: brightness=%u%% contrast=%s dim_after=%us off_after=%us "
             "flip=%s",
             loaded ? "loaded" : "defaults", (unsigned)s_cfg.brightness_pct,
             s_cfg.contrast ? "high" : "standard", (unsigned)s_cfg.dim_after_s,
             (unsigned)s_cfg.off_after_s, s_cfg.flip ? "on" : "off");

    apply(&s_cfg);
    if(!loaded) save(); /* first boot: NVS should always have a valid record from here on */
}

void platform_display_get(platform_display_cfg_t *out)
{
    *out = s_cfg;
}

void platform_display_set(const platform_display_cfg_t *cfg)
{
    platform_display_cfg_t clamped = *cfg;
    clamp_cfg(&clamped);

    if(clamped.brightness_pct != s_cfg.brightness_pct) {
        ESP_LOGI(TAG, "display brightness %u", (unsigned)clamped.brightness_pct);
    }
    if(clamped.contrast != s_cfg.contrast) {
        ESP_LOGI(TAG, "display contrast %s", clamped.contrast ? "high" : "standard");
    }
    if(clamped.dim_after_s != s_cfg.dim_after_s) {
        ESP_LOGI(TAG, "display dim-after %u", (unsigned)clamped.dim_after_s);
    }
    if(clamped.off_after_s != s_cfg.off_after_s) {
        ESP_LOGI(TAG, "display off-after %u", (unsigned)clamped.off_after_s);
    }
    if(clamped.flip != s_cfg.flip) {
        ESP_LOGI(TAG, "display flip %s", clamped.flip ? "on" : "off");
    }

    s_cfg = clamped;
    apply(&s_cfg);
    save();
}

void platform_language_get(char *out, size_t n)
{
    if(!out || n == 0) return;
    strncpy(out, s_lang, n - 1);
    out[n - 1] = '\0';
}

void platform_language_set(const char *code)
{
    nvs_handle_t h;
    esp_err_t err;

    if(!code) return;
    /* app_init() applies the stored language on every boot: an unchanged
     * code costs no flash write. */
    if(strcmp(s_lang, code) == 0) return;
    snprintf(s_lang, sizeof(s_lang), "%s", code);
    ESP_LOGI(TAG, "language %s", s_lang);

    if(nvs_open(NVS_NAMESPACE, NVS_READWRITE, &h) != ESP_OK) {
        ESP_LOGE(TAG, "nvs_open (rw) failed, language not saved");
        return;
    }
    err = nvs_set_str(h, NVS_LANG_KEY, s_lang);
    if(err == ESP_OK) err = nvs_commit(h);
    nvs_close(h);
    if(err != ESP_OK) ESP_LOGE(TAG, "failed to save language: %s", esp_err_to_name(err));
}

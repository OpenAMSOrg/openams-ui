/**
 * @file net_config.c
 * NVS-backed network settings. NVS is initialized by display_cfg_store_init(),
 * which runs before anything here.
 */
#include "net_config.h"

#include <string.h>

#include "esp_log.h"
#include "nvs.h"

#define NVS_NAMESPACE "net"

static const char *TAG = "net_config";

/* Reads one string key into buf (capacity cap), leaving buf[0] = '\0' when the
 * key is missing or shorter than expected. */
static void load_str(nvs_handle_t h, const char *key, char *buf, size_t cap)
{
    size_t len = cap;
    esp_err_t err = nvs_get_str(h, key, buf, &len);
    if(err == ESP_ERR_NVS_NOT_FOUND) {
        buf[0] = '\0';
        return;
    }
    if(err != ESP_OK) {
        ESP_LOGW(TAG, "reading \"%s\" failed: %s", key, esp_err_to_name(err));
        buf[0] = '\0';
    }
}

bool net_config_load(net_config_t *out)
{
    memset(out, 0, sizeof(*out));

    nvs_handle_t h;
    if(nvs_open(NVS_NAMESPACE, NVS_READONLY, &h) != ESP_OK) {
        return false;
    }
    load_str(h, "ssid", out->ssid, sizeof(out->ssid));
    load_str(h, "pass", out->password, sizeof(out->password));
    load_str(h, "url", out->moonraker_url, sizeof(out->moonraker_url));
    load_str(h, "unit", out->bind_unit, sizeof(out->bind_unit));
    nvs_close(h);

    return out->ssid[0] != '\0';
}

bool net_config_save(const net_config_t *cfg)
{
    nvs_handle_t h;
    if(nvs_open(NVS_NAMESPACE, NVS_READWRITE, &h) != ESP_OK) {
        ESP_LOGE(TAG, "nvs_open (rw) failed, network config not saved");
        return false;
    }
    esp_err_t err = nvs_set_str(h, "ssid", cfg->ssid);
    if(err == ESP_OK) err = nvs_set_str(h, "pass", cfg->password);
    if(err == ESP_OK) err = nvs_set_str(h, "url", cfg->moonraker_url);
    if(err == ESP_OK) err = nvs_set_str(h, "unit", cfg->bind_unit);
    if(err == ESP_OK) err = nvs_commit(h);
    nvs_close(h);
    if(err != ESP_OK) {
        ESP_LOGE(TAG, "failed to save network config: %s", esp_err_to_name(err));
        return false;
    }
    return true;
}

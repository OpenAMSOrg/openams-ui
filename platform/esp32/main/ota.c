/**
 * @file ota.c
 * See ota.h. The URL/decision logic itself lives in the portable
 * src/app/ota_policy.c so it can be unit-tested on the host; this file is
 * the ESP-IDF plumbing around it (esp_https_ota, esp_ota_ops).
 */
#include "ota.h"

#include <stdio.h>
#include <string.h>

#include "esp_crt_bundle.h"
#include "esp_err.h"
#include "esp_http_client.h"
#include "esp_https_ota.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "app/ota_policy.h"

static const char *TAG = "ota";

#define ROLLBACK_WIFI_TIMEOUT_US (60LL * 1000000LL)
#define OTA_HTTP_TIMEOUT_MS 15000

static bool s_pending_verify;
static int64_t s_ui_start_us;

void ota_rollback_init(void)
{
    s_ui_start_us = esp_timer_get_time();
    s_pending_verify = false;

    const esp_partition_t *running = esp_ota_get_running_partition();
    esp_ota_img_states_t state;
    if(running && esp_ota_get_state_partition(running, &state) == ESP_OK &&
       state == ESP_OTA_IMG_PENDING_VERIFY) {
        s_pending_verify = true;
        ESP_LOGI(TAG, "running a newly updated image; awaiting rollback confirmation");
    }
}

void ota_rollback_poll(bool wifi_configured, bool wifi_up)
{
    if(!s_pending_verify) return;

    bool confirm = wifi_up ||
                   (!wifi_configured && (esp_timer_get_time() - s_ui_start_us) >= ROLLBACK_WIFI_TIMEOUT_US);
    if(!confirm) return;

    if(esp_ota_mark_app_valid_cancel_rollback() == ESP_OK) {
        ESP_LOGI(TAG, "image confirmed valid; rollback cancelled");
    }
    else {
        ESP_LOGE(TAG, "failed to confirm the running image as valid");
    }
    s_pending_verify = false;
}

static const char *state_str(esp_ota_img_states_t state)
{
    switch(state) {
        case ESP_OTA_IMG_NEW: return "new";
        case ESP_OTA_IMG_PENDING_VERIFY: return "pending verify";
        case ESP_OTA_IMG_VALID: return "valid";
        case ESP_OTA_IMG_INVALID: return "invalid";
        case ESP_OTA_IMG_ABORTED: return "aborted";
        case ESP_OTA_IMG_UNDEFINED: return "undefined";
        default: return "unknown";
    }
}

void ota_cmd_status(void)
{
    const esp_partition_t *running = esp_ota_get_running_partition();
    esp_app_desc_t desc;
    if(running && esp_ota_get_partition_description(running, &desc) == ESP_OK) {
        printf("running: %s, version %s\n", running->label, desc.version);
    }
    else {
        printf("running: unknown\n");
    }

    const esp_partition_t *other = esp_ota_get_next_update_partition(NULL);
    if(!other) {
        printf("other slot: none\n");
        fflush(stdout);
        return;
    }

    esp_ota_img_states_t state;
    const char *st = (esp_ota_get_state_partition(other, &state) == ESP_OK) ? state_str(state) : "unknown";

    esp_app_desc_t other_desc;
    if(esp_ota_get_partition_description(other, &other_desc) == ESP_OK) {
        printf("other slot: %s, %s, version %s\n", other->label, st, other_desc.version);
    }
    else {
        printf("other slot: %s, %s, no image\n", other->label, st);
    }
    fflush(stdout);
}

void ota_cmd_update(const char *url, bool force)
{
    if(!ota_url_ok(url)) {
        printf("error: bad url (need http:// or https://host/...)\n");
        fflush(stdout);
        return;
    }

    /* The layout of an image with the spool tag writer (partitions_tagwriter.csv)
     * has one app partition and the tag library instead of the second slot. */
    if(!esp_ota_get_next_update_partition(NULL)) {
        printf("error: this display's partition table has no second app slot (the spool "
               "tag writer's layout); flash the new image over USB\n");
        fflush(stdout);
        return;
    }

    const esp_partition_t *running = esp_ota_get_running_partition();
    esp_app_desc_t running_desc;
    memset(&running_desc, 0, sizeof(running_desc));
    if(!running || esp_ota_get_partition_description(running, &running_desc) != ESP_OK) {
        printf("error: could not read the running image's header\n");
        fflush(stdout);
        return;
    }

    esp_http_client_config_t http_cfg = {
        .url = url,
        .timeout_ms = OTA_HTTP_TIMEOUT_MS,
        .keep_alive_enable = true,
        /* Ignored for a plain-http url (CONFIG_ESP_HTTPS_OTA_ALLOW_HTTP);
         * for https it verifies the server against ESP-IDF's bundled
         * trusted root CAs (docs/ESP32_OTA.md). */
        .crt_bundle_attach = esp_crt_bundle_attach,
    };
    esp_https_ota_config_t ota_cfg = {
        .http_config = &http_cfg,
    };

    esp_https_ota_handle_t handle = NULL;
    esp_err_t err = esp_https_ota_begin(&ota_cfg, &handle);
    if(err != ESP_OK) {
        printf("error: could not start the download (%s)\n", esp_err_to_name(err));
        fflush(stdout);
        return;
    }

    esp_app_desc_t new_desc;
    err = esp_https_ota_get_img_desc(handle, &new_desc);
    if(err != ESP_OK) {
        printf("error: could not read the image header (%s)\n", esp_err_to_name(err));
        fflush(stdout);
        esp_https_ota_abort(handle);
        return;
    }

    int decision = ota_decide(running_desc.version, new_desc.version, running_desc.project_name,
                               new_desc.project_name, force);
    if(decision == OTA_DECIDE_WRONG_PROJECT) {
        printf("error: image is for \"%s\", this display runs \"%s\"\n", new_desc.project_name,
               running_desc.project_name);
        fflush(stdout);
        esp_https_ota_abort(handle);
        return;
    }
    if(decision == OTA_DECIDE_SAME_VERSION) {
        printf("error: image version \"%s\" matches the running image; use \"ota <url> force\" to reinstall it\n",
               new_desc.version);
        fflush(stdout);
        esp_https_ota_abort(handle);
        return;
    }

    printf("downloading %s (version %s)\n", url, new_desc.version);
    fflush(stdout);

    int last_decile = -1;
    for(;;) {
        err = esp_https_ota_perform(handle);
        if(err != ESP_ERR_HTTPS_OTA_IN_PROGRESS) break;

        int total = esp_https_ota_get_image_size(handle);
        int read = esp_https_ota_get_image_len_read(handle);
        if(total > 0) {
            int decile = (read * 10) / total;
            if(decile != last_decile) {
                printf("  %d%%\n", decile * 10);
                fflush(stdout);
                last_decile = decile;
            }
        }
    }

    if(err != ESP_OK || !esp_https_ota_is_complete_data_received(handle)) {
        printf("error: download failed (%s)\n", esp_err_to_name(err));
        fflush(stdout);
        esp_https_ota_abort(handle);
        return;
    }

    /* Validates the image and, on success, sets it as the boot partition. */
    err = esp_https_ota_finish(handle);
    if(err != ESP_OK) {
        printf("error: image is invalid (%s)\n", esp_err_to_name(err));
        fflush(stdout);
        return;
    }

    printf("ok; rebooting into version %s\n", new_desc.version);
    fflush(stdout);
    vTaskDelay(pdMS_TO_TICKS(50));
    esp_restart();
}

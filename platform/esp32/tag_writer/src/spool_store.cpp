// Copyright (C) 2026 JR Lomas <lomas.jr@gmail.com>
// This file may be distributed under the terms of the GNU GPLv3 license.

#include "spool_store.h"

#include <stdio.h>
#include <string.h>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "nvs.h"

namespace spool {

static const char *TAG = "spool";
static const char *NVS_NAMESPACE = "tagwriter";
static const char *KEY_SPOOL = "spool1";  // bump when bambu::Spool changes
static const char *KEY_MASTER = "master";

static SemaphoreHandle_t lock;
static bambu::Spool current;
static uint32_t current_version;
static char current_date[17];

static void build_date() {
    // __DATE__ is "Mmm dd yyyy", __TIME__ "hh:mm:ss".
    static const char MONTHS[] = "JanFebMarAprMayJunJulAugSepOctNovDec";
    const char *d = __DATE__, *t = __TIME__;
    int month = 1;
    for (int i = 0; i < 12; i++) {
        if (!strncmp(d, MONTHS + i * 3, 3))
            month = i + 1;
    }
    int day = (d[4] == ' ' ? 0 : d[4] - '0') * 10 + (d[5] - '0');
    snprintf(current_date, sizeof(current_date), "%.4s_%02d_%02d_%.2s_%.2s", d + 7, month, day, t,
             t + 3);
}

static bool load_blob(const char *key, void *out, size_t size) {
    nvs_handle_t h;
    if (nvs_open(NVS_NAMESPACE, NVS_READONLY, &h) != ESP_OK)
        return false;
    size_t length = size;
    esp_err_t err = nvs_get_blob(h, key, out, &length);
    nvs_close(h);
    return err == ESP_OK && length == size;
}

static void save_blob(const char *key, const void *data, size_t size) {
    nvs_handle_t h;
    if (nvs_open(NVS_NAMESPACE, NVS_READWRITE, &h) != ESP_OK)
        return;
    esp_err_t err = nvs_set_blob(h, key, data, size);
    if (err == ESP_OK)
        err = nvs_commit(h);
    nvs_close(h);
    if (err != ESP_OK)
        ESP_LOGE(TAG, "saving %s failed: %s", key, esp_err_to_name(err));
}

void init() {
    lock = xSemaphoreCreateMutex();
    build_date();
    if (!load_blob(KEY_SPOOL, &current, sizeof(current))) {
        bambu::default_spool(current);
    } else {
        current.type[sizeof(current.type) - 1] = 0;
        current.detail[sizeof(current.detail) - 1] = 0;
        current.code[sizeof(current.code) - 1] = 0;
    }
}

void get(bambu::Spool &out) {
    xSemaphoreTake(lock, portMAX_DELAY);
    out = current;
    xSemaphoreGive(lock);
}

void set(const bambu::Spool &value) {
    // Hold the lock across the flash write, so two concurrent setters cannot
    // leave `current` and the stored blob disagreeing.
    xSemaphoreTake(lock, portMAX_DELAY);
    current = value;
    current_version++;
    save_blob(KEY_SPOOL, &value, sizeof(value));
    xSemaphoreGive(lock);
}

uint32_t version() {
    xSemaphoreTake(lock, portMAX_DELAY);
    uint32_t v = current_version;
    xSemaphoreGive(lock);
    return v;
}

const char *date() { return current_date; }

void set_date(const char *value) {
    xSemaphoreTake(lock, portMAX_DELAY);
    strncpy(current_date, value, sizeof(current_date) - 1);
    current_date[sizeof(current_date) - 1] = 0;
    xSemaphoreGive(lock);
}

bool load_key(uint8_t key[bambu::MASTER_KEY_SIZE]) {
    return load_blob(KEY_MASTER, key, bambu::MASTER_KEY_SIZE);
}

void save_key(const uint8_t key[bambu::MASTER_KEY_SIZE]) {
    save_blob(KEY_MASTER, key, bambu::MASTER_KEY_SIZE);
}

void clear_key() {
    nvs_handle_t h;
    if (nvs_open(NVS_NAMESPACE, NVS_READWRITE, &h) != ESP_OK)
        return;
    nvs_erase_key(h, KEY_MASTER);
    nvs_commit(h);
    nvs_close(h);
}

}  // namespace spool

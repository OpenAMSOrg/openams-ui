// Copyright (C) 2026 JR Lomas <lomas.jr@gmail.com>
// This file may be distributed under the terms of the GNU GPLv3 license.

#include "taglib.h"

#include <string.h>

#include "esp_log.h"
#include "esp_partition.h"

namespace taglib {

static const char *TAG = "taglib";
static const uint32_t MAGIC = 0x47415442;  // "BTAG"
static const size_t SLOT_SIZE = 4096;      // one 4 KB erase sector per entry

// On-flash record. The header is read on its own when browsing; the image
// follows. Must fit one slot.
struct Record {
    uint32_t magic;
    char material[NAME_LEN];
    char color[NAME_LEN];
    uint8_t uid[4];
    uint8_t reserved[4];
    uint8_t image[1024];
};
static_assert(sizeof(Record) <= SLOT_SIZE, "record must fit a slot");

static const esp_partition_t *part;

void init() {
    part = esp_partition_find_first(ESP_PARTITION_TYPE_DATA,
                                    (esp_partition_subtype_t)0x40, "taglib");
    if (!part)
        ESP_LOGE(TAG, "taglib partition not found");
    else
        ESP_LOGI(TAG, "library: %d slots in %u KB", slot_count(),
                 (unsigned)(part->size / 1024));
}

int slot_count() { return part ? (int)(part->size / SLOT_SIZE) : 0; }

static bool read_header(int slot, Record &r) {
    // Just the fields before the image, for fast browsing.
    size_t head = offsetof(Record, image);
    return part && esp_partition_read(part, (size_t)slot * SLOT_SIZE, &r, head) == ESP_OK &&
           r.magic == MAGIC;
}

static bool read_full(int slot, Record &r) {
    return part && esp_partition_read(part, (size_t)slot * SLOT_SIZE, &r, sizeof(r)) == ESP_OK &&
           r.magic == MAGIC;
}

int count() {
    int n = 0;
    Record r;
    for (int slot = 0; slot < slot_count(); slot++)
        if (read_header(slot, r))
            n++;
    return n;
}

static int find_uid(const uint8_t uid[4]) {
    Record r;
    for (int slot = 0; slot < slot_count(); slot++)
        if (read_header(slot, r) && !memcmp(r.uid, uid, 4))
            return slot;
    return -1;
}

static int first_free() {
    Record r;
    for (int slot = 0; slot < slot_count(); slot++)
        if (!read_header(slot, r))
            return slot;
    return -1;
}

bool add(const char *material, const char *color, const uint8_t image[1024]) {
    if (!part)
        return false;
    int slot = find_uid(image);  // image[0..3] is the UID
    if (slot < 0)
        slot = first_free();
    if (slot < 0)
        return false;

    Record r;
    memset(&r, 0, sizeof(r));
    r.magic = MAGIC;
    strncpy(r.material, material, NAME_LEN - 1);
    strncpy(r.color, color, NAME_LEN - 1);
    memcpy(r.uid, image, 4);
    memcpy(r.image, image, 1024);

    if (esp_partition_erase_range(part, (size_t)slot * SLOT_SIZE, SLOT_SIZE) != ESP_OK)
        return false;
    return esp_partition_write(part, (size_t)slot * SLOT_SIZE, &r, sizeof(r)) == ESP_OK;
}

void clear() {
    if (part)
        esp_partition_erase_range(part, 0, part->size);
}

static void insert_sorted(char out[][NAME_LEN], int &n, int max, const char *name) {
    for (int i = 0; i < n; i++) {
        int cmp = strcmp(name, out[i]);
        if (cmp == 0)
            return;  // already present
        if (cmp < 0) {
            if (n < max) {
                for (int j = (n < max ? n : max - 1); j > i; j--)
                    memcpy(out[j], out[j - 1], NAME_LEN);
                strncpy(out[i], name, NAME_LEN - 1);
                out[i][NAME_LEN - 1] = 0;
                n++;
            }
            return;
        }
    }
    if (n < max) {
        strncpy(out[n], name, NAME_LEN - 1);
        out[n][NAME_LEN - 1] = 0;
        n++;
    }
}

int list_materials(char out[][NAME_LEN], int max) {
    int n = 0;
    Record r;
    for (int slot = 0; slot < slot_count(); slot++)
        if (read_header(slot, r))
            insert_sorted(out, n, max, r.material);
    return n;
}

int list_colors(const char *material, char out[][NAME_LEN], uint16_t *counts, int max) {
    int n = 0;
    Record r;
    // Pass 1: the distinct, sorted colour names.
    for (int slot = 0; slot < slot_count(); slot++)
        if (read_header(slot, r) && !strcmp(r.material, material))
            insert_sorted(out, n, max, r.color);
    // Pass 2: how many entries each has.
    if (counts) {
        for (int i = 0; i < n; i++)
            counts[i] = color_entry_count(material, out[i]);
    }
    return n;
}

int color_entry_count(const char *material, const char *color) {
    int n = 0;
    Record r;
    for (int slot = 0; slot < slot_count(); slot++)
        if (read_header(slot, r) && !strcmp(r.material, material) && !strcmp(r.color, color))
            n++;
    return n;
}

bool load(const char *material, const char *color, int n, uint8_t image[1024], char uid_out[9]) {
    Record r;
    int seen = 0;
    for (int slot = 0; slot < slot_count(); slot++) {
        if (!read_header(slot, r) || strcmp(r.material, material) || strcmp(r.color, color))
            continue;
        if (seen++ != n)
            continue;
        if (!read_full(slot, r))
            return false;
        memcpy(image, r.image, 1024);
        for (int i = 0; i < 4; i++)
            sprintf(uid_out + i * 2, "%02X", r.uid[i]);
        return true;
    }
    return false;
}

}  // namespace taglib

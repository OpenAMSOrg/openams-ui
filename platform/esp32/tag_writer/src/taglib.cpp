// Copyright (C) 2026 JR Lomas <lomas.jr@gmail.com>
// This file may be distributed under the terms of the GNU GPLv3 license.

#include "taglib.h"

#include <stddef.h>
#include <stdio.h>
#include <string.h>

#include "esp_log.h"
#include "esp_partition.h"
#include "esp_task_wdt.h"
#include "sdkconfig.h"

namespace taglib {

static const char *TAG = "taglib";

// Layout (version 2), all little-endian, shared with tools/tagtool.py:
//   0                      superblock, alone in the first 4 KB sector
//   4096 + i * 80          header i
//   4096 + N * 80 + i * 1024  image i
// N, the capacity, is (partition size - 4096) / 1104. An entry is written once
// into erased flash, so adding one erases nothing and needs no sector buffer.
// Entries are appended: the first free header ends the list. A header that is
// replaced (the same UID stored again with new contents) is zeroed, and only
// clear() gives its slot back.
static const uint32_t SUPER_MAGIC = 0x42494C54;  // "TLIB"
static const uint16_t VERSION = 2;
static const uint32_t LIVE = 0x32474154;  // "TAG2": a stored entry
static const uint32_t FREE = 0xFFFFFFFF;  // erased, never written
static const size_t SUPER_SIZE = 4096;
static const size_t IMAGE_SIZE = 1024;
static const uint8_t NEVER_CLONED = 0x01;  // a state bit, cleared on the first clone

struct Super {
    uint32_t magic;
    uint16_t version;
    uint16_t header_size;
    uint32_t capacity;
    uint32_t image_size;
};

struct Header {
    uint32_t magic;
    uint8_t uid[4];
    uint8_t state;
    uint8_t reserved[7];  // 0xFF: free for later flags
    char material[NAME_LEN];
    char color[NAME_LEN];
};
static_assert(sizeof(Header) == 80, "tools/tagtool.py writes 80-byte headers");

static const esp_partition_t *part;
static const Header *headers;  // the header table, mapped (null: read it instead)
static int capacity;  // 0 while the partition is missing or not formatted
static int live;

static size_t header_at(int slot) { return SUPER_SIZE + (size_t)slot * sizeof(Header); }

static size_t image_at(int slot) {
    return SUPER_SIZE + (size_t)capacity * sizeof(Header) + (size_t)slot * IMAGE_SIZE;
}

static int capacity_for(size_t size) {
    return size <= SUPER_SIZE ? 0 : (int)((size - SUPER_SIZE) / (sizeof(Header) + IMAGE_SIZE));
}

// Calls fn(slot, header) for every header up to the first free one, until fn
// returns false. False on a read error. On the 5" board a pass over 6,500
// headers takes about 25 ms through the mapped table, 65 ms read a few at a time.
template <typename Fn> static bool scan(Fn fn) {
    if (headers) {
        for (int slot = 0; slot < capacity; slot++)
            if (headers[slot].magic == FREE || !fn(slot, headers[slot]))
                break;
        return true;
    }
    const int BATCH = 8;  // 640 bytes of stack
    Header batch[BATCH];
    for (int first = 0; first < capacity; first += BATCH) {
        int n = capacity - first < BATCH ? capacity - first : BATCH;
        if (esp_partition_read(part, header_at(first), batch, n * sizeof(Header)) != ESP_OK)
            return false;
        for (int i = 0; i < n; i++)
            if (batch[i].magic == FREE || !fn(first + i, batch[i]))
                return true;
    }
    return true;
}

static bool named(const Header &h, const char *material, const char *color) {
    return h.magic == LIVE && !strncmp(h.material, material, NAME_LEN) &&
           (!color || !strncmp(h.color, color, NAME_LEN));
}

void init() {
    part = esp_partition_find_first(ESP_PARTITION_TYPE_DATA,
                                    (esp_partition_subtype_t)0x40, "taglib");
    if (!part) {
        ESP_LOGE(TAG, "taglib partition not found");
        return;
    }
#if CONFIG_SPIRAM
    // The 5" board's table (7,475 headers) is mapped: a few MMU pages and a
    // few bytes of heap for good, which the 4" board (no PSRAM, no boot-time
    // heap for the tag writer, 1,599 headers) does without. The flash write
    // and erase calls flush the cache over what they change.
    const void *table;
    esp_partition_mmap_handle_t handle;
    if (esp_partition_mmap(part, 0, SUPER_SIZE + capacity_for(part->size) * sizeof(Header),
                           ESP_PARTITION_MMAP_DATA, &table, &handle) == ESP_OK)
        headers = reinterpret_cast<const Header *>(static_cast<const uint8_t *>(table) + SUPER_SIZE);
#endif
    Super s;
    if (esp_partition_read(part, 0, &s, sizeof(s)) == ESP_OK && s.magic == SUPER_MAGIC &&
        s.version == VERSION && s.header_size == sizeof(Header) && s.image_size == IMAGE_SIZE &&
        (int)s.capacity == capacity_for(part->size))
        capacity = (int)s.capacity;
    live = 0;
    scan([](int, const Header &h) {
        live += h.magic == LIVE;
        return true;
    });
    if (!capacity)
        // Erased, or written by an older firmware (one tag per 4 KB slot).
        ESP_LOGW(TAG, "library not formatted: tag libclear yes, or tools/tagtool.py libflash");
    else
        ESP_LOGI(TAG, "library: %d of %d entries in %u KB", live, capacity,
                 (unsigned)(part->size / 1024));
}

bool has_partition() { return part != nullptr; }
int slot_count() { return capacity; }
int count() { return live; }

static bool same_image(int slot, const uint8_t image[1024]) {
    uint8_t chunk[128];
    for (size_t at = 0; at < IMAGE_SIZE; at += sizeof(chunk))
        if (esp_partition_read(part, image_at(slot) + at, chunk, sizeof(chunk)) != ESP_OK ||
            memcmp(chunk, image + at, sizeof(chunk)))
            return false;
    return true;
}

bool add(const char *material, const char *color, const uint8_t image[1024], const char **why) {
    if (!capacity) {
        *why = part ? "the library is not formatted (libclear yes)"
                    : "this image has no tag library partition";
        return false;
    }
    Header h;
    memset(&h, 0, sizeof(h));
    h.magic = LIVE;
    memcpy(h.uid, image, 4);  // image[0..3] is the UID
    h.state = 0xFF;
    memset(h.reserved, 0xFF, sizeof(h.reserved));
    strncpy(h.material, material, NAME_LEN - 1);
    strncpy(h.color, color, NAME_LEN - 1);

    // The new entry goes after the last header written.
    int old = -1, slot = 0;
    uint8_t old_state = 0xFF;
    bool read = scan([&](int at, const Header &o) {
        if (o.magic == LIVE && !memcmp(o.uid, h.uid, 4)) {
            old = at;
            old_state = o.state;
        }
        slot = at + 1;
        return true;
    });
    if (!read) {
        *why = "flash read failed";
        return false;
    }
    if (old >= 0) {
        Header o;
        if (esp_partition_read(part, header_at(old), &o, sizeof(o)) == ESP_OK &&
            named(o, h.material, h.color) && same_image(old, image)) {
            *why = "already stored";
            return true;
        }
        h.state = old_state;  // the same UID: a clone made from it still counts
    }
    if (slot == capacity) {
        *why = "the library is full";
        return false;
    }
    // The image first: a header is only live once its image is there. The
    // copy it replaces is retired last, so a reset in between keeps one.
    if (esp_partition_write(part, image_at(slot), image, IMAGE_SIZE) != ESP_OK ||
        esp_partition_write(part, header_at(slot), &h, sizeof(h)) != ESP_OK) {
        *why = "flash write failed";
        return false;
    }
    if (old >= 0) {
        uint32_t retired = 0;
        esp_partition_write(part, header_at(old), &retired, sizeof(retired));
    } else {
        live++;
    }
    *why = "ok";
    return true;
}

void clear() {
    if (!part)
        return;
    capacity = 0;
    live = 0;
    // A block at a time, so the console's watchdog is fed through a long erase.
    const size_t BLOCK = 64 * 1024;
    for (size_t at = 0; at < part->size; at += BLOCK) {
        size_t n = part->size - at < BLOCK ? part->size - at : BLOCK;
        if (esp_partition_erase_range(part, at, n) != ESP_OK) {
            ESP_LOGE(TAG, "erase failed at 0x%x", (unsigned)at);
            return;
        }
        esp_task_wdt_reset();
    }
    // The superblock last: an interrupted clear leaves the library unformatted.
    Super s = {SUPER_MAGIC, VERSION, (uint16_t)sizeof(Header), (uint32_t)capacity_for(part->size),
               IMAGE_SIZE};
    if (esp_partition_write(part, 0, &s, sizeof(s)) == ESP_OK)
        capacity = (int)s.capacity;
}

// Inserts name into the sorted list out[0..n); returns its index, or -1 when
// the list is full.
static int insert_sorted(char out[][NAME_LEN], int &n, int max, const char *name) {
    int i = 0;
    while (i < n && strncmp(name, out[i], NAME_LEN) > 0)
        i++;
    if (i < n && !strncmp(name, out[i], NAME_LEN))
        return i;
    if (n == max)
        return -1;
    memmove(out[i + 1], out[i], (size_t)(n - i) * NAME_LEN);
    strncpy(out[i], name, NAME_LEN - 1);
    out[i][NAME_LEN - 1] = 0;
    n++;
    return i;
}

int list_materials(char out[][NAME_LEN], int max) {
    int n = 0, last = -1;  // entries of one material tend to come in runs
    scan([&](int, const Header &h) {
        if (h.magic == LIVE && (last < 0 || strncmp(h.material, out[last], NAME_LEN)))
            last = insert_sorted(out, n, max, h.material);
        return true;
    });
    return n;
}

int list_colors(const char *material, char out[][NAME_LEN], uint16_t *counts, uint16_t *fresh,
                int max) {
    int n = 0;
    scan([&](int, const Header &h) {
        if (named(h, material, nullptr))
            insert_sorted(out, n, max, h.color);
        return true;
    });
    if (!counts && !fresh)
        return n;
    for (int i = 0; i < n; i++) {
        if (counts)
            counts[i] = 0;
        if (fresh)
            fresh[i] = 0;
    }
    // The names are sorted and complete: count each entry against its name.
    scan([&](int, const Header &h) {
        if (!named(h, material, nullptr))
            return true;
        for (int i = 0; i < n; i++) {
            if (strncmp(h.color, out[i], NAME_LEN))
                continue;
            if (counts)
                counts[i]++;
            if (fresh && (h.state & NEVER_CLONED))
                fresh[i]++;
            break;
        }
        return true;
    });
    return n;
}

int pick(const char *material, const char *color, char uid_out[9], bool *fresh) {
    int first = -1, unused = -1;
    uint8_t first_uid[4] = {0}, unused_uid[4] = {0};
    scan([&](int slot, const Header &h) {
        if (!named(h, material, color))
            return true;
        if (first < 0) {
            first = slot;
            memcpy(first_uid, h.uid, 4);
        }
        if (h.state & NEVER_CLONED) {
            unused = slot;
            memcpy(unused_uid, h.uid, 4);
            return false;
        }
        return true;
    });
    int slot = unused >= 0 ? unused : first;
    const uint8_t *uid = unused >= 0 ? unused_uid : first_uid;
    if (slot >= 0)
        for (int i = 0; i < 4; i++)
            sprintf(uid_out + i * 2, "%02X", uid[i]);
    if (fresh)
        *fresh = unused >= 0;
    return slot;
}

bool load(int slot, uint8_t image[1024]) {
    Header h;
    return slot >= 0 && slot < capacity &&
           esp_partition_read(part, header_at(slot), &h, sizeof(h)) == ESP_OK && h.magic == LIVE &&
           esp_partition_read(part, image_at(slot), image, IMAGE_SIZE) == ESP_OK;
}

void mark_cloned(int slot) {
    Header h;
    if (slot < 0 || slot >= capacity ||
        esp_partition_read(part, header_at(slot), &h, sizeof(h)) != ESP_OK ||
        h.magic != LIVE || !(h.state & NEVER_CLONED))
        return;
    // Clearing a bit needs no erase.
    uint8_t state = h.state & ~NEVER_CLONED;
    esp_partition_write(part, header_at(slot) + offsetof(Header, state), &state, 1);
}

}  // namespace taglib

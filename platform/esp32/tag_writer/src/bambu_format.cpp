// Copyright (C) 2026 JR Lomas <lomas.jr@gmail.com>
// This file may be distributed under the terms of the GNU GPLv3 license.

#include "bambu_format.h"

#include <string.h>

#include "progmem_compat.h"
#include "sha256.h"

namespace bambu {

const uint8_t ACCESS_WRITABLE[4] = {0xFF, 0x07, 0x80, 0x69};
const uint8_t ACCESS_BAMBU_LOCKED[3] = {0x87, 0x87, 0x87};
const uint8_t DEFAULT_KEY[KEY_SIZE] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

// Codes match OpenAMS GENERIC_FILAMENT_CODES / tray.py MATERIAL_DEFAULTS.
static const Preset PRESETS[] PROGMEM = {
    {"PLA", "PLA Basic", "GFL99", 190, 230, 55, 55, 8, 124},
    {"PLA", "PLA Matte", "GFL99", 190, 230, 55, 55, 8, 132},
    {"PLA", "PLA Silk", "GFL99", 200, 230, 55, 55, 8, 124},
    {"PLA-CF", "PLA-CF", "GFL98", 210, 240, 55, 55, 8, 122},
    {"PETG", "PETG Basic", "GFG99", 220, 260, 70, 65, 8, 127},
    {"PETG-CF", "PETG-CF", "GFG99", 240, 270, 70, 65, 8, 125},
    {"ABS", "ABS", "GFB99", 240, 280, 90, 80, 8, 104},
    {"ASA", "ASA", "GFB98", 240, 280, 90, 80, 8, 107},
    {"TPU", "TPU 95A", "GFU99", 200, 240, 35, 65, 8, 121},
    {"PA", "PA", "GFN99", 260, 300, 100, 80, 12, 114},
    {"PA-CF", "PA-CF", "GFN99", 260, 300, 100, 80, 12, 120},
    {"PC", "PC", "GFC99", 260, 300, 100, 80, 8, 120},
    {"PVA", "PVA", "GFS99", 190, 230, 55, 55, 8, 123},
};

// Block 8 of a genuine tag: X1 camera calibration words, then the float
// the guide labels nozzle diameter (0.2). Copied so our tags look the same.
static const uint8_t BLOCK8[16] PROGMEM = {
    0x88, 0x13, 0x10, 0x0E, 0xE8, 0x03, 0xE8, 0x03,
    0x9A, 0x99, 0x19, 0x3F, 0xCD, 0xCC, 0x4C, 0x3E,
};

static const uint8_t RFID_A_INFO[7] = {'R', 'F', 'I', 'D', '-', 'A', 0};

uint8_t preset_count() { return sizeof(PRESETS) / sizeof(PRESETS[0]); }

void preset_get(uint8_t index, Preset &out) {
    memcpy_P(&out, &PRESETS[index], sizeof(Preset));
}

static void copy_text(char *dst, size_t size, const char *src) {
    memset(dst, 0, size);
    strncpy(dst, src, size - 1);
}

void apply_preset(Spool &spool, uint8_t index) {
    if (index >= preset_count())
        return;
    Preset p;
    preset_get(index, p);
    copy_text(spool.type, sizeof(spool.type), p.type);
    copy_text(spool.detail, sizeof(spool.detail), p.detail);
    copy_text(spool.code, sizeof(spool.code), p.code);
    spool.nozzle_min_c = p.nozzle_min_c;
    spool.nozzle_max_c = p.nozzle_max_c;
    spool.bed_c = p.bed_c;
    spool.dry_c = p.dry_c;
    spool.dry_hours = p.dry_hours;
    spool.density_x100 = p.density_x100;
    spool.length_m = 0;
}

uint8_t preset_match(const Spool &spool) {
    Preset p;
    for (uint8_t i = 0; i < preset_count(); i++) {
        preset_get(i, p);
        if (strcmp(p.detail, spool.detail) == 0)
            return i;
    }
    return 0xFF;
}

void default_spool(Spool &spool) {
    memset(&spool, 0, sizeof(spool));
    apply_preset(spool, 0);
    spool.rgba = 0xFF6A13FF;
    spool.weight_g = 1000;
    spool.diameter_x100 = 175;
    spool.width_x100 = 6625;
}

uint16_t estimate_length_m(const Spool &spool) {
    if (spool.length_m)
        return spool.length_m;
    if (!spool.weight_g || !spool.diameter_x100 || !spool.density_x100)
        return 0;
    float radius_cm = spool.diameter_x100 / 2000.0f;
    float area_cm2 = 3.14159265f * radius_cm * radius_cm;
    float metres = spool.weight_g / (spool.density_x100 / 100.0f * area_cm2) / 100.0f;
    return metres > 65535.0f ? 65535 : (uint16_t)(metres + 0.5f);
}

void derive_keys(const uint8_t master[MASTER_KEY_SIZE], const uint8_t *uid,
                 uint8_t uid_length, uint8_t keys[SECTORS][KEY_SIZE]) {
    hkdf_sha256(master, MASTER_KEY_SIZE, uid, uid_length, RFID_A_INFO,
                sizeof(RFID_A_INFO), &keys[0][0], SECTORS * KEY_SIZE);
}

static void put_u16(uint8_t *out, uint16_t value) {
    out[0] = value & 0xFF;
    out[1] = value >> 8;
}

static uint16_t get_u16(const uint8_t *in) { return in[0] | (in[1] << 8); }

static void put_text(uint8_t *out, size_t size, const char *text) {
    size_t length = strlen(text);
    memcpy(out, text, length < size ? length : size);
}

static void get_text(char *dst, const uint8_t *src, size_t size) {
    size_t i = 0;
    for (; i < size && src[i]; i++)
        dst[i] = (src[i] >= 0x20 && src[i] < 0x7F) ? src[i] : '?';
    dst[i] = 0;
}

void build_block(const Spool &spool, uint8_t block, const uint8_t tray_uid[16],
                 const char *date, uint8_t out[BLOCK_SIZE]) {
    memset(out, 0, BLOCK_SIZE);
    switch (block) {
    case 1:  // material variant ID (unknown for custom spools), material ID
        put_text(out + 8, 8, spool.code);
        break;
    case 2:
        put_text(out, 16, spool.type);
        break;
    case 4:
        put_text(out, 16, spool.detail);
        break;
    case 5: {
        out[0] = spool.rgba >> 24;
        out[1] = spool.rgba >> 16;
        out[2] = spool.rgba >> 8;
        out[3] = spool.rgba;
        put_u16(out + 4, spool.weight_g);
        float diameter = spool.diameter_x100 / 100.0f;
        memcpy(out + 8, &diameter, 4);
        break;
    }
    case 6:
        put_u16(out + 0, spool.dry_c);
        put_u16(out + 2, spool.dry_hours);
        put_u16(out + 4, 1);  // bed temperature type, as on genuine tags
        put_u16(out + 6, spool.bed_c);
        put_u16(out + 8, spool.nozzle_max_c);
        put_u16(out + 10, spool.nozzle_min_c);
        break;
    case 8:
        memcpy_P(out, BLOCK8, BLOCK_SIZE);
        break;
    case 9:
        memcpy(out, tray_uid, BLOCK_SIZE);
        break;
    case 10:
        put_u16(out + 4, spool.width_x100);
        break;
    case 12:
        put_text(out, 16, date);
        break;
    case 14:
        put_u16(out + 4, estimate_length_m(spool));
        break;
    default:
        break;
    }
}

void build_trailer(const uint8_t key_a[KEY_SIZE], uint8_t out[BLOCK_SIZE]) {
    memcpy(out, key_a, KEY_SIZE);
    memcpy(out + 6, ACCESS_WRITABLE, 4);
    memcpy(out + 10, DEFAULT_KEY, KEY_SIZE);
}

void parse_block(uint8_t block, const uint8_t data[BLOCK_SIZE], TagInfo &info) {
    switch (block) {
    case 1:
        get_text(info.variant, data, 8);
        get_text(info.code, data + 8, 8);
        break;
    case 2:
        get_text(info.type, data, 16);
        break;
    case 4:
        get_text(info.detail, data, 16);
        break;
    case 5: {
        info.rgba = ((uint32_t)data[0] << 24) | ((uint32_t)data[1] << 16) |
                    ((uint32_t)data[2] << 8) | data[3];
        info.weight_g = get_u16(data + 4);
        float diameter;
        memcpy(&diameter, data + 8, 4);
        info.diameter_x100 = (diameter > 0.0f && diameter < 10.0f)
                                 ? (uint16_t)(diameter * 100.0f + 0.5f) : 0;
        break;
    }
    case 6:
        info.dry_c = get_u16(data + 0);
        info.dry_hours = get_u16(data + 2);
        info.bed_c = get_u16(data + 6);
        info.nozzle_max_c = get_u16(data + 8);
        info.nozzle_min_c = get_u16(data + 10);
        break;
    case 9:
        memcpy(info.tray_uid, data, 16);
        break;
    case 10:
        info.width_x100 = get_u16(data + 4);
        break;
    case 12:
        get_text(info.production, data, 16);
        break;
    case 14:
        info.length_m = get_u16(data + 4);
        break;
    default:
        break;
    }
}

void tag_to_spool(const TagInfo &info, Spool &spool) {
    Spool fresh;
    default_spool(fresh);
    copy_text(fresh.type, sizeof(fresh.type), info.type);
    copy_text(fresh.detail, sizeof(fresh.detail), info.detail);
    copy_text(fresh.code, sizeof(fresh.code), info.code);
    fresh.rgba = info.rgba;
    if (info.weight_g) fresh.weight_g = info.weight_g;
    if (info.diameter_x100) fresh.diameter_x100 = info.diameter_x100;
    fresh.nozzle_min_c = info.nozzle_min_c;
    fresh.nozzle_max_c = info.nozzle_max_c;
    fresh.bed_c = info.bed_c;
    fresh.dry_c = info.dry_c;
    fresh.dry_hours = info.dry_hours;
    if (info.width_x100) fresh.width_x100 = info.width_x100;
    // Keep the tag's length only if it disagrees with our estimate, so a
    // later weight edit still recomputes it.
    Preset p;
    for (uint8_t i = 0; i < preset_count(); i++) {
        preset_get(i, p);
        if (strcmp(p.type, fresh.type) == 0) {
            fresh.density_x100 = p.density_x100;
            break;
        }
    }
    fresh.length_m = 0;
    if (info.length_m && info.length_m != estimate_length_m(fresh))
        fresh.length_m = info.length_m;
    spool = fresh;
}

}  // namespace bambu

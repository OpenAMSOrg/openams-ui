// Copyright (C) 2026 JR Lomas <lomas.jr@gmail.com>
// This file may be distributed under the terms of the GNU GPLv3 license.

// Bambu Lab MIFARE Classic 1K filament tag layout.
//
// Block map and field encodings follow the Bambu Research Group RFID tag
// guide and the OpenAMS host decoder (bambu_lab_tag_processor.py), which
// reads blocks 2, 4, 5, 6, 8, 9, 10, 12 and 14 with the HKDF-derived key A.
// Portable: builds for the AVR target and for the host-side tests.
#pragma once

#include <stdint.h>

namespace bambu {

const uint8_t BLOCK_SIZE = 16;
const uint8_t SECTORS = 16;
const uint8_t KEY_SIZE = 6;
const uint8_t MASTER_KEY_SIZE = 16;
const uint8_t MAX_UID = 10;

// Sector trailers we write: transport access bits (every block read/write
// with key A) so the tag can be rewritten later. Genuine tags use 87 87 87.
extern const uint8_t ACCESS_WRITABLE[4];
// Genuine Bambu tags: data blocks read-only, keys frozen.
extern const uint8_t ACCESS_BAMBU_LOCKED[3];
extern const uint8_t DEFAULT_KEY[KEY_SIZE];

struct Spool {
    char type[16];      // block 2, e.g. "PLA"
    char detail[16];    // block 4, e.g. "PLA Matte"
    char code[8];       // block 1 material ID, e.g. "GFL99"
    uint32_t rgba;      // block 5, 0xRRGGBBAA
    uint16_t weight_g;  // block 5
    uint16_t diameter_x100;  // block 5 (float on the tag)
    uint16_t nozzle_min_c;   // block 6
    uint16_t nozzle_max_c;   // block 6
    uint16_t bed_c;          // block 6
    uint16_t dry_c;          // block 6
    uint16_t dry_hours;      // block 6
    uint16_t density_x100;   // not stored; used to derive the length
    uint16_t width_x100;     // block 10, spool width in mm*100
    uint16_t length_m;       // block 14; 0 = derive from weight
};

struct Preset {
    char type[10];
    char detail[14];
    char code[6];
    uint16_t nozzle_min_c, nozzle_max_c, bed_c, dry_c, dry_hours;
    uint16_t density_x100;
};

uint8_t preset_count();
void preset_get(uint8_t index, Preset &out);
// Applies a material preset, keeping colour, weight, diameter and width.
void apply_preset(Spool &spool, uint8_t index);
// Index of the preset whose detail name matches the spool, or 0xFF.
uint8_t preset_match(const Spool &spool);
void default_spool(Spool &spool);

// Filament length from weight, diameter and density (metres).
uint16_t estimate_length_m(const Spool &spool);

// The 16 key-A sector keys: HKDF-SHA256(salt=master, ikm=uid,
// info="RFID-A\0"). The whole UID is the IKM, as the host decoder does.
void derive_keys(const uint8_t master[MASTER_KEY_SIZE], const uint8_t *uid,
                 uint8_t uid_length, uint8_t keys[SECTORS][KEY_SIZE]);

// Data-block contents for `block` (never block 0 or a trailer). The tray
// UID (block 9) and production date (block 12) come from `tray_uid` and
// `date` ("YYYY_MM_DD_HH_MM").
void build_block(const Spool &spool, uint8_t block, const uint8_t tray_uid[16],
                 const char *date, uint8_t out[BLOCK_SIZE]);

void build_trailer(const uint8_t key_a[KEY_SIZE], uint8_t out[BLOCK_SIZE]);

inline bool is_trailer(uint8_t block) { return (block & 3) == 3; }

// Fields the OpenAMS decoder reports, parsed back from tag blocks.
struct TagInfo {
    char type[17];
    char detail[17];
    char code[9];
    char variant[9];
    char production[17];
    uint32_t rgba;
    uint16_t weight_g;
    uint16_t diameter_x100;
    uint16_t dry_c, dry_hours, bed_c, nozzle_max_c, nozzle_min_c;
    uint16_t width_x100;
    uint16_t length_m;
    uint8_t tray_uid[16];
};

// Feeds one block into `info` (blocks the layout does not use are ignored).
void parse_block(uint8_t block, const uint8_t data[BLOCK_SIZE], TagInfo &info);
void tag_to_spool(const TagInfo &info, Spool &spool);

}  // namespace bambu

// Copyright (C) 2026 JR Lomas <lomas.jr@gmail.com>
// This file may be distributed under the terms of the GNU GPLv3 license.

// On-device library of genuine tag images, for cloning from the touchscreen
// without a PC or a physical source tag. Stored in the `taglib` flash
// partition, one tag per 4 KB sector (592 slots). Each slot holds the spool's
// material/colour names (for browsing) and the full 1024-byte MIFARE image.
#pragma once

#include <stdint.h>

namespace taglib {

const uint8_t NAME_LEN = 28;

struct Entry {
    char material[NAME_LEN];  // e.g. "PLA Basic"
    char color[NAME_LEN];     // e.g. "Black"
    uint8_t uid[4];
    uint8_t image[1024];
};

// Finds the partition. Call once at boot.
void init();

int slot_count();   // total slots (populated or not)
int count();        // populated entries

// Stores `image` (a validated Bambu image) under material/color. Replaces the
// slot with the same UID, else uses a free slot. False if the library is full.
bool add(const char *material, const char *color, const uint8_t image[1024]);
void clear();

// Distinct material names present, sorted. Returns how many were written.
int list_materials(char out[][NAME_LEN], int max);
// Distinct colours for one material, sorted, each with its entry count.
int list_colors(const char *material, char out[][NAME_LEN], uint16_t *counts, int max);

// Loads the image of the n-th entry matching material+color (n for picking
// among duplicate identities). uid_out (9 bytes) gets the UID hex. False if
// there is no such entry.
bool load(const char *material, const char *color, int n, uint8_t image[1024], char uid_out[9]);
int color_entry_count(const char *material, const char *color);

}  // namespace taglib

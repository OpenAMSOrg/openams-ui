// Copyright (C) 2026 JR Lomas <lomas.jr@gmail.com>
// This file may be distributed under the terms of the GNU GPLv3 license.

// The library of genuine tag images, for cloning from the touchscreen without
// a PC or a physical source tag. One of two implementations is built
// (MMU_TAG_LIBRARY):
// - taglib_remote.cpp (TW_TAGLIB_REMOTE, the default): served by OpenAMS on
//   the printer host, its openams_taglib Moonraker component, over HTTP.
// - taglib.cpp: the on-device `taglib` flash partition (5" board only): a
//   superblock sector, one 80-byte header per entry (the spool's
//   material/colour names, for browsing), then one 1024-byte MIFARE image per
//   entry, 7,475 of them. tools/tagtool.py libflash builds the same layout on
//   a PC.
// The remote one blocks for a network round trip, at most a few seconds when
// the printer does not answer.
#pragma once

#include <stdint.h>

namespace taglib {

const uint8_t NAME_LEN = 32;

// Finds the partition and checks its superblock. Call once at boot.
void init();
// The display's Moonraker WebSocket URL ("ws://host:port/websocket"), which
// says where OpenAMS is; empty when none is stored. Ignored on the device.
void set_service(const char *ws_url);

bool has_partition();
int slot_count();   // entries the library holds; 0 until it is formatted
// Stored entries; remote: as OpenAMS last said, -1 before it was first asked.
int count();
// Asks the library how it is (remote: one request), so count() and
// status_text() are current. False when it could not be asked.
bool refresh();
// Why the library came back empty or a call failed, for the screen.
const char *status_text();

// Stores `image` (a validated Bambu image) under material/color. A UID already
// stored is replaced (or kept as it is when nothing changed); a new one takes
// the next free slot. False, with the reason, if it could not be stored.
bool add(const char *material, const char *color, const uint8_t image[1024], const char **why);
// Erases and formats the library (a few seconds per MB of partition).
void clear();

// Distinct material names present, sorted. Returns how many were written.
int list_materials(char out[][NAME_LEN], int max);
// Distinct colours for one material, sorted, each with its entry count and how
// many of those have never been cloned (counts / fresh may be null).
int list_colors(const char *material, char out[][NAME_LEN], uint16_t *counts, uint16_t *fresh,
                int max);

// The entry to clone next for material+color: the first one never cloned,
// else the first one. uid_out (9 bytes) gets its UID hex; fresh says which.
// -1 if there is no such entry.
int pick(const char *material, const char *color, char uid_out[9], bool *fresh);
// Reads one entry's image. False on a bad slot or a read error.
bool load(int slot, uint8_t image[1024]);
// Records that an entry has been cloned, so pick() moves on to another UID.
void mark_cloned(int slot);

}  // namespace taglib

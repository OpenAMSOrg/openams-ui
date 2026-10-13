// Copyright (C) 2026 JR Lomas <lomas.jr@gmail.com>
// This file may be distributed under the terms of the GNU GPLv3 license.

// The spool being edited (shared by the UI and the console) and the
// settings kept in NVS: that spool, the Bambu master key.
#pragma once

#include <stdint.h>

#include "bambu_format.h"

namespace spool {

// Loads the saved spool (or the default). NVS must already be up.
void init();
void get(bambu::Spool &out);
// Replaces the spool and saves it.
void set(const bambu::Spool &value);
// Changes on every set(), so a screen can tell its copy is stale.
uint32_t version();

// Production date written to block 12 ("YYYY_MM_DD_HH_MM"); the build time
// unless the console sets it (no RTC on this board).
const char *date();
void set_date(const char *value);

bool load_key(uint8_t key[bambu::MASTER_KEY_SIZE]);
void save_key(const uint8_t key[bambu::MASTER_KEY_SIZE]);
void clear_key();

}  // namespace spool

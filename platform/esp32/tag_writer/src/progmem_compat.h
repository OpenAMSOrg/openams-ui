// Copyright (C) 2026 JR Lomas <lomas.jr@gmail.com>
// This file may be distributed under the terms of the GNU GPLv3 license.

// Lets the portable modules keep their tables in flash on AVR and still
// build unchanged for the host-side tests.
#pragma once

#if defined(__AVR__)
#include <avr/pgmspace.h>
#else
#include <string.h>
#define PROGMEM
#define pgm_read_byte(addr) (*(const uint8_t *)(addr))
#define pgm_read_word(addr) (*(const uint16_t *)(addr))
#define pgm_read_dword(addr) (*(const uint32_t *)(addr))
#define memcpy_P memcpy
#define strncpy_P strncpy
#endif

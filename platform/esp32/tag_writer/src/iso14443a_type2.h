// Copyright (C) 2026 JR Lomas <lomas.jr@gmail.com>
// This file may be distributed under the terms of the GNU GPLv3 license.

// ISO/IEC 14443A Type 2 (NTAG, Ultralight) protocol implementation.
#pragma once

#include <stdint.h>
#include <stddef.h>
#include <vector>
#include <string>
#include "pn5180.h"
#include "tag.h"

static const size_t MAX_TYPE2_PAGES = 231;

class Iso14443aType2 {
public:
    explicit Iso14443aType2(Pn5180 &chip) : chip_(chip) {}

    // RF field management
    void field_on();
    void field_off();

    // READ (0x30): 16 bytes starting at page (4 pages)
    std::vector<uint8_t> read_pages(uint8_t page);
    Pn5180::Status read_pages(uint8_t page, uint8_t out[16]);

    // FAST_READ (0x3A): at most 12 pages, returns 4 * (end - start + 1) bytes
    std::vector<uint8_t> fast_read(uint8_t start, uint8_t end);
    Pn5180::Status fast_read(uint8_t start, uint8_t end, uint8_t *out, size_t *out_len);

    // WRITE (0xA2): 4 bytes to page
    void write_page(uint8_t page, const uint8_t data[4]);
    void write_page(uint8_t page, const std::vector<uint8_t> &data);

    // Classification & memory geometry
    void classify_type2(Tag &tag);
    void geom_type2(Tag &tag, size_t pages, int user_last = -1);

    // Re-select / reactivate tag after halt/auth-fail
    bool reactivate(Tag &tag);

    // High-level full read, write, verify
    void read_tag(Tag &tag);
    void write_blocks(Tag &tag, const std::vector<BlockWrite> &writes);
    void verify_blocks(Tag &tag, const std::vector<BlockWrite> &writes);

private:
    Pn5180 &chip_;
};

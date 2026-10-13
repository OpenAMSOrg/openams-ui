// Copyright (C) 2026 JR Lomas <lomas.jr@gmail.com>
// This file may be distributed under the terms of the GNU GPLv3 license.

// ISO/IEC 15693 (NFC-V, NFC Forum Type 5) protocol implementation.
#pragma once

#include <stdint.h>
#include <stddef.h>
#include <vector>
#include <string>
#include <memory>
#include "pn5180.h"
#include "tag.h"

// RF config for ISO15693 high data rate (26.48 kbps)
static const uint8_t RF_ISO15693_TX = 0x0D;
static const uint8_t RF_ISO15693_RX = 0x8D;

class Iso15693Error : public TagError {
public:
    int code;
    explicit Iso15693Error(const std::string &msg, int code = -1)
        : TagError(msg), code(code) {}
};

TagType classify_icode(const uint8_t *uid8);
TagType classify_icode(const std::vector<uint8_t> &uid);
void known_size(TagType tag_type, size_t &nblocks, size_t &bsize, int &user_last);

class Iso15693 {
public:
    explicit Iso15693(Pn5180 &chip) : chip_(chip) {}

    // RF field management
    void field_on();
    void field_off();

    // Inventory: single-slot inventory (returns allocated Tag or nullptr)
    std::unique_ptr<Tag> inventory();

    // System info: fills dsfid, afi, ic_ref, memory geometry
    void get_system_info(Tag &tag);

    // Block reading (supports read multiple with single read fallback)
    std::vector<uint8_t> read_blocks(Tag &tag, size_t first, size_t count);
    std::vector<uint8_t> read_single(const uint8_t uid_lsb[8], size_t block, size_t bs);

    // Single block writing
    void write_block(Tag &tag, size_t block, const uint8_t *data, size_t len);
    void write_block(Tag &tag, size_t block, const std::vector<uint8_t> &data);

    // Full tag read (including Type 5 capability container sizing rules)
    void read_tag(Tag &tag);

    // Validated write planning and read-back verification
    void write_blocks(Tag &tag, const std::vector<BlockWrite> &writes);
    void verify_blocks(Tag &tag, const std::vector<BlockWrite> &writes);

private:
    Pn5180 &chip_;

    std::pair<std::vector<uint8_t>, bool> read_multiple(
        Tag &tag, const uint8_t uid_lsb[8], size_t first, size_t n, size_t bs);
};

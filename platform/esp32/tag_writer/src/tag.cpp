// Copyright (C) 2026 JR Lomas <lomas.jr@gmail.com>
// This file may be distributed under the terms of the GNU GPLv3 license.

// Tag data model implementation matching tag.py.
#include "tag.h"
#include <stdio.h>
#include <string.h>
#include <algorithm>

const char *tag_technology_to_string(TagTechnology tech) {
    switch (tech) {
        case TagTechnology::ISO14443A: return TagTech::ISO14443A;
        case TagTechnology::ISO15693:  return TagTech::ISO15693;
        default: return "unknown";
    }
}

TagTechnology tag_technology_from_string(const std::string &str) {
    if (str == TagTech::ISO14443A) return TagTechnology::ISO14443A;
    if (str == TagTech::ISO15693)  return TagTechnology::ISO15693;
    return TagTechnology::UNKNOWN;
}

const char *tag_type_to_string(TagType type) {
    switch (type) {
        case TagType::NTAG213:           return TagTypes::NTAG213;
        case TagType::NTAG215:           return TagTypes::NTAG215;
        case TagType::NTAG216:           return TagTypes::NTAG216;
        case TagType::ULTRALIGHT:        return TagTypes::ULTRALIGHT;
        case TagType::ULTRALIGHT_EV1:    return TagTypes::ULTRALIGHT_EV1;
        case TagType::TYPE2:             return TagTypes::TYPE2;
        case TagType::MIFARE_MINI:       return TagTypes::MIFARE_MINI;
        case TagType::MIFARE_CLASSIC_1K: return TagTypes::MIFARE_CLASSIC_1K;
        case TagType::MIFARE_CLASSIC_4K: return TagTypes::MIFARE_CLASSIC_4K;
        case TagType::ISO14443_4:        return TagTypes::ISO14443_4;
        case TagType::ICODE_SLIX:        return TagTypes::ICODE_SLIX;
        case TagType::ICODE_SLIX2:       return TagTypes::ICODE_SLIX2;
        case TagType::ICODE_SLIX_S:      return TagTypes::ICODE_SLIX_S;
        case TagType::ICODE_SLIX_L:      return TagTypes::ICODE_SLIX_L;
        case TagType::ICODE_DNA:         return TagTypes::ICODE_DNA;
        case TagType::TYPE5:             return TagTypes::TYPE5;
        default:                         return "unknown";
    }
}

TagType tag_type_from_string(const std::string &str) {
    if (str == TagTypes::NTAG213)           return TagType::NTAG213;
    if (str == TagTypes::NTAG215)           return TagType::NTAG215;
    if (str == TagTypes::NTAG216)           return TagType::NTAG216;
    if (str == TagTypes::ULTRALIGHT)        return TagType::ULTRALIGHT;
    if (str == TagTypes::ULTRALIGHT_EV1)    return TagType::ULTRALIGHT_EV1;
    if (str == TagTypes::TYPE2)             return TagType::TYPE2;
    if (str == TagTypes::MIFARE_MINI)       return TagType::MIFARE_MINI;
    if (str == TagTypes::MIFARE_CLASSIC_1K) return TagType::MIFARE_CLASSIC_1K;
    if (str == TagTypes::MIFARE_CLASSIC_4K) return TagType::MIFARE_CLASSIC_4K;
    if (str == TagTypes::ISO14443_4)        return TagType::ISO14443_4;
    if (str == TagTypes::ICODE_SLIX)        return TagType::ICODE_SLIX;
    if (str == TagTypes::ICODE_SLIX2)       return TagType::ICODE_SLIX2;
    if (str == TagTypes::ICODE_SLIX_S)      return TagType::ICODE_SLIX_S;
    if (str == TagTypes::ICODE_SLIX_L)      return TagType::ICODE_SLIX_L;
    if (str == TagTypes::ICODE_DNA)         return TagType::ICODE_DNA;
    if (str == TagTypes::TYPE5)             return TagType::TYPE5;
    return TagType::UNKNOWN;
}

bool is_type2_tag(TagType type) {
    return type == TagType::NTAG213 ||
           type == TagType::NTAG215 ||
           type == TagType::NTAG216 ||
           type == TagType::ULTRALIGHT ||
           type == TagType::ULTRALIGHT_EV1 ||
           type == TagType::TYPE2;
}

bool is_mifare_classic_tag(TagType type) {
    return type == TagType::MIFARE_MINI ||
           type == TagType::MIFARE_CLASSIC_1K ||
           type == TagType::MIFARE_CLASSIC_4K;
}

bool is_type5_tag(TagType type) {
    return type == TagType::ICODE_SLIX ||
           type == TagType::ICODE_SLIX2 ||
           type == TagType::ICODE_SLIX_S ||
           type == TagType::ICODE_SLIX_L ||
           type == TagType::ICODE_DNA ||
           type == TagType::TYPE5;
}

Tag::Tag(TagTechnology tech, const std::vector<uint8_t> &uid_bytes, TagType type)
    : technology(tech),
      uid(uid_bytes),
      tag_type(type),
      has_atqa(false),
      has_sak(false),
      sak(0),
      has_dsfid(false),
      dsfid(0),
      has_afi(false),
      afi(0),
      has_ic_ref(false),
      ic_ref(0),
      no_read_multiple(false),
      block_size(4),
      block_count(0),
      user_first_block(0),
      user_last_block(-1),
      key_source("") {
    atqa[0] = 0;
    atqa[1] = 0;
}

Tag::Tag(TagTechnology tech, const uint8_t *uid_data, size_t uid_len, TagType type)
    : technology(tech),
      uid(uid_data ? std::vector<uint8_t>(uid_data, uid_data + uid_len) : std::vector<uint8_t>()),
      tag_type(type),
      has_atqa(false),
      has_sak(false),
      sak(0),
      has_dsfid(false),
      dsfid(0),
      has_afi(false),
      afi(0),
      has_ic_ref(false),
      ic_ref(0),
      no_read_multiple(false),
      block_size(4),
      block_count(0),
      user_first_block(0),
      user_last_block(-1),
      key_source("") {
    atqa[0] = 0;
    atqa[1] = 0;
}

std::string Tag::uid_hex() const {
    std::string out;
    char buf[4];
    for (uint8_t b : uid) {
        snprintf(buf, sizeof(buf), "%02x", b);
        out += buf;
    }
    return out;
}

std::string Tag::nfc_uid() const {
    return uid_hex();
}

std::string Tag::nfc_id() const {
    std::string out;
    char buf[16];
    for (size_t i = 0; i < uid.size(); ++i) {
        if (i > 0) out += ':';
        snprintf(buf, sizeof(buf), "%x", uid[i]);
        out += buf;
    }
    return out;
}

void Tag::alloc_memory(size_t bsize, size_t bcount) {
    block_size = bsize;
    block_count = bcount;
    memory.assign(bsize * bcount, 0);
    readable.assign(bcount, false);
}

void Tag::set_block(size_t block, const uint8_t *data, size_t len) {
    if (len != block_size) {
        char err[64];
        snprintf(err, sizeof(err), "block %zu: expected %zu bytes, got %zu",
                 block, block_size, len);
        throw TagError(err);
    }
    if (block >= block_count) {
        char err[64];
        snprintf(err, sizeof(err), "block %zu >= block_count %zu", block, block_count);
        throw TagError(err);
    }
    size_t start = block * block_size;
    memcpy(&memory[start], data, block_size);
    readable[block] = true;
}

void Tag::set_block(size_t block, const std::vector<uint8_t> &data) {
    set_block(block, data.data(), data.size());
}

std::vector<uint8_t> Tag::get_block(size_t block) const {
    if (block >= block_count) {
        char err[64];
        snprintf(err, sizeof(err), "block %zu >= block_count %zu", block, block_count);
        throw TagError(err);
    }
    size_t start = block * block_size;
    return std::vector<uint8_t>(memory.data() + start, memory.data() + start + block_size);
}

bool Tag::get_block(size_t block, uint8_t *out, size_t out_len) const {
    if (block >= block_count || out_len < block_size) {
        return false;
    }
    size_t start = block * block_size;
    memcpy(out, memory.data() + start, block_size);
    return true;
}

std::vector<uint8_t> Tag::get_bytes(size_t offset, size_t length) const {
    if (offset > memory.size() || length > memory.size() - offset) {
        throw TagError("get_bytes: offset + length out of range");
    }
    return std::vector<uint8_t>(memory.data() + offset, memory.data() + offset + length);
}

size_t Tag::user_capacity() const {
    if (user_last_block < user_first_block) {
        return 0;
    }
    return static_cast<size_t>(user_last_block - user_first_block + 1) * block_size;
}

std::vector<BlockWrite> Tag::plan_writes(
    const std::vector<std::pair<size_t, std::vector<uint8_t>>> &patches) const {
    std::vector<uint8_t> image = memory;
    std::vector<uint8_t> covered(image.size(), 0);
    std::set<size_t> touched;

    for (const auto &p : patches) {
        size_t offset = p.first;
        const auto &data = p.second;
        if (data.empty()) {
            continue;
        }
        size_t end = offset + data.size();
        if (end > image.size()) {
            throw TagWriteError("write outside tag memory");
        }
        memcpy(&image[offset], data.data(), data.size());
        memset(&covered[offset], 1, data.size());
        size_t first = offset / block_size;
        size_t last = (end - 1) / block_size;
        for (size_t b = first; b <= last; ++b) {
            touched.insert(b);
        }
    }

    std::vector<BlockWrite> writes;
    for (size_t block : touched) {
        if (static_cast<int>(block) < user_first_block ||
            static_cast<int>(block) > user_last_block) {
            char err[64];
            snprintf(err, sizeof(err), "block %zu is outside the user area", block);
            throw TagWriteError(err);
        }
        size_t start = block * block_size;
        size_t end = start + block_size;
        std::vector<uint8_t> new_data(&image[start], &image[end]);

        if (!readable[block]) {
            // Unknown current content: only a full overwrite is safe
            bool has_uncovered = false;
            for (size_t i = start; i < end; ++i) {
                if (covered[i] == 0) {
                    has_uncovered = true;
                    break;
                }
            }
            if (has_uncovered) {
                char err[64];
                snprintf(err, sizeof(err), "block %zu was not read", block);
                throw TagWriteError(err);
            }
        } else {
            auto current = get_block(block);
            if (new_data == current) {
                continue;
            }
        }
        writes.push_back({block, new_data});
    }
    return writes;
}

TagDescription Tag::describe() const {
    TagDescription desc;
    desc.technology = tag_technology_to_string(technology);
    desc.tag_type = tag_type_to_string(tag_type);
    desc.uid = uid_hex();
    desc.nfc_uid = nfc_uid();
    desc.nfc_id = nfc_id();
    desc.block_size = block_size;
    desc.block_count = block_count;
    desc.user_bytes = user_capacity();
    return desc;
}

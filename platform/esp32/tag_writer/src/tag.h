// Copyright (C) 2026 JR Lomas <lomas.jr@gmail.com>
// This file may be distributed under the terms of the GNU GPLv3 license.

// Tag data model shared by the protocol layers and format codecs.
#pragma once

#include <stdint.h>
#include <stddef.h>
#include <string>
#include <vector>
#include <map>
#include <set>
#include <stdexcept>
#include <utility>

// Air interfaces (technology)
namespace TagTech {
    static const char * const ISO14443A = "iso14443a";
    static const char * const ISO15693  = "iso15693";
}

enum class TagTechnology {
    UNKNOWN,
    ISO14443A,
    ISO15693,
};

// Tag types (tag_type)
namespace TagTypes {
    static const char * const NTAG213           = "ntag213";
    static const char * const NTAG215           = "ntag215";
    static const char * const NTAG216           = "ntag216";
    static const char * const ULTRALIGHT        = "mifare_ultralight";
    static const char * const ULTRALIGHT_EV1    = "mifare_ultralight_ev1";
    static const char * const TYPE2             = "nfc_type2";
    static const char * const MIFARE_MINI       = "mifare_mini";
    static const char * const MIFARE_CLASSIC_1K = "mifare_classic_1k";
    static const char * const MIFARE_CLASSIC_4K = "mifare_classic_4k";
    static const char * const ISO14443_4        = "iso14443_4";
    static const char * const ICODE_SLIX        = "icode_slix";
    static const char * const ICODE_SLIX2       = "icode_slix2";
    static const char * const ICODE_SLIX_S      = "icode_slix_s";
    static const char * const ICODE_SLIX_L      = "icode_slix_l";
    static const char * const ICODE_DNA         = "icode_dna";
    static const char * const TYPE5             = "nfc_type5";
}

enum class TagType {
    UNKNOWN,
    NTAG213,
    NTAG215,
    NTAG216,
    ULTRALIGHT,
    ULTRALIGHT_EV1,
    TYPE2,
    MIFARE_MINI,
    MIFARE_CLASSIC_1K,
    MIFARE_CLASSIC_4K,
    ISO14443_4,
    ICODE_SLIX,
    ICODE_SLIX2,
    ICODE_SLIX_S,
    ICODE_SLIX_L,
    ICODE_DNA,
    TYPE5,
};

const char *tag_technology_to_string(TagTechnology tech);
TagTechnology tag_technology_from_string(const std::string &str);

const char *tag_type_to_string(TagType type);
TagType tag_type_from_string(const std::string &str);

bool is_type2_tag(TagType type);
bool is_mifare_classic_tag(TagType type);
bool is_type5_tag(TagType type);

// Exception hierarchy matching tag.py
class TagError : public std::runtime_error {
public:
    explicit TagError(const std::string &msg) : std::runtime_error(msg) {}
};

class NoTagError : public TagError {
public:
    explicit NoTagError(const std::string &msg = "no tag answered") : TagError(msg) {}
};

class CollisionError : public TagError {
public:
    explicit CollisionError(const std::string &msg = "collision") : TagError(msg) {}
};

class AuthError : public TagError {
public:
    explicit AuthError(const std::string &msg = "auth failed") : TagError(msg) {}
};

class TagWriteError : public TagError {
public:
    explicit TagWriteError(const std::string &msg) : TagError(msg) {}
};

struct SectorKey {
    uint8_t key[6];
    bool key_b;
};

struct TagDescription {
    std::string technology;
    std::string tag_type;
    std::string uid;
    std::string nfc_uid;
    std::string nfc_id;
    size_t block_size;
    size_t block_count;
    size_t user_bytes;
};

struct BlockWrite {
    size_t block;
    std::vector<uint8_t> data;

    bool operator==(const BlockWrite &other) const {
        return block == other.block && data == other.data;
    }
};

class Tag {
public:
    TagTechnology technology;
    std::vector<uint8_t> uid;
    TagType tag_type;

    // ISO14443A
    bool has_atqa;
    uint8_t atqa[2];
    bool has_sak;
    uint8_t sak;
    std::vector<uint8_t> version;

    // ISO15693
    bool has_dsfid;
    uint8_t dsfid;
    bool has_afi;
    uint8_t afi;
    bool has_ic_ref;
    uint8_t ic_ref;
    bool no_read_multiple;

    // Memory geometry
    size_t block_size;
    size_t block_count;
    int user_first_block;
    int user_last_block;
    std::vector<uint8_t> memory;
    std::vector<bool> readable;

    // MIFARE Classic
    std::map<int, SectorKey> sector_keys;
    std::string key_source;

    Tag(TagTechnology tech = TagTechnology::UNKNOWN,
        const std::vector<uint8_t> &uid_bytes = {},
        TagType type = TagType::UNKNOWN);

    Tag(TagTechnology tech,
        const uint8_t *uid_data,
        size_t uid_len,
        TagType type = TagType::UNKNOWN);

    // UID helpers
    std::string uid_hex() const;
    std::string nfc_uid() const;
    std::string nfc_id() const;

    // Memory helpers
    void alloc_memory(size_t bsize, size_t bcount);
    void set_block(size_t block, const uint8_t *data, size_t len);
    void set_block(size_t block, const std::vector<uint8_t> &data);
    std::vector<uint8_t> get_block(size_t block) const;
    bool get_block(size_t block, uint8_t *out, size_t out_len) const;
    std::vector<uint8_t> get_bytes(size_t offset, size_t length) const;
    size_t user_capacity() const;

    // Write planning
    std::vector<BlockWrite> plan_writes(
        const std::vector<std::pair<size_t, std::vector<uint8_t>>> &patches) const;

    // Tag description
    TagDescription describe() const;
};

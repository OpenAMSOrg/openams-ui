// Copyright (C) 2026 JR Lomas <lomas.jr@gmail.com>
// This file may be distributed under the terms of the GNU GPLv3 license.

#ifndef NDEF_H
#define NDEF_H

#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace ndef {

// Type Name Format (TNF) constants
constexpr uint8_t TNF_EMPTY = 0;
constexpr uint8_t TNF_WELL_KNOWN = 1;
constexpr uint8_t TNF_MIME = 2;
constexpr uint8_t TNF_URI = 3;
constexpr uint8_t TNF_EXTERNAL = 4;
constexpr uint8_t TNF_UNKNOWN = 5;
constexpr uint8_t TNF_UNCHANGED = 6;

// URI prefix lookup
const char* get_uri_prefix(uint8_t code);
uint8_t find_best_uri_prefix(const std::string& uri, size_t& prefix_len);

struct NdefRecord {
    uint8_t tnf = TNF_EMPTY;
    std::vector<uint8_t> type;
    std::vector<uint8_t> payload;
    std::vector<uint8_t> id;
    int32_t payload_offset = -1; // -1 represents None / not set

    NdefRecord() = default;
    NdefRecord(uint8_t tnf,
               std::vector<uint8_t> type,
               std::vector<uint8_t> payload,
               std::vector<uint8_t> id = {},
               int32_t payload_offset = -1)
        : tnf(tnf), type(std::move(type)), payload(std::move(payload)),
          id(std::move(id)), payload_offset(payload_offset) {}

    NdefRecord(uint8_t tnf,
               const std::string& type_str,
               std::vector<uint8_t> payload,
               const std::string& id_str = "",
               int32_t payload_offset = -1);

    bool is_mime(const std::string& mime_type) const;
    bool is_text() const;
    bool is_uri() const;

    bool get_text(std::string& out_text, std::string* err = nullptr) const;
    std::string text() const;

    bool get_uri(std::string& out_uri, std::string* err = nullptr) const;
    std::string uri() const;
};

struct NdefMessage {
    std::vector<NdefRecord> records;
    size_t tlv_offset = 0;
    size_t offset = 0;
    size_t length = 0;
    size_t capacity = 0;

    NdefMessage() = default;
    NdefMessage(std::vector<NdefRecord> records,
                size_t tlv_offset,
                size_t offset,
                size_t length,
                size_t capacity)
        : records(std::move(records)),
          tlv_offset(tlv_offset),
          offset(offset),
          length(length),
          capacity(capacity) {}
};

// Record factory helpers
NdefRecord text_record(const std::string& text, const std::string& lang = "en", std::string* err = nullptr);
NdefRecord uri_record(const std::string& uri);
NdefRecord mime_record(const std::string& mime_type, const std::vector<uint8_t>& payload);

// Record parse and encode
bool parse_records(const uint8_t* data, size_t len,
                   std::vector<NdefRecord>& records,
                   size_t base_offset = 0,
                   std::string* err = nullptr);
bool parse_records(const std::vector<uint8_t>& data,
                   std::vector<NdefRecord>& records,
                   size_t base_offset = 0,
                   std::string* err = nullptr);

bool encode_records(const std::vector<NdefRecord>& records,
                    std::vector<uint8_t>& out,
                    std::string* err = nullptr);
std::vector<uint8_t> encode_records(const std::vector<NdefRecord>& records);

// Tag representation and TLV read/write
enum class Technology {
    Unknown = 0,
    ISO14443A = 1,
    ISO15693 = 2
};

enum class TagType {
    Unknown = 0,
    Type2 = 1,
    Type5 = 2,
    MifareClassic = 3,
    NTAG213 = 10,
    NTAG215 = 11,
    NTAG216 = 12,
    ICODE_SLIX2 = 20
};

struct Tag {
    Technology technology = Technology::ISO14443A;
    TagType tag_type = TagType::Type2;
    std::vector<uint8_t> memory;
    size_t block_size = 4;
    size_t user_first_block = 0;
    size_t user_last_block = 0;
};

size_t calc_capacity(size_t tlv_offset, size_t tlv_area_end);

bool read_message(const Tag& tag, NdefMessage& msg, std::string* err = nullptr);

bool message_patches(const Tag& tag,
                     const std::vector<NdefRecord>& records,
                     std::vector<std::pair<size_t, std::vector<uint8_t>>>& patches,
                     std::string* err = nullptr);

} // namespace ndef

#endif // NDEF_H

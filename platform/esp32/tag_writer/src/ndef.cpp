// Copyright (C) 2026 JR Lomas <lomas.jr@gmail.com>
// This file may be distributed under the terms of the GNU GPLv3 license.

#include "ndef.h"
#include <algorithm>
#include <cctype>
#include <cstring>

namespace ndef {

static const char* const URI_PREFIX_MAP[36] = {
    "",                              // 0x00
    "http://www.",                   // 0x01
    "https://www.",                  // 0x02
    "http://",                       // 0x03
    "https://",                      // 0x04
    "tel:",                          // 0x05
    "mailto:",                       // 0x06
    "ftp://anonymous:anonymous@",    // 0x07
    "ftp://ftp.",                    // 0x08
    "ftps://",                       // 0x09
    "sftp://",                       // 0x0A
    "smb://",                        // 0x0B
    "nfs://",                        // 0x0C
    "ftp://",                        // 0x0D
    "dav://",                        // 0x0E
    "news:",                         // 0x0F
    "telnet://",                     // 0x10
    "imap:",                         // 0x11
    "rtsp://",                       // 0x12
    "urn:",                          // 0x13
    "pop:",                          // 0x14
    "sip:",                          // 0x15
    "sips:",                         // 0x16
    "tftp:",                         // 0x17
    "btspp://",                      // 0x18
    "btl2cap://",                    // 0x19
    "btgoep://",                     // 0x1A
    "tcpobex://",                    // 0x1B
    "irdaobex://",                   // 0x1C
    "file://",                       // 0x1D
    "urn:epc:id:",                   // 0x1E
    "urn:epc:tag:",                  // 0x1F
    "urn:epc:pat:",                  // 0x20
    "urn:epc:raw:",                  // 0x21
    "urn:epc:",                      // 0x22
    "urn:nfc:"                       // 0x23
};

const char* get_uri_prefix(uint8_t code) {
    if (code < 36) {
        return URI_PREFIX_MAP[code];
    }
    return "";
}

uint8_t find_best_uri_prefix(const std::string& uri, size_t& prefix_len) {
    uint8_t best_code = 0;
    size_t best_len = 0;
    for (uint8_t i = 1; i < 36; ++i) {
        const char* p = URI_PREFIX_MAP[i];
        size_t plen = std::strlen(p);
        if (plen > 0 && uri.compare(0, plen, p) == 0 && plen > best_len) {
            best_code = i;
            best_len = plen;
        }
    }
    prefix_len = best_len;
    return best_code;
}

NdefRecord::NdefRecord(uint8_t tnf,
                       const std::string& type_str,
                       std::vector<uint8_t> payload,
                       const std::string& id_str,
                       int32_t payload_offset)
    : tnf(tnf),
      type(type_str.begin(), type_str.end()),
      payload(std::move(payload)),
      id(id_str.begin(), id_str.end()),
      payload_offset(payload_offset) {}

bool NdefRecord::is_mime(const std::string& mime_type) const {
    if (tnf != TNF_MIME) {
        return false;
    }
    if (type.size() != mime_type.size()) {
        return false;
    }
    for (size_t i = 0; i < type.size(); ++i) {
        if (std::tolower(type[i]) != std::tolower(static_cast<unsigned char>(mime_type[i]))) {
            return false;
        }
    }
    return true;
}

bool NdefRecord::is_text() const {
    return tnf == TNF_WELL_KNOWN && type.size() == 1 && type[0] == 'T';
}

bool NdefRecord::is_uri() const {
    return tnf == TNF_WELL_KNOWN && type.size() == 1 && type[0] == 'U';
}

static bool utf16_to_utf8(const uint8_t* data, size_t len, std::string& out) {
    out.clear();
    if (len % 2 != 0) {
        return false;
    }
    if (len == 0) {
        return true;
    }
    bool big_endian = true;
    size_t start = 0;
    if (len >= 2) {
        if (data[0] == 0xFE && data[1] == 0xFF) {
            big_endian = true;
            start = 2;
        } else if (data[0] == 0xFF && data[1] == 0xFE) {
            big_endian = false;
            start = 2;
        }
    }
    for (size_t i = start; i < len; i += 2) {
        uint16_t u = big_endian ? ((static_cast<uint16_t>(data[i]) << 8) | data[i + 1])
                                : (data[i] | (static_cast<uint16_t>(data[i + 1]) << 8));
        uint32_t cp = u;
        if (cp >= 0xD800 && cp <= 0xDBFF) {
            if (i + 2 >= len) return false;
            i += 2;
            uint16_t u2 = big_endian ? ((static_cast<uint16_t>(data[i]) << 8) | data[i + 1])
                                     : (data[i] | (static_cast<uint16_t>(data[i + 1]) << 8));
            if (u2 < 0xDC00 || u2 > 0xDFFF) return false;
            cp = 0x10000 + (((cp & 0x3FF) << 10) | (u2 & 0x3FF));
        } else if (cp >= 0xDC00 && cp <= 0xDFFF) {
            return false;
        }
        if (cp <= 0x7F) {
            out.push_back(static_cast<char>(cp));
        } else if (cp <= 0x7FF) {
            out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
            out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        } else if (cp <= 0xFFFF) {
            out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
            out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        } else if (cp <= 0x10FFFF) {
            out.push_back(static_cast<char>(0xF0 | (cp >> 18)));
            out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        } else {
            return false;
        }
    }
    return true;
}

static bool is_valid_utf8(const uint8_t* data, size_t len) {
    size_t i = 0;
    while (i < len) {
        uint8_t b = data[i++];
        if (b <= 0x7F) {
            continue;
        } else if ((b & 0xE0) == 0xC0) {
            if (b < 0xC2) return false;
            if (i >= len) return false;
            uint8_t b2 = data[i++];
            if ((b2 & 0xC0) != 0x80) return false;
        } else if ((b & 0xF0) == 0xE0) {
            if (i + 1 >= len) return false;
            uint8_t b2 = data[i++];
            uint8_t b3 = data[i++];
            if ((b2 & 0xC0) != 0x80 || (b3 & 0xC0) != 0x80) return false;
            if (b == 0xE0 && b2 < 0xA0) return false;
            if (b == 0xED && b2 >= 0xA0) return false;
        } else if ((b & 0xF8) == 0xF0) {
            if (i + 2 >= len) return false;
            uint8_t b2 = data[i++];
            uint8_t b3 = data[i++];
            uint8_t b4 = data[i++];
            if ((b2 & 0xC0) != 0x80 || (b3 & 0xC0) != 0x80 || (b4 & 0xC0) != 0x80) return false;
            if (b == 0xF0 && b2 < 0x90) return false;
            if (b == 0xF4 && b2 > 0x8F) return false;
            if (b > 0xF4) return false;
        } else {
            return false;
        }
    }
    return true;
}

bool NdefRecord::get_text(std::string& out_text, std::string* err) const {
    if (!is_text()) {
        if (err) *err = "not a text record";
        return false;
    }
    if (payload.empty()) {
        out_text = "";
        return true;
    }
    uint8_t status = payload[0];
    size_t lang_len = status & 0x3F;
    if (1 + lang_len > payload.size()) {
        if (err) *err = "truncated text record header";
        return false;
    }
    const uint8_t* text_bytes = payload.data() + 1 + lang_len;
    size_t text_len = payload.size() - 1 - lang_len;
    bool is_utf16 = (status & 0x80) != 0;

    if (is_utf16) {
        if (!utf16_to_utf8(text_bytes, text_len, out_text)) {
            if (err) *err = "malformed text payload: invalid UTF-16";
            return false;
        }
    } else {
        if (!is_valid_utf8(text_bytes, text_len)) {
            if (err) *err = "malformed text payload: invalid UTF-8";
            return false;
        }
        out_text.assign(reinterpret_cast<const char*>(text_bytes), text_len);
    }
    return true;
}

std::string NdefRecord::text() const {
    std::string out;
    if (get_text(out, nullptr)) {
        return out;
    }
    return "";
}

bool NdefRecord::get_uri(std::string& out_uri, std::string* err) const {
    if (!is_uri()) {
        if (err) *err = "not a URI record";
        return false;
    }
    if (payload.empty()) {
        out_uri = "";
        return true;
    }
    uint8_t code = payload[0];
    const char* prefix = get_uri_prefix(code);
    const uint8_t* rest_bytes = payload.data() + 1;
    size_t rest_len = payload.size() - 1;
    if (!is_valid_utf8(rest_bytes, rest_len)) {
        if (err) *err = "malformed URI payload: invalid UTF-8";
        return false;
    }
    out_uri = std::string(prefix) + std::string(reinterpret_cast<const char*>(rest_bytes), rest_len);
    return true;
}

std::string NdefRecord::uri() const {
    std::string out;
    if (get_uri(out, nullptr)) {
        return out;
    }
    return "";
}

NdefRecord text_record(const std::string& text, const std::string& lang, std::string* err) {
    if (lang.size() > 0x3F) {
        if (err) *err = "language code too long";
        return NdefRecord();
    }
    uint8_t status = static_cast<uint8_t>(lang.size() & 0x3F);
    std::vector<uint8_t> payload;
    payload.reserve(1 + lang.size() + text.size());
    payload.push_back(status);
    payload.insert(payload.end(), lang.begin(), lang.end());
    payload.insert(payload.end(), text.begin(), text.end());
    return NdefRecord(TNF_WELL_KNOWN, std::vector<uint8_t>{'T'}, std::move(payload));
}

NdefRecord uri_record(const std::string& uri) {
    size_t prefix_len = 0;
    uint8_t code = find_best_uri_prefix(uri, prefix_len);
    std::string rest = uri.substr(prefix_len);
    std::vector<uint8_t> payload;
    payload.reserve(1 + rest.size());
    payload.push_back(code);
    payload.insert(payload.end(), rest.begin(), rest.end());
    return NdefRecord(TNF_WELL_KNOWN, std::vector<uint8_t>{'U'}, std::move(payload));
}

NdefRecord mime_record(const std::string& mime_type, const std::vector<uint8_t>& payload) {
    return NdefRecord(TNF_MIME, mime_type, payload);
}

bool parse_records(const uint8_t* data, size_t len,
                   std::vector<NdefRecord>& records,
                   size_t base_offset,
                   std::string* err) {
    records.clear();
    std::vector<std::vector<uint8_t>> chunk_payloads;
    bool has_chunk_hdr = false;
    uint8_t c_tnf = 0;
    std::vector<uint8_t> c_type;
    std::vector<uint8_t> c_id;

    size_t pos = 0;
    bool me_encountered = false;

    while (pos < len) {
        uint8_t header = data[pos++];
        bool me = (header & 0x40) != 0;
        bool cf = (header & 0x20) != 0;
        bool sr = (header & 0x10) != 0;
        bool il = (header & 0x08) != 0;
        uint8_t tnf = header & 0x07;

        if (pos >= len) {
            if (err) *err = "truncated record header";
            return false;
        }
        uint8_t type_len = data[pos++];

        uint32_t payload_len = 0;
        if (sr) {
            if (pos >= len) {
                if (err) *err = "truncated short record payload length";
                return false;
            }
            payload_len = data[pos++];
        } else {
            if (pos + 4 > len) {
                if (err) *err = "truncated normal record payload length";
                return false;
            }
            payload_len = (static_cast<uint32_t>(data[pos]) << 24) |
                          (static_cast<uint32_t>(data[pos + 1]) << 16) |
                          (static_cast<uint32_t>(data[pos + 2]) << 8) |
                          static_cast<uint32_t>(data[pos + 3]);
            pos += 4;
        }

        uint8_t id_len = 0;
        if (il) {
            if (pos >= len) {
                if (err) *err = "truncated ID length";
                return false;
            }
            id_len = data[pos++];
        }

        // Check each span against the bytes left, never a sum: payload_len is
        // 32-bit and pos + type_len + id_len + payload_len can wrap on the ESP32.
        size_t remaining = len - pos;
        if (type_len > remaining || id_len > remaining - type_len ||
            payload_len > remaining - type_len - id_len) {
            if (err) *err = "record runs past message end";
            return false;
        }

        std::vector<uint8_t> rec_type(data + pos, data + pos + type_len);
        pos += type_len;

        std::vector<uint8_t> rec_id(data + pos, data + pos + id_len);
        pos += id_len;

        int32_t payload_offset = static_cast<int32_t>(base_offset + pos);
        std::vector<uint8_t> payload(data + pos, data + pos + payload_len);
        pos += payload_len;

        if (cf) {
            if (!has_chunk_hdr) {
                has_chunk_hdr = true;
                c_tnf = tnf;
                c_type = rec_type;
                c_id = rec_id;
            }
            chunk_payloads.push_back(std::move(payload));
        } else if (has_chunk_hdr) {
            chunk_payloads.push_back(std::move(payload));
            std::vector<uint8_t> joined_payload;
            for (const auto& cp : chunk_payloads) {
                joined_payload.insert(joined_payload.end(), cp.begin(), cp.end());
            }
            records.emplace_back(c_tnf, c_type, joined_payload, c_id, -1);
            has_chunk_hdr = false;
            chunk_payloads.clear();
        } else {
            records.emplace_back(tnf, rec_type, payload, rec_id, payload_offset);
        }

        if (me) {
            me_encountered = true;
            break;
        }
    }

    if (!me_encountered) {
        if (!records.empty() || has_chunk_hdr) {
            if (err) *err = "message missing ME flag on final record";
            return false;
        }
    }
    if (has_chunk_hdr) {
        if (err) *err = "unterminated chunked record sequence";
        return false;
    }
    return true;
}

bool parse_records(const std::vector<uint8_t>& data,
                   std::vector<NdefRecord>& records,
                   size_t base_offset,
                   std::string* err) {
    return parse_records(data.data(), data.size(), records, base_offset, err);
}

bool encode_records(const std::vector<NdefRecord>& records,
                    std::vector<uint8_t>& out,
                    std::string* err) {
    (void)err;
    out.clear();
    if (records.empty()) {
        return true;
    }
    size_t n = records.size();
    for (size_t i = 0; i < n; ++i) {
        const auto& rec = records[i];
        uint8_t header = rec.tnf & 0x07;
        if (i == 0) header |= 0x80;
        if (i == n - 1) header |= 0x40;
        bool sr = rec.payload.size() < 256;
        if (sr) header |= 0x10;
        bool il = !rec.id.empty();
        if (il) header |= 0x08;

        out.push_back(header);
        out.push_back(static_cast<uint8_t>(rec.type.size()));
        if (sr) {
            out.push_back(static_cast<uint8_t>(rec.payload.size()));
        } else {
            uint32_t plen = static_cast<uint32_t>(rec.payload.size());
            out.push_back(static_cast<uint8_t>((plen >> 24) & 0xFF));
            out.push_back(static_cast<uint8_t>((plen >> 16) & 0xFF));
            out.push_back(static_cast<uint8_t>((plen >> 8) & 0xFF));
            out.push_back(static_cast<uint8_t>(plen & 0xFF));
        }
        if (il) {
            out.push_back(static_cast<uint8_t>(rec.id.size()));
        }
        out.insert(out.end(), rec.type.begin(), rec.type.end());
        if (il) {
            out.insert(out.end(), rec.id.begin(), rec.id.end());
        }
        out.insert(out.end(), rec.payload.begin(), rec.payload.end());
    }
    return true;
}

std::vector<uint8_t> encode_records(const std::vector<NdefRecord>& records) {
    std::vector<uint8_t> out;
    encode_records(records, out, nullptr);
    return out;
}

size_t calc_capacity(size_t tlv_offset, size_t tlv_area_end) {
    if (tlv_area_end < tlv_offset) {
        return 0;
    }
    size_t avail = tlv_area_end - tlv_offset;
    if (avail >= 260) {
        return avail - 5;
    } else if (avail >= 3) {
        return std::min(static_cast<size_t>(254), avail - 3);
    }
    return 0;
}

static inline bool is_type2_tag(const Tag& tag) {
    if (tag.tag_type == TagType::MifareClassic) {
        return false;
    }
    if (tag.tag_type == TagType::Type2 ||
        tag.tag_type == TagType::NTAG213 ||
        tag.tag_type == TagType::NTAG215 ||
        tag.tag_type == TagType::NTAG216) {
        return true;
    }
    return tag.technology == Technology::ISO14443A;
}

static inline bool is_type5_tag(const Tag& tag) {
    if (tag.tag_type == TagType::Type5 ||
        tag.tag_type == TagType::ICODE_SLIX2) {
        return true;
    }
    return tag.technology == Technology::ISO15693;
}

bool read_message(const Tag& tag, NdefMessage& msg, std::string* err) {
    if (tag.tag_type == TagType::MifareClassic) {
        if (err) *err = "mifare classic tags not supported for NDEF message";
        return false;
    }
    if (tag.memory.size() < 16) {
        if (err) *err = "tag memory too small";
        return false;
    }

    size_t user_end = (tag.user_last_block + 1) * tag.block_size;
    if (user_end == 0 || user_end > tag.memory.size()) {
        user_end = tag.memory.size();
    }

    bool is_type2 = is_type2_tag(tag);
    bool is_type5 = is_type5_tag(tag);

    size_t tlv_start = 0;
    size_t cc_size = 0;

    if (is_type2) {
        if (tag.memory.size() < 16) {
            if (err) *err = "memory too small for Type 2 CC";
            return false;
        }
        uint8_t cc0 = tag.memory[12];
        uint8_t cc1 = tag.memory[13];
        if (cc0 == 0xE1 && (cc1 >> 4) == 1) {
            tlv_start = 16;
            cc_size = static_cast<size_t>(tag.memory[14]) * 8;
        } else {
            if (err) *err = "invalid Type 2 CC";
            return false;
        }
    } else if (is_type5) {
        if (tag.memory[0] == 0xE1) {
            tlv_start = 4;
            cc_size = static_cast<size_t>(tag.memory[2]) * 8;
        } else if (tag.memory[0] == 0xE2) {
            tlv_start = 8;
            cc_size = ((static_cast<size_t>(tag.memory[6]) << 8) | tag.memory[7]) * 8;
        } else {
            if (err) *err = "invalid Type 5 CC";
            return false;
        }
    } else {
        if (err) *err = "unsupported technology/tag type";
        return false;
    }

    size_t tlv_area_end = std::min(tlv_start + cc_size, user_end);
    size_t pos = tlv_start;
    while (pos < tlv_area_end) {
        uint8_t t = tag.memory[pos];
        if (t == 0x00) {
            pos += 1;
            continue;
        }
        if (t == 0xFE) {
            break;
        }
        size_t tlv_offset = pos;
        pos += 1;
        if (pos >= tlv_area_end) {
            break;
        }
        size_t l = tag.memory[pos++];
        if (l == 0xFF) {
            if (pos + 2 > tlv_area_end) {
                break;
            }
            l = (static_cast<size_t>(tag.memory[pos]) << 8) | tag.memory[pos + 1];
            pos += 2;
        }

        size_t msg_offset = pos;
        if (t == 0x03) {
            if (pos + l > user_end) {
                if (err) *err = "NDEF TLV length exceeds user memory";
                return false;
            }
            std::vector<NdefRecord> records;
            if (!parse_records(tag.memory.data() + msg_offset, l, records, msg_offset, err)) {
                return false;
            }
            size_t capacity = calc_capacity(tlv_offset, tlv_area_end);
            msg = NdefMessage(std::move(records), tlv_offset, msg_offset, l, capacity);
            return true;
        }
        pos += l;
    }

    if (err) *err = "no NDEF message TLV found";
    return false;
}

bool message_patches(const Tag& tag,
                     const std::vector<NdefRecord>& records,
                     std::vector<std::pair<size_t, std::vector<uint8_t>>>& patches,
                     std::string* err) {
    patches.clear();
    size_t user_end = (tag.user_last_block + 1) * tag.block_size;
    std::vector<uint8_t> msg_bytes = encode_records(records);

    std::vector<uint8_t> tlv_data;
    if (msg_bytes.size() < 255) {
        tlv_data.push_back(0x03);
        tlv_data.push_back(static_cast<uint8_t>(msg_bytes.size()));
    } else {
        tlv_data.push_back(0x03);
        tlv_data.push_back(0xFF);
        tlv_data.push_back(static_cast<uint8_t>((msg_bytes.size() >> 8) & 0xFF));
        tlv_data.push_back(static_cast<uint8_t>(msg_bytes.size() & 0xFF));
    }
    tlv_data.insert(tlv_data.end(), msg_bytes.begin(), msg_bytes.end());
    tlv_data.push_back(0xFE);

    bool is_type2 = is_type2_tag(tag);
    bool is_type5 = is_type5_tag(tag);

    if (is_type2) {
        if (tag.memory.size() < 16 || tag.memory[12] != 0xE1 || ((tag.memory[13] >> 4) != 1)) {
            if (err) *err = "tag is not NDEF formatted";
            return false;
        }
        size_t tlv_offset = 16;
        size_t cc_size = static_cast<size_t>(tag.memory[14]) * 8;
        size_t tlv_area_end = std::min(tlv_offset + cc_size, user_end);
        if (tlv_offset + tlv_data.size() > tlv_area_end) {
            if (err) *err = "message exceeds tag capacity";
            return false;
        }
        patches.push_back({tlv_offset, std::move(tlv_data)});
        return true;
    } else if (is_type5) {
        bool has_cc = (!tag.memory.empty() && tag.memory.size() >= 4 &&
                       (tag.memory[0] == 0xE1 || tag.memory[0] == 0xE2));
        if (has_cc) {
            size_t tlv_offset = 0;
            size_t cc_size = 0;
            if (tag.memory[0] == 0xE1) {
                tlv_offset = 4;
                cc_size = static_cast<size_t>(tag.memory[2]) * 8;
            } else {
                tlv_offset = 8;
                cc_size = ((static_cast<size_t>(tag.memory[6]) << 8) | tag.memory[7]) * 8;
            }
            size_t tlv_area_end = std::min(tlv_offset + cc_size, user_end);
            if (tlv_offset + tlv_data.size() > tlv_area_end) {
                if (err) *err = "message exceeds tag capacity";
                return false;
            }
            patches.push_back({tlv_offset, std::move(tlv_data)});
            return true;
        } else {
            size_t user_bytes = (tag.user_last_block + 1) * tag.block_size;
            size_t mlen = user_bytes / 8;
            std::vector<uint8_t> patch_data;
            size_t tlv_offset = 0;
            size_t cc_size = mlen * 8;
            if (mlen <= 255) {
                patch_data = {0xE1, 0x40, static_cast<uint8_t>(mlen), 0x01};
                tlv_offset = 4;
            } else {
                patch_data.resize(8);
                patch_data[0] = 0xE2;
                patch_data[1] = 0x40;
                patch_data[2] = 0x00;
                patch_data[3] = 0x01;
                patch_data[4] = 0x00;
                patch_data[5] = 0x00;
                patch_data[6] = static_cast<uint8_t>((mlen >> 8) & 0xFF);
                patch_data[7] = static_cast<uint8_t>(mlen & 0xFF);
                tlv_offset = 8;
            }
            size_t tlv_area_end = std::min(tlv_offset + cc_size, user_end);
            if (tlv_offset + tlv_data.size() > tlv_area_end) {
                if (err) *err = "message exceeds tag capacity";
                return false;
            }
            patch_data.insert(patch_data.end(), tlv_data.begin(), tlv_data.end());
            patches.push_back({0, std::move(patch_data)});
            return true;
        }
    }

    if (err) *err = "unsupported tag type for NDEF";
    return false;
}

} // namespace ndef

// Copyright (C) 2026 JR Lomas <lomas.jr@gmail.com>
// Based on the OpenTag3D spec (GPLv3).
// This file may be distributed under the terms of the GNU GPLv3 license.

#include "opentag3d.h"
#include "ndef.h"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

static constexpr const char *MIME_TYPE = "application/opentag3d";
static constexpr size_t PAYLOAD_LEN = 0xD8;

// Clamp a rounded value to a uint16 field: a decoded source value can be out
// of range or non-finite, and that cast would otherwise be undefined.
static uint16_t ot3d_u16(double v) {
    double r = std::round(v);
    if (!(r > 0.0)) return 0;
    if (r > 65535.0) return 65535;
    return (uint16_t)r;
}

static ndef::Tag to_ndef_tag(const Tag &tag) {
    ndef::Tag ntag;
    if (tag.technology == TagTechnology::ISO14443A) {
        ntag.technology = ndef::Technology::ISO14443A;
    } else if (tag.technology == TagTechnology::ISO15693) {
        ntag.technology = ndef::Technology::ISO15693;
    } else {
        ntag.technology = ndef::Technology::Unknown;
    }

    switch (tag.tag_type) {
        case TagType::NTAG213: ntag.tag_type = ndef::TagType::NTAG213; break;
        case TagType::NTAG215: ntag.tag_type = ndef::TagType::NTAG215; break;
        case TagType::NTAG216: ntag.tag_type = ndef::TagType::NTAG216; break;
        case TagType::ICODE_SLIX2: ntag.tag_type = ndef::TagType::ICODE_SLIX2; break;
        case TagType::MIFARE_CLASSIC_1K:
        case TagType::MIFARE_CLASSIC_4K:
        case TagType::MIFARE_MINI:
            ntag.tag_type = ndef::TagType::MifareClassic; break;
        default:
            if (is_type2_tag(tag.tag_type)) {
                ntag.tag_type = ndef::TagType::Type2;
            } else if (is_type5_tag(tag.tag_type)) {
                ntag.tag_type = ndef::TagType::Type5;
            } else {
                ntag.tag_type = ndef::TagType::Unknown;
            }
            break;
    }

    ntag.memory = tag.memory;
    ntag.block_size = tag.block_size;
    ntag.user_first_block = tag.user_first_block >= 0 ? static_cast<size_t>(tag.user_first_block) : 0;
    ntag.user_last_block = tag.user_last_block >= 0 ? static_cast<size_t>(tag.user_last_block) : 0;
    return ntag;
}

static std::string get_str(const std::vector<uint8_t> &buf, size_t offset, size_t length) {
    if (offset >= buf.size()) return "";
    size_t actual_len = std::min(length, buf.size() - offset);
    size_t null_pos = 0;
    while (null_pos < actual_len && buf[offset + null_pos] != '\0') {
        null_pos++;
    }
    size_t start = 0;
    while (start < null_pos && (buf[offset + start] == ' ' || buf[offset + start] == '\t' ||
                               buf[offset + start] == '\r' || buf[offset + start] == '\n')) {
        start++;
    }
    size_t end = null_pos;
    while (end > start && (buf[offset + end - 1] == ' ' || buf[offset + end - 1] == '\t' ||
                           buf[offset + end - 1] == '\r' || buf[offset + end - 1] == '\n')) {
        end--;
    }
    if (start >= end) return "";
    return std::string(reinterpret_cast<const char*>(&buf[offset + start]), end - start);
}

static std::string get_rgba(const std::vector<uint8_t> &buf, size_t offset) {
    if (offset + 4 > buf.size()) return "";
    uint8_t r = buf[offset], g = buf[offset + 1], b = buf[offset + 2], a = buf[offset + 3];
    if (r == 0 && g == 0 && b == 0 && a == 0) return "";
    char cbuf[7];
    snprintf(cbuf, sizeof(cbuf), "%02X%02X%02X", r, g, b);
    return std::string(cbuf);
}

static bool pack_color(const std::string &c_hex, uint8_t *out, std::string &err) {
    std::string s = c_hex;
    size_t start = 0;
    while (start < s.size() && (s[start] == ' ' || s[start] == '#')) start++;
    size_t end = s.size();
    while (end > start && (s[end - 1] == ' ' || s[end - 1] == '\t')) end--;
    s = s.substr(start, end - start);
    if (s.size() < 6) {
        err = "invalid color: " + c_hex;
        return false;
    }
    for (size_t i = 0; i < 6; ++i) {
        if (!std::isxdigit(static_cast<unsigned char>(s[i]))) {
            err = "invalid color: " + c_hex;
            return false;
        }
    }
    char hex[3] = {0, 0, 0};
    hex[0] = s[0]; hex[1] = s[1]; out[0] = static_cast<uint8_t>(std::strtoul(hex, nullptr, 16));
    hex[0] = s[2]; hex[1] = s[3]; out[1] = static_cast<uint8_t>(std::strtoul(hex, nullptr, 16));
    hex[0] = s[4]; hex[1] = s[5]; out[2] = static_cast<uint8_t>(std::strtoul(hex, nullptr, 16));
    out[3] = 0xFF;
    return true;
}

static bool temp_to_byte(double t, uint8_t &out, std::string &err) {
    long v = static_cast<long>(std::round(t / 5.0));
    if (v < 0 || v > 255) {
        char buf[64];
        snprintf(buf, sizeof(buf), "temperature %.1f out of range", t);
        err = buf;
        return false;
    }
    out = static_cast<uint8_t>(v);
    return true;
}

static double default_density_for(const std::string &material) {
    std::string mat = material;
    for (auto &c : mat) c = std::toupper(static_cast<unsigned char>(c));
    if (mat == "PLA" || mat == "PLA+" || mat == "PLA-CF" || mat == "PLA-AERO") return 1.24;
    if (mat == "PETG" || mat == "PETG-CF") return 1.27;
    if (mat == "ABS" || mat == "ABS-GF" || mat == "ABS-CF") return 1.04;
    if (mat == "ASA" || mat == "ASA-CF" || mat == "ASA-AERO") return 1.07;
    if (mat == "PC") return 1.20;
    if (mat == "TPU") return 1.21;
    if (mat == "PA" || mat == "PA6" || mat == "PA-CF" || mat == "PA-GF") return 1.14;
    if (mat == "PP" || mat == "PP-CF" || mat == "PP-GF") return 0.90;
    if (mat == "PVA") return 1.19;
    if (mat == "HIPS") return 1.04;
    if (mat == "BVOH") return 1.14;
    if (mat == "CPE") return 1.27;
    return 0.0;
}

std::vector<std::string> OpenTag3DCodec::tag_types() const {
    return {
        TagTypes::NTAG213,
        TagTypes::NTAG215,
        TagTypes::NTAG216,
        TagTypes::ULTRALIGHT,
        TagTypes::ULTRALIGHT_EV1,
        TagTypes::TYPE2,
        TagTypes::ICODE_SLIX,
        TagTypes::ICODE_SLIX2,
        TagTypes::ICODE_SLIX_S,
        TagTypes::ICODE_SLIX_L,
        TagTypes::ICODE_DNA,
        TagTypes::TYPE5
    };
}

bool OpenTag3DCodec::can_encode(const Tag &tag) {
    std::string ttype = tag_type_to_string(tag.tag_type);
    bool type_match = false;
    for (const auto &t : tag_types()) {
        if (ttype == t) {
            type_match = true;
            break;
        }
    }
    bool tech_match = (tag.technology == TagTechnology::ISO14443A ||
                       tag.technology == TagTechnology::ISO15693);
    if (!type_match && !tech_match) {
        return false;
    }
    ndef::Tag ntag = to_ndef_tag(tag);
    ndef::NdefMessage msg;
    if (ndef::read_message(ntag, msg)) {
        return msg.capacity >= 240;
    }
    return tag.user_capacity() >= 240;
}

bool OpenTag3DCodec::decode(const Tag &tag, DecodeResult &out) {
    ndef::Tag ntag = to_ndef_tag(tag);
    ndef::NdefMessage msg;
    if (!ndef::read_message(ntag, msg)) {
        return false;
    }

    const ndef::NdefRecord *target = nullptr;
    for (const auto &rec : msg.records) {
        if (rec.is_mime(MIME_TYPE)) {
            target = &rec;
            break;
        }
    }
    if (!target) {
        return false;
    }

    std::vector<uint8_t> payload = target->payload;
    if (payload.size() < PAYLOAD_LEN) {
        payload.resize(PAYLOAD_LEN, 0);
    }

    uint16_t tag_version = (static_cast<uint16_t>(payload[0]) << 8) | payload[1];
    uint16_t ver_major = tag_version / 1000;
    char ver_buf[32];
    snprintf(ver_buf, sizeof(ver_buf), "%.3f", tag_version / 1000.0);
    std::string ver_str = ver_buf;

    if (ver_major != 2) {
        out.recognized = true;
        out.format = "opentag3d";
        out.filament = Filament();
        out.info.clear();
        out.info["error"] = std::string("unsupported OpenTag3D version ") + ver_str;
        return true;
    }

    std::string mat_raw = get_str(payload, 0x02, 5);
    std::string mat_mod = get_str(payload, 0x07, 5);
    std::string mfg = get_str(payload, 0x0C, 16);
    std::string c_name = get_str(payload, 0x1C, 32);

    std::vector<std::string> colors;
    for (size_t off : {0x3C, 0x40, 0x44, 0x48}) {
        std::string c = get_rgba(payload, off);
        if (!c.empty()) {
            colors.push_back(c);
        }
    }
    std::string color_hex = colors.empty() ? "" : colors[0];

    std::string serial = get_str(payload, 0x4C, 32);
    std::string sku = get_str(payload, 0x6C, 16);

    uint64_t barcode = 0;
    for (size_t i = 0x7C; i < 0x82; ++i) {
        barcode = (barcode << 8) | payload[i];
    }

    uint16_t y = (static_cast<uint16_t>(payload[0x84]) << 8) | payload[0x85];
    uint8_t m = payload[0x86];
    uint8_t d = payload[0x87];
    std::string mfg_date;
    if (y != 0 && m != 0 && d != 0) {
        char dbuf[16];
        snprintf(dbuf, sizeof(dbuf), "%04d-%02d-%02d", y, m, d);
        mfg_date = dbuf;
    }

    uint16_t diameter = (static_cast<uint16_t>(payload[0x8C]) << 8) | payload[0x8D];
    double diameter_mm = (diameter != 0) ? (diameter / 1000.0) : 0.0;

    uint8_t tolerance = payload[0x8E];
    uint8_t nozzle_diam = payload[0x8F];

    uint8_t print_temp = payload[0x90];
    uint8_t min_print = payload[0x91];
    uint8_t max_print = payload[0x92];
    uint8_t chamber_temp = payload[0x93];
    uint8_t bed_temp = payload[0x94];
    uint8_t min_bed = payload[0x95];
    uint8_t max_bed = payload[0x96];

    uint8_t max_dry = payload[0x9A];
    uint8_t dry_time = payload[0x9B];

    uint16_t density = (static_cast<uint16_t>(payload[0x9C]) << 8) | payload[0x9D];
    double density_val = (density != 0) ? (density / 1000.0) : 0.0;

    uint16_t weight = (static_cast<uint16_t>(payload[0x9E]) << 8) | payload[0x9F];
    int weight_g = weight;

    uint16_t empty_spool = (static_cast<uint16_t>(payload[0xA0]) << 8) | payload[0xA1];
    int empty_spool_g = empty_spool;

    uint16_t meas_len = (static_cast<uint16_t>(payload[0xA2]) << 8) | payload[0xA3];
    int length_m = meas_len;

    uint16_t meas_weight = (static_cast<uint16_t>(payload[0xA4]) << 8) | payload[0xA5];
    int remaining_g = 0;
    if (meas_weight != 0 && empty_spool != 0) {
        remaining_g = static_cast<int>(meas_weight) - static_cast<int>(empty_spool);
    }

    int nozzle_min = (min_print != 0) ? (min_print * 5)
                                      : ((print_temp != 0) ? (print_temp * 5) : 0);
    int nozzle_max = (max_print != 0) ? (max_print * 5)
                                      : ((print_temp != 0) ? (print_temp * 5) : 0);

    int bed_min_c = (min_bed != 0) ? (min_bed * 5)
                                   : ((bed_temp != 0) ? (bed_temp * 5) : 0);
    int bed_max_c = (max_bed != 0) ? (max_bed * 5)
                                   : ((bed_temp != 0) ? (bed_temp * 5) : 0);

    int chamber_c = (chamber_temp != 0) ? (chamber_temp * 5) : 0;
    int drying_c = (max_dry != 0) ? (max_dry * 5) : 0;
    int drying_min = (dry_time != 0) ? (dry_time * 60) : 0;

    std::string material = mat_raw;
    for (auto &c : material) c = std::toupper(static_cast<unsigned char>(c));

    std::vector<std::string> name_parts;
    if (!material.empty()) name_parts.push_back(material);
    if (!mat_mod.empty()) name_parts.push_back(mat_mod);
    if (!c_name.empty()) name_parts.push_back(c_name);
    std::string name;
    for (size_t i = 0; i < name_parts.size(); ++i) {
        if (i > 0) name += " ";
        name += name_parts[i];
    }

    std::string eff_sku = sku;
    if (eff_sku.empty() && barcode != 0) {
        eff_sku = std::to_string(barcode);
    }

    Filament fil;
    fil.brand = mfg;
    fil.material = material;
    fil.name = name;
    fil.color_hex = color_hex;
    fil.colors = colors;
    fil.diameter_mm = diameter_mm;
    fil.density = density_val;
    fil.weight_g = weight_g;
    fil.spool_weight_g = empty_spool_g;
    fil.length_m = length_m;
    fil.nozzle_min = nozzle_min;
    fil.nozzle_max = nozzle_max;
    fil.bed_min = bed_min_c;
    fil.bed_max = bed_max_c;
    fil.drying_temp = drying_c;
    fil.drying_time_min = drying_min;
    fil.sku = eff_sku;
    if (remaining_g != 0) {
        fil.fields["remaining_g"] = std::to_string(remaining_g);
    }
    if (chamber_c != 0) {
        fil.fields["chamber_temp"] = std::to_string(chamber_c);
    }
    if (!mfg_date.empty()) {
        fil.fields["manufactured"] = mfg_date;
    }

    out.info.clear();
    out.info["tag_version"] = ver_str;
    if (!mat_raw.empty()) out.info["material"] = mat_raw;
    if (!mat_mod.empty()) out.info["material_mod"] = mat_mod;
    if (!mfg.empty()) out.info["manufacturer"] = mfg;
    if (!c_name.empty()) out.info["color_name"] = c_name;
    if (!serial.empty()) out.info["serial"] = serial;
    if (!sku.empty()) out.info["sku"] = sku;
    if (barcode != 0) out.info["barcode"] = std::to_string(barcode);
    if (!mfg_date.empty()) out.info["mfg_date"] = mfg_date;
    if (diameter != 0) out.info["diameter"] = std::to_string(diameter);
    if (tolerance != 0) out.info["tolerance"] = std::to_string(tolerance);
    if (nozzle_diam != 0) {
        char ndbuf[32];
        snprintf(ndbuf, sizeof(ndbuf), "%.1f", nozzle_diam * 0.1);
        out.info["nozzle_diameter"] = ndbuf;
    }
    if (density != 0) out.info["density"] = std::to_string(density);
    if (weight != 0) out.info["weight"] = std::to_string(weight);
    std::string data_url = get_str(payload, 0xB8, 32);
    if (!data_url.empty()) out.info["data_url"] = data_url;
    if (tag_version > 2003) {
        out.info["warning"] = std::string("newer minor version ") + ver_str;
    }

    out.recognized = true;
    out.format = "opentag3d";
    out.filament = fil;
    return true;
}

bool OpenTag3DCodec::encode(Tag &tag, const Filament &fields, std::string &err) {
    if (fields.material.empty()) {
        err = "material is required";
        return false;
    }
    if (fields.brand.empty()) {
        err = "brand is required";
        return false;
    }
    if (fields.color_hex.empty()) {
        err = "color_hex is required";
        return false;
    }
    if (fields.diameter_mm <= 0.0) {
        err = "diameter_mm is required";
        return false;
    }
    if (fields.weight_g <= 0) {
        err = "weight_g is required";
        return false;
    }

    int n_min = fields.nozzle_min;
    int n_max = fields.nozzle_max;
    if (n_min <= 0 && n_max <= 0) {
        err = "nozzle temps required";
        return false;
    }
    if (n_min <= 0) n_min = n_max;
    if (n_max <= 0) n_max = n_min;

    int b_min = fields.bed_min;
    int b_max = fields.bed_max;
    if (b_min <= 0 && b_max <= 0) {
        err = "bed temp required";
        return false;
    }
    if (b_min <= 0) b_min = b_max;
    if (b_max <= 0) b_max = b_min;

    double density = fields.density;
    if (density <= 0.0) {
        density = default_density_for(fields.material);
    }
    if (density <= 0.0) {
        err = "density is required and no default found";
        return false;
    }

    std::vector<uint8_t> buf(PAYLOAD_LEN, 0);
    // 0x00: version 2003
    buf[0] = (2003 >> 8) & 0xFF;
    buf[1] = 2003 & 0xFF;

    // 0x02: material (5 bytes)
    size_t mat_len = std::min(fields.material.size(), size_t(5));
    memcpy(&buf[0x02], fields.material.data(), mat_len);

    // 0x07: material_mod (5 bytes)
    auto it_mod = fields.fields.find("subtype");
    if (it_mod != fields.fields.end() && !it_mod->second.empty()) {
        size_t mod_len = std::min(it_mod->second.size(), size_t(5));
        memcpy(&buf[0x07], it_mod->second.data(), mod_len);
    }

    // 0x0C: manufacturer (16 bytes)
    size_t mfg_len = std::min(fields.brand.size(), size_t(16));
    memcpy(&buf[0x0C], fields.brand.data(), mfg_len);

    // 0x1C: color_name (32 bytes)
    std::string cname;
    auto it_cname = fields.fields.find("color_name");
    if (it_cname != fields.fields.end() && !it_cname->second.empty()) {
        cname = it_cname->second;
    } else if (!fields.name.empty()) {
        cname = fields.name;
    }
    if (!cname.empty()) {
        size_t cname_len = std::min(cname.size(), size_t(32));
        memcpy(&buf[0x1C], cname.data(), cname_len);
    }

    // Colors: 0x3C, 0x40, 0x44, 0x48
    if (!pack_color(fields.color_hex, &buf[0x3C], err)) {
        return false;
    }
    if (fields.colors.size() > 1) {
        for (size_t i = 1; i < fields.colors.size() && i <= 3; ++i) {
            if (!pack_color(fields.colors[i], &buf[0x3C + i * 4], err)) {
                return false;
            }
        }
    }

    // 0x8C: diameter
    uint16_t diam_val = ot3d_u16(fields.diameter_mm * 1000.0);
    buf[0x8C] = (diam_val >> 8) & 0xFF;
    buf[0x8D] = diam_val & 0xFF;

    double n_avg = (n_min + n_max) / 2.0;
    if (!temp_to_byte(n_avg, buf[0x90], err) ||
        !temp_to_byte(n_min, buf[0x91], err) ||
        !temp_to_byte(n_max, buf[0x92], err)) {
        return false;
    }

    auto it_chamber = fields.fields.find("chamber_temp");
    if (it_chamber != fields.fields.end() && !it_chamber->second.empty()) {
        double ct = std::strtod(it_chamber->second.c_str(), nullptr);
        if (!temp_to_byte(ct, buf[0x93], err)) {
            return false;
        }
    }

    double b_avg = (b_min + b_max) / 2.0;
    if (!temp_to_byte(b_avg, buf[0x94], err) ||
        !temp_to_byte(b_min, buf[0x95], err) ||
        !temp_to_byte(b_max, buf[0x96], err)) {
        return false;
    }

    if (fields.drying_temp > 0) {
        if (!temp_to_byte(fields.drying_temp, buf[0x9A], err)) {
            return false;
        }
    }
    if (fields.drying_time_min > 0) {
        int dt = static_cast<int>(std::round(fields.drying_time_min / 60.0));
        buf[0x9B] = static_cast<uint8_t>(std::min(255, std::max(0, dt)));
    }

    // 0x9C: density
    uint16_t dens_val = ot3d_u16(density * 1000.0);
    buf[0x9C] = (dens_val >> 8) & 0xFF;
    buf[0x9D] = dens_val & 0xFF;

    // 0x9E: weight
    uint16_t wt_val = ot3d_u16(fields.weight_g);
    buf[0x9E] = (wt_val >> 8) & 0xFF;
    buf[0x9F] = wt_val & 0xFF;

    if (fields.spool_weight_g > 0) {
        uint16_t sw_val = ot3d_u16(fields.spool_weight_g);
        buf[0xA0] = (sw_val >> 8) & 0xFF;
        buf[0xA1] = sw_val & 0xFF;
    }

    ndef::NdefRecord rec = ndef::mime_record(MIME_TYPE, buf);
    std::vector<std::pair<size_t, std::vector<uint8_t>>> patches;
    ndef::Tag ntag = to_ndef_tag(tag);
    if (!ndef::message_patches(ntag, {rec}, patches, &err)) {
        return false;
    }

    for (const auto &p : patches) {
        size_t off = p.first;
        const auto &data = p.second;
        if (off + data.size() > tag.memory.size()) {
            err = "write outside tag memory";
            return false;
        }
        std::copy(data.begin(), data.end(), tag.memory.begin() + off);
        if (tag.block_size > 0) {
            size_t first_b = off / tag.block_size;
            size_t last_b = (off + data.size() - 1) / tag.block_size;
            for (size_t b = first_b; b <= last_b && b < tag.readable.size(); ++b) {
                tag.readable[b] = true;
            }
        }
    }

    return true;
}

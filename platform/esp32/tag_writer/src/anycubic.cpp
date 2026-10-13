// Copyright (C) 2026 JR Lomas <lomas.jr@gmail.com>
// Based on OpenRFID and ACE-RFID (GPLv3).
// This file may be distributed under the terms of the GNU GPLv3 license.

#include "anycubic.h"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

static constexpr uint8_t MAGIC_BYTE0 = 0x7B;
static constexpr uint8_t MAGIC_BYTE2 = 0x65;

static std::string decode_ascii(const uint8_t *data, size_t len) {
    size_t end = 0;
    while (end < len && data[end] != '\0') {
        end++;
    }
    size_t start = 0;
    while (start < end && (data[start] == ' ' || data[start] == '\t' ||
                           data[start] == '\r' || data[start] == '\n')) {
        start++;
    }
    while (end > start && (data[end - 1] == ' ' || data[end - 1] == '\t' ||
                           data[end - 1] == '\r' || data[end - 1] == '\n')) {
        end--;
    }
    if (start >= end) return "";
    return std::string(reinterpret_cast<const char*>(data + start), end - start);
}

static bool parse_rgb(const std::string &c_hex, uint8_t &r, uint8_t &g, uint8_t &b, std::string &err) {
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
    hex[0] = s[0]; hex[1] = s[1]; r = static_cast<uint8_t>(std::strtoul(hex, nullptr, 16));
    hex[0] = s[2]; hex[1] = s[3]; g = static_cast<uint8_t>(std::strtoul(hex, nullptr, 16));
    hex[0] = s[4]; hex[1] = s[5]; b = static_cast<uint8_t>(std::strtoul(hex, nullptr, 16));
    return true;
}

std::vector<std::string> AnycubicCodec::tag_types() const {
    return {
        TagTypes::NTAG213,
        TagTypes::NTAG215,
        TagTypes::NTAG216,
        TagTypes::ULTRALIGHT,
        TagTypes::ULTRALIGHT_EV1,
        TagTypes::TYPE2
    };
}

bool AnycubicCodec::can_encode(const Tag &tag) {
    std::string ttype = tag_type_to_string(tag.tag_type);
    bool type_match = false;
    for (const auto &t : tag_types()) {
        if (ttype == t) {
            type_match = true;
            break;
        }
    }
    return type_match && (tag.user_last_block >= 31);
}

bool AnycubicCodec::decode(const Tag &tag, DecodeResult &out) {
    if (tag.memory.size() < 128) {
        return false;
    }

    if (tag.memory[16] != MAGIC_BYTE0 || tag.memory[18] != MAGIC_BYTE2) {
        return false;
    }

    std::string sku = decode_ascii(&tag.memory[20], 20);
    std::string brand_raw = decode_ascii(&tag.memory[40], 20);
    std::string brand = (brand_raw == "AC") ? "Anycubic" : brand_raw;

    std::string mat_raw = decode_ascii(&tag.memory[60], 16);
    std::string name = mat_raw;
    std::string material = mat_raw;
    while (!material.empty() && material.back() == '+') {
        material.pop_back();
    }
    for (auto &c : material) c = std::toupper(static_cast<unsigned char>(c));

    uint8_t b = tag.memory[81];
    uint8_t g = tag.memory[82];
    uint8_t r = tag.memory[83];
    char cbuf[7];
    snprintf(cbuf, sizeof(cbuf), "%02X%02X%02X", r, g, b);
    std::string color_hex = cbuf;
    std::vector<std::string> colors = { color_hex };

    uint16_t n_min = static_cast<uint16_t>(tag.memory[96]) | (static_cast<uint16_t>(tag.memory[97]) << 8);
    uint16_t n_max = static_cast<uint16_t>(tag.memory[98]) | (static_cast<uint16_t>(tag.memory[99]) << 8);

    uint16_t b_min = static_cast<uint16_t>(tag.memory[116]) | (static_cast<uint16_t>(tag.memory[117]) << 8);
    uint16_t b_max = static_cast<uint16_t>(tag.memory[118]) | (static_cast<uint16_t>(tag.memory[119]) << 8);

    uint16_t diam_raw = static_cast<uint16_t>(tag.memory[120]) | (static_cast<uint16_t>(tag.memory[121]) << 8);
    uint16_t len_raw = static_cast<uint16_t>(tag.memory[122]) | (static_cast<uint16_t>(tag.memory[123]) << 8);
    double diameter_mm = (diam_raw != 0) ? (diam_raw / 100.0) : 0.0;
    int length_m = len_raw;

    uint16_t weight_raw = static_cast<uint16_t>(tag.memory[124]) | (static_cast<uint16_t>(tag.memory[125]) << 8);
    int weight_g = weight_raw;

    Filament fil;
    fil.brand = brand;
    fil.material = material;
    fil.name = name;
    fil.sku = sku;
    fil.color_hex = color_hex;
    fil.colors = colors;
    fil.nozzle_min = n_min;
    fil.nozzle_max = n_max;
    fil.bed_min = b_min;
    fil.bed_max = b_max;
    fil.diameter_mm = diameter_mm;
    fil.length_m = length_m;
    fil.weight_g = weight_g;

    out.info.clear();
    if (!sku.empty()) out.info["sku"] = sku;
    if (!brand_raw.empty()) out.info["brand"] = brand_raw;
    if (!mat_raw.empty()) out.info["material"] = mat_raw;
    if (length_m != 0) out.info["length_m"] = std::to_string(length_m);

    out.recognized = true;
    out.format = "anycubic";
    out.filament = fil;
    return true;
}

bool AnycubicCodec::encode(Tag &tag, const Filament &fields, std::string &err) {
    if (!can_encode(tag)) {
        err = "tag cannot fit Anycubic layout";
        return false;
    }

    if (fields.material.empty()) {
        err = "material is required";
        return false;
    }

    if (fields.color_hex.empty()) {
        err = "color_hex is required";
        return false;
    }

    uint8_t r = 0, g = 0, b = 0;
    if (!parse_rgb(fields.color_hex, r, g, b, err)) {
        return false;
    }
    if (r == 0 && g == 0 && b == 0) {
        r = 1;
        g = 1;
        b = 1;
    }

    std::vector<uint8_t> buf(112, 0);
    // Page 4: magic 7B 00 65 00
    buf[0] = 0x7B;
    buf[1] = 0x00;
    buf[2] = 0x65;
    buf[3] = 0x00;

    // Pages 5-9: SKU (20 bytes)
    if (!fields.sku.empty()) {
        size_t n = std::min(fields.sku.size(), size_t(20));
        memcpy(&buf[4], fields.sku.data(), n);
    }

    // Pages 10-14: Brand (20 bytes)
    std::string brand = fields.brand.empty() ? "AC" : fields.brand;
    size_t nb = std::min(brand.size(), size_t(20));
    memcpy(&buf[24], brand.data(), nb);

    // Pages 15-18: Material (16 bytes)
    std::string mat_text = !fields.name.empty() ? fields.name : fields.material;
    size_t nm = std::min(mat_text.size(), size_t(16));
    memcpy(&buf[44], mat_text.data(), nm);

    // Page 20: Color as A, B, G, R
    buf[64] = 0xFF;
    buf[65] = b;
    buf[66] = g;
    buf[67] = r;

    // Page 24: Nozzle temps
    int n_min = fields.nozzle_min;
    int n_max = (fields.nozzle_max > 0) ? fields.nozzle_max : n_min;
    buf[80] = n_min & 0xFF;
    buf[81] = (n_min >> 8) & 0xFF;
    buf[82] = n_max & 0xFF;
    buf[83] = (n_max >> 8) & 0xFF;

    // Page 29: Bed temps
    int b_min = fields.bed_min;
    int b_max = (fields.bed_max > 0) ? fields.bed_max : b_min;
    buf[100] = b_min & 0xFF;
    buf[101] = (b_min >> 8) & 0xFF;
    buf[102] = b_max & 0xFF;
    buf[103] = (b_max >> 8) & 0xFF;

    // Page 30: Diameter and length
    double diam = (fields.diameter_mm > 0.0) ? fields.diameter_mm : 1.75;
    uint16_t diam_raw = static_cast<uint16_t>(std::round(diam * 100.0));
    uint16_t len_m = static_cast<uint16_t>(fields.length_m);
    buf[104] = diam_raw & 0xFF;
    buf[105] = (diam_raw >> 8) & 0xFF;
    buf[106] = len_m & 0xFF;
    buf[107] = (len_m >> 8) & 0xFF;

    // Page 31: Weight
    uint16_t w = static_cast<uint16_t>(fields.weight_g);
    buf[108] = w & 0xFF;
    buf[109] = (w >> 8) & 0xFF;

    if (16 + buf.size() > tag.memory.size()) {
        err = "write outside tag memory";
        return false;
    }

    std::copy(buf.begin(), buf.end(), tag.memory.begin() + 16);
    if (tag.block_size > 0) {
        for (size_t block = 4; block <= 31 && block < tag.readable.size(); ++block) {
            tag.readable[block] = true;
        }
    }

    return true;
}

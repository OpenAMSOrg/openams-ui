// Copyright (C) 2026 JR Lomas <lomas.jr@gmail.com>
// Based on OpenRFID (GPLv3).
// This file may be distributed under the terms of the GNU GPLv3 license.

#include "qidi.h"
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>

namespace qidi_data {

static const MaterialInfo MATERIALS_TABLE[] = {
    {1, "PLA Rapido", "PLA", 190, 240},
    {2, "PLA Matte", "PLA", 190, 240},
    {3, "PLA Metal", "PLA", 190, 240},
    {4, "PLA Silk", "PLA", 190, 240},
    {5, "PLA-CF", "PLA-CF", 210, 250},
    {6, "PLA-Wood", "PLA", 190, 240},
    {7, "PLA Basic", "PLA", 190, 240},
    {8, "PLA Matte Basic", "PLA", 190, 230},
    {10, "Support For PLA", "PLA-S", 210, 240},
    {11, "ABS Rapido", "ABS", 240, 280},
    {12, "ABS-GF", "ABS-GF", 240, 280},
    {13, "ABS-Metal", "ABS", 240, 280},
    {14, "ABS-Odorless", "ABS", 240, 280},
    {18, "ASA", "ASA", 240, 280},
    {19, "ASA-Aero", "ASA-AERO", 240, 280},
    {20, "ASA-CF", "ASA-CF", 240, 280},
    {23, "PC", "PC", 240, 280},
    {24, "UltraPA", "UltraPA", 260, 300},
    {25, "PA-CF", "PA-CF", 260, 300},
    {26, "UltraPA-CF25", "UltraPA-CF25", 300, 320},
    {27, "PA12-CF", "PA12-CF", 260, 300},
    {30, "PAHT-CF", "PAHT-CF", 300, 320},
    {31, "PAHT-GF", "PAHT-GF", 300, 320},
    {32, "Support For PAHT", "PAHT-S", 260, 280},
    {33, "Support For PET/PA", "PA-S", 260, 280},
    {34, "PC/ABS-FR", "PC-ABS-FR", 260, 280},
    {35, "TPEE", "TPEE", 230, 260},
    {36, "PEBA", "PEBA", 230, 260},
    {37, "PET-CF", "PET-CF", 280, 320},
    {38, "PET-GF", "PET-GF", 280, 320},
    {39, "PETG Basic", "PETG", 240, 280},
    {40, "PETG-Tough", "PETG", 240, 275},
    {41, "PETG Rapido", "PETG", 220, 270},
    {42, "PETG-CF", "PETG-CF", 240, 270},
    {43, "PETG-GF", "PETG-GF", 240, 270},
    {44, "PPS-CF", "PPS-CF", 300, 350},
    {45, "PETG Translucent", "PETG", 240, 280},
    {46, "PPS-GF", "PPS-GF", 300, 350},
    {47, "PVA", "PVA", 210, 250},
    {48, "TPU-AERO 64D", "TPU-AERO 64D", 200, 250},
    {49, "TPU-Aero", "TPU-AERO", 200, 250},
    {50, "TPU 95A-HF", "TPU", 200, 250},
};

static const ColorInfo COLORS_TABLE[] = {
    {1, "FAFAFA"},
    {2, "060606"},
    {3, "D9E3ED"},
    {4, "5CF30F"},
    {5, "63E492"},
    {6, "2850FF"},
    {7, "FE98FE"},
    {8, "DFD628"},
    {9, "228332"},
    {10, "99DEFF"},
    {11, "1714B0"},
    {12, "CEC0FE"},
    {13, "CADE4B"},
    {14, "1353AB"},
    {15, "5EA9FD"},
    {16, "A878FF"},
    {17, "FE717A"},
    {18, "FF362D"},
    {19, "E2DFCD"},
    {20, "898F9B"},
    {21, "6E3812"},
    {22, "CAC59F"},
    {23, "F28636"},
    {24, "B87F2B"},
};

const MaterialInfo *find_material(uint8_t code) {
    for (const auto &item : MATERIALS_TABLE) {
        if (item.code == code) {
            return &item;
        }
    }
    return nullptr;
}

const char *find_color(uint8_t code) {
    for (const auto &item : COLORS_TABLE) {
        if (item.code == code) {
            return item.hex;
        }
    }
    return nullptr;
}

const char *find_vendor(uint8_t code) {
    if (code == 1) return "QIDI";
    if (code == 0) return "Generic";
    return nullptr;
}

} // namespace qidi_data

static std::string trim(const std::string &s) {
    size_t start = 0;
    while (start < s.size() && (s[start] == ' ' || s[start] == '\t' || s[start] == '\r' || s[start] == '\n')) {
        start++;
    }
    size_t end = s.size();
    while (end > start && (s[end - 1] == ' ' || s[end - 1] == '\t' || s[end - 1] == '\r' || s[end - 1] == '\n')) {
        end--;
    }
    return s.substr(start, end - start);
}

static std::string to_lower(const std::string &s) {
    std::string out = s;
    for (char &c : out) {
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    }
    return out;
}

static std::string to_upper(const std::string &s) {
    std::string out = s;
    for (char &c : out) {
        if (c >= 'a' && c <= 'z') c = static_cast<char>(c - 'a' + 'A');
    }
    return out;
}

QidiCodec::QidiCodec(const CodecOptions &options) {
    (void)options;
}

std::vector<std::string> QidiCodec::tag_types() const {
    return {TagTypes::MIFARE_CLASSIC_1K, TagTypes::MIFARE_MINI};
}

void QidiCodec::mifare_keys(const uint8_t *uid, size_t uid_len,
                            std::map<int, std::pair<std::vector<uint8_t>,
                            std::vector<uint8_t>>> &out) {
    (void)uid;
    (void)uid_len;
    out[1] = {
        {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF},
        {0xD3, 0xF7, 0xD3, 0xF7, 0xD3, 0xF7}
    };
}

bool QidiCodec::can_encode(const Tag &tag) {
    return tag.tag_type == TagType::MIFARE_CLASSIC_1K
        && tag.sector_keys.find(1) != tag.sector_keys.end()
        && tag.readable.size() > 4
        && tag.readable[4];
}

bool QidiCodec::decode(const Tag &tag, DecodeResult &out) {
    if ((tag.tag_type != TagType::MIFARE_CLASSIC_1K && tag.tag_type != TagType::MIFARE_MINI)
            || tag.readable.size() <= 4
            || !tag.readable[4]) {
        return false;
    }

    std::vector<uint8_t> b4 = tag.get_block(4);
    if (b4.size() != 16) {
        return false;
    }

    uint8_t m = b4[0];
    uint8_t c = b4[1];
    uint8_t v = b4[2];

    const auto *mat = qidi_data::find_material(m);
    if (!mat) {
        return false;
    }
    const char *color_hex = qidi_data::find_color(c);
    if (!color_hex) {
        return false;
    }
    const char *brand = qidi_data::find_vendor(v);
    if (!brand) {
        return false;
    }

    for (size_t i = 3; i < 16; ++i) {
        if (b4[i] != 0) {
            return false;
        }
    }

    if (tag.readable.size() > 5 && tag.readable[5]) {
        auto b5 = tag.get_block(5);
        for (uint8_t b : b5) {
            if (b != 0) return false;
        }
    }
    if (tag.readable.size() > 6 && tag.readable[6]) {
        auto b6 = tag.get_block(6);
        for (uint8_t b : b6) {
            if (b != 0) return false;
        }
    }

    out.recognized = true;
    out.format = "qidi";
    out.filament = Filament{};
    out.filament.brand = brand;
    out.filament.material = mat->type;
    out.filament.name = mat->name;
    out.filament.color_hex = color_hex;
    out.filament.colors = { color_hex };
    out.filament.nozzle_min = mat->nozzle_temp_min;
    out.filament.nozzle_max = mat->nozzle_temp_max;

    out.info.clear();
    out.info["material_code"] = std::to_string(m);
    out.info["color_code"] = std::to_string(c);
    out.info["vendor_code"] = std::to_string(v);
    return true;
}

bool QidiCodec::encode(Tag &tag, const Filament &fields, std::string &err) {
    std::string name_req = to_lower(trim(fields.name));
    std::string mat_req = to_lower(trim(fields.material));

    int matched_m = -1;
    if (!name_req.empty()) {
        for (const auto &item : qidi_data::MATERIALS_TABLE) {
            if (to_lower(item.name) == name_req) {
                matched_m = item.code;
                break;
            }
        }
    }
    if (matched_m < 0 && !mat_req.empty()) {
        for (const auto &item : qidi_data::MATERIALS_TABLE) {
            if (to_lower(item.type) == mat_req) {
                matched_m = item.code;
                break;
            }
        }
    }
    if (matched_m < 0) {
        std::vector<std::string> valid_names;
        for (const auto &item : qidi_data::MATERIALS_TABLE) {
            valid_names.push_back(item.name);
        }
        std::sort(valid_names.begin(), valid_names.end());
        std::string joined;
        for (size_t i = 0; i < valid_names.size(); ++i) {
            if (i > 0) joined += ", ";
            joined += valid_names[i];
        }
        err = "unknown material; valid names: " + joined;
        return false;
    }

    std::string color_hex = fields.color_hex;
    if (color_hex.empty()) {
        err = "color_hex is required";
        return false;
    }
    std::string color_clean = trim(color_hex);
    if (!color_clean.empty() && color_clean[0] == '#') {
        color_clean = color_clean.substr(1);
    }
    color_clean = to_upper(color_clean);
    if (color_clean.size() != 6) {
        err = "invalid color_hex: " + color_hex;
        return false;
    }
    for (char c : color_clean) {
        if (!((c >= '0' && c <= '9') || (c >= 'A' && c <= 'F'))) {
            err = "invalid color_hex: " + color_hex;
            return false;
        }
    }

    int tr = std::stoi(color_clean.substr(0, 2), nullptr, 16);
    int tg = std::stoi(color_clean.substr(2, 2), nullptr, 16);
    int tb = std::stoi(color_clean.substr(4, 2), nullptr, 16);

    int matched_c = -1;
    for (const auto &col : qidi_data::COLORS_TABLE) {
        if (col.hex == color_clean) {
            matched_c = col.code;
            break;
        }
    }
    if (matched_c < 0) {
        int best_dist = -1;
        for (const auto &col : qidi_data::COLORS_TABLE) {
            std::string chex(col.hex);
            int cr = std::stoi(chex.substr(0, 2), nullptr, 16);
            int cg = std::stoi(chex.substr(2, 2), nullptr, 16);
            int cb = std::stoi(chex.substr(4, 2), nullptr, 16);
            int dist = (tr - cr)*(tr - cr) + (tg - cg)*(tg - cg) + (tb - cb)*(tb - cb);
            if (best_dist < 0 || dist < best_dist) {
                best_dist = dist;
                matched_c = col.code;
            }
        }
    }

    std::string brand = to_upper(trim(fields.brand));
    uint8_t v = (brand == "QIDI") ? 1 : 0;

    uint8_t patch[16] = {0};
    patch[0] = static_cast<uint8_t>(matched_m);
    patch[1] = static_cast<uint8_t>(matched_c);
    patch[2] = v;

    if (tag.block_count <= 4 || tag.block_size != 16) {
        err = "qidi: tag memory not allocated or invalid block size";
        return false;
    }
    tag.set_block(4, patch, 16);
    return true;
}

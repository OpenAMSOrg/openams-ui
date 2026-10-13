// Copyright (C) 2026 JR Lomas <lomas.jr@gmail.com>
// Based on OpenRFID and canvas-rfid-tool (GPLv3/MIT).
// This file may be distributed under the terms of the GNU GPLv3 license.

#include "elegoo.h"
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>

static const char *get_base_material(uint8_t fam_id) {
    switch (fam_id) {
        case 0x00: return "PLA";
        case 0x01: return "PETG";
        case 0x02: return "ABS";
        case 0x03: return "TPU";
        case 0x04: return "PA";
        case 0x05: return "CPE";
        case 0x06: return "PC";
        case 0x07: return "PVA";
        case 0x08: return "ASA";
        case 0x09: return "BVOH";
        case 0x0A: return "EVA";
        case 0x0B: return "HIPS";
        case 0x0C: return "PP";
        case 0x0D: return "PPA";
        case 0x0E: return "PPS";
        default:   return nullptr;
    }
}

static std::vector<std::string> get_modifiers(uint8_t fam_id, uint8_t var_id) {
    switch (fam_id) {
        case 0x00: // PLA
            switch (var_id) {
                case 0x01: return {"+"};
                case 0x02: return {"Pro"};
                case 0x03: return {"Silk"};
                case 0x04: return {"CF"};
                case 0x05: return {"Carbon"};
                case 0x06: return {"Matte"};
                case 0x07: return {"Fluo"};
                case 0x08: return {"Wood"};
                case 0x09: return {"Basic"};
                case 0x0A: return {"RAPID", "+"};
                case 0x0B: return {"Marble"};
                case 0x0C: return {"Galaxy"};
                case 0x0D: return {"Red", "Copper"};
                case 0x0E: return {"Sparkle"};
                default:   return {};
            }
        case 0x01: // PETG
            switch (var_id) {
                case 0x01: return {"CF"};
                case 0x02: return {"GF"};
                case 0x03: return {"Pro"};
                case 0x04: return {"Translucent"};
                case 0x05: return {"RAPID"};
                default:   return {};
            }
        case 0x02: // ABS
            switch (var_id) {
                case 0x01: return {"GF"};
                default:   return {};
            }
        case 0x03: // TPU
            switch (var_id) {
                case 0x01: return {"95A"};
                case 0x02: return {"RAPID", "95A"};
                default:   return {};
            }
        case 0x04: // PA
            switch (var_id) {
                case 0x01: return {"CF"};
                case 0x03: return {"HT", "CF"};
                case 0x04: return {"6"};
                case 0x05: return {"6", "CF"};
                case 0x06: return {"12"};
                case 0x07: return {"12", "CF"};
                default:   return {};
            }
        case 0x05: // CPE
            return {};
        case 0x06: // PC
            switch (var_id) {
                case 0x01: return {"TG"};
                case 0x02: return {"FR"};
                default:   return {};
            }
        case 0x07: // PVA
        case 0x08: // ASA
        case 0x09: // BVOH
        case 0x0A: // EVA
        case 0x0B: // HIPS
            return {};
        case 0x0C: // PP
            switch (var_id) {
                case 0x01: return {"CF"};
                case 0x02: return {"GF"};
                default:   return {};
            }
        case 0x0D: // PPA
            switch (var_id) {
                case 0x01: return {"CF"};
                case 0x02: return {"GF"};
                default:   return {};
            }
        case 0x0E: // PPS
            switch (var_id) {
                case 0x02: return {"CF"};
                default:   return {};
            }
        default:
            return {};
    }
}

std::vector<std::string> ElegooCodec::tag_types() const {
    return {
        TagTypes::NTAG213,
        TagTypes::NTAG215,
        TagTypes::NTAG216,
        TagTypes::ULTRALIGHT,
        TagTypes::ULTRALIGHT_EV1,
        TagTypes::TYPE2
    };
}

bool ElegooCodec::decode(const Tag &tag, DecodeResult &out) {
    if (tag.memory.size() < 96) {
        return false;
    }

    // Check page 16 (0x40) and page 17 byte 0 (0x44)
    if (tag.memory[0x40] != 0x36 ||
        tag.memory[0x41] != 0xEE ||
        tag.memory[0x42] != 0xEE ||
        tag.memory[0x43] != 0xEE ||
        tag.memory[0x44] != 0xEE) {
        return false;
    }

    // Page 18: BCD ASCII representation
    std::string bcd_mat;
    for (size_t i = 0x48; i < 0x4C; ++i) {
        uint8_t b = tag.memory[i];
        if (b != 0) {
            char h[3];
            snprintf(h, sizeof(h), "%02x", b);
            if (h[0] >= '0' && h[0] <= '9' && h[1] >= '0' && h[1] <= '9') {
                int val = (h[0] - '0') * 10 + (h[1] - '0');
                if (val >= 0 && val <= 127) {
                    bcd_mat += static_cast<char>(val);
                }
            }
        }
    }

    uint8_t fam_id = tag.memory[0x4C];
    uint8_t var_id = tag.memory[0x4D];

    const char *base_lookup = get_base_material(fam_id);
    std::string base = (base_lookup != nullptr) ? base_lookup : (!bcd_mat.empty() ? bcd_mat : "PLA");
    std::vector<std::string> mods = get_modifiers(fam_id, var_id);

    auto it6 = std::find(mods.begin(), mods.end(), "6");
    if (it6 != mods.end()) {
        base += "6";
        mods.erase(it6);
    } else {
        auto it12 = std::find(mods.begin(), mods.end(), "12");
        if (it12 != mods.end()) {
            base += "12";
            mods.erase(it12);
        }
    }

    std::string material = base;
    for (auto &c : material) c = std::toupper(static_cast<unsigned char>(c));

    std::string name;
    if (!mods.empty()) {
        name = base;
        for (const auto &m : mods) {
            name += " " + m;
        }
    } else {
        name = base;
    }

    uint8_t r = tag.memory[0x50];
    uint8_t g = tag.memory[0x51];
    uint8_t b = tag.memory[0x52];
    char cbuf[7];
    snprintf(cbuf, sizeof(cbuf), "%02X%02X%02X", r, g, b);
    std::string color_hex = cbuf;
    std::vector<std::string> colors = { color_hex };

    uint16_t n_min = (static_cast<uint16_t>(tag.memory[0x54]) << 8) | tag.memory[0x55];
    uint16_t n_max = (static_cast<uint16_t>(tag.memory[0x56]) << 8) | tag.memory[0x57];

    uint16_t d_raw = (static_cast<uint16_t>(tag.memory[0x5C]) << 8) | tag.memory[0x5D];
    uint16_t w_raw = (static_cast<uint16_t>(tag.memory[0x5E]) << 8) | tag.memory[0x5F];
    double diameter_mm = (d_raw != 0) ? (d_raw / 100.0) : 0.0;
    int weight_g = w_raw;

    Filament fil;
    fil.brand = "Elegoo";
    fil.material = material;
    fil.name = name;
    fil.color_hex = color_hex;
    fil.colors = colors;
    fil.diameter_mm = diameter_mm;
    fil.weight_g = weight_g;
    fil.nozzle_min = n_min;
    fil.nozzle_max = n_max;

    out.info.clear();
    out.info["family_id"] = std::to_string(fam_id);
    out.info["variant_id"] = std::to_string(var_id);
    if (!bcd_mat.empty()) out.info["raw_material"] = bcd_mat;
    std::string mod_str;
    for (size_t i = 0; i < mods.size(); ++i) {
        if (i > 0) mod_str += " ";
        mod_str += mods[i];
    }
    if (!mod_str.empty()) out.info["modifiers"] = mod_str;

    out.recognized = true;
    out.format = "elegoo";
    out.filament = fil;
    return true;
}

// Copyright (C) 2026 JR Lomas <lomas.jr@gmail.com>
// This file may be distributed under the terms of the GNU GPLv3 license.

#include "openprinttag.h"
#include "cbor.h"
#include "ndef.h"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <ctime>

namespace openprinttag_data {

const char * const MIME_TYPE = "application/vnd.openprinttag";

// The tables are constant arrays, read from flash: a global std::map is built
// on the heap by a static constructor at boot, whether or not a tag is ever
// read, and on the display that heap is the printer link's.
struct EnumName {
    int key;
    const char *name;
};

struct MaterialDefaultsEntry {
    int type;  // a MATERIAL_TYPE key
    MaterialDefaults defaults;
};

const EnumName MATERIAL_CLASS[] = {
    {0, "FFF"},
    {1, "SLA"}
};

const EnumName MATERIAL_TYPE[] = {
    {0, "PLA"},
    {1, "PETG"},
    {2, "TPU"},
    {3, "ABS"},
    {4, "ASA"},
    {5, "PC"},
    {6, "PCTG"},
    {7, "PP"},
    {8, "PA6"},
    {9, "PA11"},
    {10, "PA12"},
    {11, "PA66"},
    {12, "CPE"},
    {13, "TPE"},
    {14, "HIPS"},
    {15, "PHA"},
    {16, "PET"},
    {17, "PEI"},
    {18, "PBT"},
    {19, "PVB"},
    {20, "PVA"},
    {21, "PEKK"},
    {22, "PEEK"},
    {23, "BVOH"},
    {24, "TPC"},
    {25, "PPS"},
    {26, "PPSU"},
    {27, "PVC"},
    {28, "PEBA"},
    {29, "PVDF"},
    {30, "PPA"},
    {31, "PCL"},
    {32, "PES"},
    {33, "PMMA"},
    {34, "POM"},
    {35, "PPE"},
    {36, "PS"},
    {37, "PSU"},
    {38, "TPI"},
    {39, "SBS"},
    {40, "OBC"},
    {41, "EVA"},
    {42, "PA612"}
};

const EnumName MATERIAL_TYPE_NAME[] = {
    {0, "Polylactic Acid"},
    {1, "Polyethylene Terephthalate Glycol"},
    {2, "Thermoplastic Polyurethane"},
    {3, "Acrylonitrile Butadiene Styrene"},
    {4, "Acrylonitrile Styrene Acrylate"},
    {5, "Polycarbonate"},
    {6, "Polycyclohexylenedimethylene Terephthalate Glycol"},
    {7, "Polypropylene"},
    {8, "Polyamide 6"},
    {9, "Polyamide 11"},
    {10, "Polyamide 12"},
    {11, "Polyamide 66"},
    {12, "Copolyester"},
    {13, "Thermoplastic Elastomer"},
    {14, "High Impact Polystyrene"},
    {15, "Polyhydroxyalkanoate"},
    {16, "Polyethylene Terephthalate"},
    {17, "Polyetherimide"},
    {18, "Polybutylene Terephthalate"},
    {19, "Polyvinyl Butyral"},
    {20, "Polyvinyl Alcohol"},
    {21, "Polyetherketoneketone"},
    {22, "Polyether Ether Ketone"},
    {23, "Butenediol Vinyl Alcohol Copolymer"},
    {24, "Thermoplastic Copolyester"},
    {25, "Polyphenylene Sulfide"},
    {26, "Polyphenylsulfone"},
    {27, "Polyvinyl Chloride"},
    {28, "Polyether Block Amide"},
    {29, "Polyvinylidene Fluoride"},
    {30, "Polyphthalamide"},
    {31, "Polycaprolactone"},
    {32, "Polyethersulfone"},
    {33, "Polymethyl Methacrylate"},
    {34, "Polyoxymethylene"},
    {35, "Polyphenylene Ether"},
    {36, "Polystyrene"},
    {37, "Polysulfone"},
    {38, "Thermoplastic Polyimide"},
    {39, "Styrene-Butadiene-Styrene"},
    {40, "Olefin Block Copolymer"},
    {41, "Ethylene Vinyl Acetate"},
    {42, "Polyamide 612"}
};

const MaterialDefaultsEntry MATERIAL_TYPE_DEFAULTS[] = {
    {0, {180, 220, 0, 60, 25, 15, 35, 45, 240, 160, 45, 1.24, 0.0}},
    {1, {220, 250, 60, 80, 30, 15, 50, 65, 360, 170, 60, 1.27, 0.0}},
    {2, {210, 240, 20, 60, 25, 15, 40, 60, 360, 160, 40, 1.21, 0.4}},
    {3, {230, 260, 90, 110, 50, 40, 80, 80, 240, 180, 70, 1.04, 0.0}},
    {4, {230, 260, 90, 110, 50, 40, 80, 80, 240, 180, 70, 1.07, 0.0}},
    {5, {270, 310, 110, 130, 80, 60, 100, 120, 480, 200, 100, 1.20, 0.0}},
    {7, {220, 250, 80, 100, 50, 30, 70, 60, 120, 150, 50, 0.90, 0.0}},
    {14, {220, 250, 80, 100, 45, 35, 75, 70, 240, 170, 65, 1.04, 0.0}},
    {16, {255, 280, 70, 90, 40, 20, 60, 65, 360, 190, 60, 1.38, 0.0}},
    {19, {200, 225, 60, 75, 25, 15, 45, 55, 360, 160, 50, 1.10, 0.0}}
};

const EnumName TAGS[] = {
    {0, "filtration_recommended"},
    {1, "biocompatible"},
    {2, "antibacterial"},
    {3, "air_filtering"},
    {4, "abrasive"},
    {5, "foaming"},
    {6, "self_extinguishing"},
    {7, "paramagnetic"},
    {8, "radiation_shielding"},
    {9, "high_temperature"},
    {10, "esd_safe"},
    {11, "conductive"},
    {12, "blend"},
    {13, "water_soluble"},
    {14, "ipa_soluble"},
    {15, "limonene_soluble"},
    {16, "matte"},
    {17, "silk"},
    {19, "translucent"},
    {20, "transparent"},
    {21, "iridescent"},
    {22, "pearlescent"},
    {23, "glitter"},
    {24, "glow_in_the_dark"},
    {25, "neon"},
    {26, "illuminescent_color_change"},
    {27, "temperature_color_change"},
    {28, "gradual_color_change"},
    {29, "coextruded"},
    {30, "contains_carbon"},
    {31, "contains_carbon_fiber"},
    {32, "contains_carbon_nano_tubes"},
    {33, "contains_glass"},
    {34, "contains_glass_fiber"},
    {35, "contains_kevlar"},
    {36, "contains_stone"},
    {37, "contains_magnetite"},
    {38, "contains_organic_material"},
    {39, "contains_cork"},
    {40, "contains_wax"},
    {41, "contains_wood"},
    {42, "contains_bamboo"},
    {43, "contains_pine"},
    {44, "contains_ceramic"},
    {45, "contains_boron_carbide"},
    {46, "contains_metal"},
    {47, "contains_bronze"},
    {48, "contains_iron"},
    {49, "contains_steel"},
    {50, "contains_silver"},
    {51, "contains_copper"},
    {52, "contains_aluminium"},
    {53, "contains_brass"},
    {54, "contains_tungsten"},
    {55, "imitates_wood"},
    {56, "imitates_metal"},
    {57, "imitates_marble"},
    {58, "imitates_stone"},
    {59, "lithophane"},
    {60, "recycled"},
    {61, "home_compostable"},
    {62, "industrially_compostable"},
    {63, "bio_based"},
    {64, "low_outgassing"},
    {65, "without_pigments"},
    {66, "contains_algae"},
    {67, "castable"},
    {68, "contains_ptfe"},
    {69, "limited_edition"},
    {70, "emi_shielding"},
    {71, "high_speed"},
    {72, "contains_graphene"},
    {73, "uv_resistant"},
    {74, "acetone_soluble"}
};

const EnumName CERTIFICATIONS[] = {
    {0, "ul_2818"},
    {1, "ul_94_v0"},
    {2, "ul_2904"}
};

const EnumName WRITE_PROTECTION[] = {
    {0, "no"},
    {1, "irreversible"},
    {2, "protect_page_unlockable"}
};

bool lookup_material_defaults(const std::string &material, int &min_temp, int &max_temp) {
    std::string mat_upper = material;
    for (char &c : mat_upper) c = toupper(static_cast<unsigned char>(c));
    for (const EnumName &type : MATERIAL_TYPE) {
        std::string name_upper = type.name;
        for (char &c : name_upper) c = toupper(static_cast<unsigned char>(c));
        if (name_upper == mat_upper) {
            for (const MaterialDefaultsEntry &e : MATERIAL_TYPE_DEFAULTS) {
                if (e.type == type.key) {
                    min_temp = e.defaults.min_print_temperature;
                    max_temp = e.defaults.max_print_temperature;
                    return min_temp > 0 && max_temp > 0;
                }
            }
        }
    }
    return false;
}

} // namespace openprinttag_data

struct FieldDesc {
    const char *name;
    const char *type;
    const char *enum_table;
};

struct Field {
    int key;
    FieldDesc desc;
};

template <size_t N>
static const FieldDesc *find_field(const Field (&table)[N], int key) {
    for (const Field &f : table)
        if (f.key == key) return &f.desc;
    return nullptr;
}

static const Field META_FIELDS[] = {
    {0, {"main_region_offset", "int", nullptr}},
    {1, {"main_region_size", "int", nullptr}},
    {2, {"aux_region_offset", "int", nullptr}},
    {3, {"aux_region_size", "int", nullptr}}
};

static const Field MAIN_FIELDS[] = {
    {0, {"instance_uuid", "uuid", nullptr}},
    {1, {"package_uuid", "uuid", nullptr}},
    {2, {"material_uuid", "uuid", nullptr}},
    {3, {"brand_uuid", "uuid", nullptr}},
    {4, {"gtin", "int", nullptr}},
    {5, {"brand_specific_instance_id", "string", nullptr}},
    {6, {"brand_specific_package_id", "string", nullptr}},
    {7, {"brand_specific_material_id", "string", nullptr}},
    {8, {"material_class", "enum", "MATERIAL_CLASS"}},
    {9, {"material_type", "enum", "MATERIAL_TYPE"}},
    {10, {"material_name", "string", nullptr}},
    {11, {"brand_name", "string", nullptr}},
    {13, {"write_protection", "enum", "WRITE_PROTECTION"}},
    {14, {"manufactured_date", "timestamp", nullptr}},
    {15, {"expiration_date", "timestamp", nullptr}},
    {16, {"nominal_netto_full_weight", "number", nullptr}},
    {17, {"actual_netto_full_weight", "number", nullptr}},
    {18, {"empty_container_weight", "number", nullptr}},
    {19, {"primary_color", "color_rgba", nullptr}},
    {20, {"secondary_color_0", "color_rgba", nullptr}},
    {21, {"secondary_color_1", "color_rgba", nullptr}},
    {22, {"secondary_color_2", "color_rgba", nullptr}},
    {23, {"secondary_color_3", "color_rgba", nullptr}},
    {24, {"secondary_color_4", "color_rgba", nullptr}},
    {27, {"transmission_distance", "number", nullptr}},
    {28, {"tags", "enum_array", "TAGS"}},
    {29, {"density", "number", nullptr}},
    {30, {"filament_diameter", "number", nullptr}},
    {31, {"hardness_shore_a", "int", nullptr}},
    {32, {"hardness_shore_d", "int", nullptr}},
    {33, {"min_nozzle_diameter", "number", nullptr}},
    {34, {"min_print_temperature", "int", nullptr}},
    {35, {"max_print_temperature", "int", nullptr}},
    {36, {"preheat_temperature", "int", nullptr}},
    {37, {"min_bed_temperature", "int", nullptr}},
    {38, {"max_bed_temperature", "int", nullptr}},
    {39, {"min_chamber_temperature", "int", nullptr}},
    {40, {"max_chamber_temperature", "int", nullptr}},
    {41, {"chamber_temperature", "int", nullptr}},
    {42, {"container_width", "int", nullptr}},
    {43, {"container_outer_diameter", "int", nullptr}},
    {44, {"container_inner_diameter", "int", nullptr}},
    {45, {"container_hole_diameter", "int", nullptr}},
    {46, {"viscosity_18c", "number", nullptr}},
    {47, {"viscosity_25c", "number", nullptr}},
    {48, {"viscosity_40c", "number", nullptr}},
    {49, {"viscosity_60c", "number", nullptr}},
    {50, {"container_volumetric_capacity", "number", nullptr}},
    {51, {"cure_wavelength", "int", nullptr}},
    {52, {"material_abbreviation", "string", nullptr}},
    {53, {"nominal_full_length", "number", nullptr}},
    {54, {"actual_full_length", "number", nullptr}},
    {55, {"country_of_origin", "string", nullptr}},
    {56, {"certifications", "enum_array", "CERTIFICATIONS"}},
    {57, {"drying_temperature", "int", nullptr}},
    {58, {"drying_time", "int", nullptr}},
    {59, {"primary_color_lab", "color_lab", nullptr}},
    {60, {"primary_color_ral", "string", nullptr}}
};

static const Field AUX_FIELDS[] = {
    {0, {"consumed_weight", "number", nullptr}},
    {1, {"workgroup", "string", nullptr}},
    {2, {"general_purpose_range_user", "string", nullptr}},
    {3, {"last_stir_time", "timestamp", nullptr}},
    {4, {"storage_location", "string", nullptr}},
    {5, {"purchase_time", "timestamp", nullptr}},
    {6, {"purchase_price", "number", nullptr}},
    {7, {"purchase_currency", "string", nullptr}}
};

// SHA-1 implementation for UUIDv5
static void sha1(const uint8_t *msg, size_t len, uint8_t digest[20]) {
    uint32_t h0 = 0x67452301;
    uint32_t h1 = 0xEFCDAB89;
    uint32_t h2 = 0x98BADCFE;
    uint32_t h3 = 0x10325476;
    uint32_t h4 = 0xC3D2E1F0;

    size_t new_len = (((len + 8) / 64) + 1) * 64;
    std::vector<uint8_t> p(new_len, 0);
    std::memcpy(p.data(), msg, len);
    p[len] = 0x80;

    uint64_t bits = static_cast<uint64_t>(len) * 8;
    for (int i = 0; i < 8; ++i) {
        p[new_len - 1 - i] = static_cast<uint8_t>((bits >> (i * 8)) & 0xFF);
    }

    for (size_t chunk = 0; chunk < new_len; chunk += 64) {
        uint32_t w[80];
        for (int i = 0; i < 16; ++i) {
            w[i] = (static_cast<uint32_t>(p[chunk + i * 4]) << 24) |
                   (static_cast<uint32_t>(p[chunk + i * 4 + 1]) << 16) |
                   (static_cast<uint32_t>(p[chunk + i * 4 + 2]) << 8) |
                   (static_cast<uint32_t>(p[chunk + i * 4 + 3]));
        }
        for (int i = 16; i < 80; ++i) {
            uint32_t v = w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16];
            w[i] = (v << 1) | (v >> 31);
        }

        uint32_t a = h0;
        uint32_t b = h1;
        uint32_t c = h2;
        uint32_t d = h3;
        uint32_t e = h4;

        for (int i = 0; i < 80; ++i) {
            uint32_t f, k;
            if (i < 20) {
                f = (b & c) | ((~b) & d);
                k = 0x5A827999;
            } else if (i < 40) {
                f = b ^ c ^ d;
                k = 0x6ED9EBA1;
            } else if (i < 60) {
                f = (b & c) | (b & d) | (c & d);
                k = 0x8F1BBCDC;
            } else {
                f = b ^ c ^ d;
                k = 0xCA62C1D6;
            }
            uint32_t temp = ((a << 5) | (a >> 27)) + f + e + k + w[i];
            e = d;
            d = c;
            c = (b << 30) | (b >> 2);
            b = a;
            a = temp;
        }

        h0 += a;
        h1 += b;
        h2 += c;
        h3 += d;
        h4 += e;
    }

    uint32_t h[5] = {h0, h1, h2, h3, h4};
    for (int i = 0; i < 5; ++i) {
        digest[i * 4] = static_cast<uint8_t>((h[i] >> 24) & 0xFF);
        digest[i * 4 + 1] = static_cast<uint8_t>((h[i] >> 16) & 0xFF);
        digest[i * 4 + 2] = static_cast<uint8_t>((h[i] >> 8) & 0xFF);
        digest[i * 4 + 3] = static_cast<uint8_t>(h[i] & 0xFF);
    }
}

static std::string format_uuid(const uint8_t bytes[16]) {
    char buf[37];
    snprintf(buf, sizeof(buf),
             "%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x",
             bytes[0], bytes[1], bytes[2], bytes[3],
             bytes[4], bytes[5],
             bytes[6], bytes[7],
             bytes[8], bytes[9],
             bytes[10], bytes[11], bytes[12], bytes[13], bytes[14], bytes[15]);
    return std::string(buf);
}

static bool parse_uuid(const std::string &str, uint8_t out[16]) {
    std::string hex;
    for (char c : str) {
        if (c != '-') hex += c;
    }
    if (hex.size() != 32) return false;
    for (size_t i = 0; i < 16; ++i) {
        char b[3] = {hex[i * 2], hex[i * 2 + 1], '\0'};
        out[i] = static_cast<uint8_t>(std::strtoul(b, nullptr, 16));
    }
    return true;
}

static const uint8_t NS_BRAND[16] = {0x52, 0x69, 0xdf, 0xb7, 0x15, 0x59, 0x44, 0x0a, 0x85, 0xbe, 0xab, 0xa5, 0xf3, 0xef, 0xf2, 0xd2};
static const uint8_t NS_MATERIAL[16] = {0x61, 0x6f, 0xc8, 0x6d, 0x7d, 0x99, 0x49, 0x53, 0x96, 0xc7, 0x46, 0xd2, 0x83, 0x6b, 0x9b, 0xe9};
static const uint8_t NS_PACKAGE[16] = {0x6f, 0x7d, 0x48, 0x5e, 0xdb, 0x8d, 0x49, 0x79, 0x90, 0x4e, 0xa2, 0x31, 0xcd, 0x66, 0x02, 0xb2};
static const uint8_t NS_INSTANCE[16] = {0x31, 0x06, 0x2f, 0x81, 0xb5, 0xbd, 0x4f, 0x86, 0xa5, 0xf8, 0x46, 0x36, 0x7e, 0x84, 0x15, 0x08};

static std::string uuid5(const uint8_t ns[16], const std::vector<uint8_t> &name) {
    std::vector<uint8_t> data(ns, ns + 16);
    data.insert(data.end(), name.begin(), name.end());
    uint8_t digest[20];
    sha1(data.data(), data.size(), digest);
    digest[6] = (digest[6] & 0x0F) | 0x50; // Version 5
    digest[8] = (digest[8] & 0x3F) | 0x80; // RFC 4122 variant
    return format_uuid(digest);
}

static cbor::CborValue compact_float(double num) {
    if (std::floor(num) == num) {
        return cbor::CborValue::from_int(static_cast<int64_t>(num));
    }
    return cbor::CborValue::from_float(num);
}

template <size_t N>
static const char *find_name(const openprinttag_data::EnumName (&table)[N], int key) {
    for (const openprinttag_data::EnumName &e : table)
        if (e.key == key) return e.name;
    return nullptr;
}

// The name of value @p key in the enum table called @p table; nullptr for an
// unknown table or value.
static const char *enum_name(const char *table, int key) {
    if (!table) return nullptr;
    if (std::strcmp(table, "MATERIAL_CLASS") == 0) return find_name(openprinttag_data::MATERIAL_CLASS, key);
    if (std::strcmp(table, "MATERIAL_TYPE") == 0) return find_name(openprinttag_data::MATERIAL_TYPE, key);
    if (std::strcmp(table, "TAGS") == 0) return find_name(openprinttag_data::TAGS, key);
    if (std::strcmp(table, "CERTIFICATIONS") == 0) return find_name(openprinttag_data::CERTIFICATIONS, key);
    if (std::strcmp(table, "WRITE_PROTECTION") == 0) return find_name(openprinttag_data::WRITE_PROTECTION, key);
    return nullptr;
}

static std::string decode_field_val_string(const cbor::CborValue &val, const char *type_name, const char *enum_table) {
    if (std::strcmp(type_name, "int") == 0 || std::strcmp(type_name, "timestamp") == 0) {
        return std::to_string(val.as_int64());
    }
    if (std::strcmp(type_name, "number") == 0) {
        if (val.is_int()) return std::to_string(val.as_int64());
        double d = val.as_float();
        if (std::floor(d) == d && d >= -9.2e18 && d <= 9.2e18)
            return std::to_string(static_cast<int64_t>(d));
        char buf[32];
        snprintf(buf, sizeof(buf), "%.3f", d);
        return buf;
    }
    if (std::strcmp(type_name, "string") == 0) {
        return val.as_text();
    }
    if (std::strcmp(type_name, "uuid") == 0) {
        if (val.is_bytes() && val.as_bytes().size() == 16) {
            return format_uuid(val.as_bytes().data());
        }
        return "";
    }
    if (std::strcmp(type_name, "enum") == 0) {
        int key = static_cast<int>(val.as_int64());
        const char *name = enum_name(enum_table, key);
        if (name) return name;
        return std::to_string(key);
    }
    if (std::strcmp(type_name, "enum_array") == 0) {
        std::string out = "[";
        if (val.is_array()) {
            for (size_t i = 0; i < val.as_array().size(); ++i) {
                if (i > 0) out += ", ";
                int key = static_cast<int>(val.as_array()[i].as_int64());
                const char *name = enum_name(enum_table, key);
                if (name) out += name;
                else out += std::to_string(key);
            }
        }
        out += "]";
        return out;
    }
    if (std::strcmp(type_name, "color_rgba") == 0) {
        if (val.is_bytes()) {
            std::string out = "#";
            char buf[4];
            for (uint8_t b : val.as_bytes()) {
                snprintf(buf, sizeof(buf), "%02x", b);
                out += buf;
            }
            return out;
        }
        return "";
    }
    if (std::strcmp(type_name, "bool") == 0) {
        return val.as_bool() ? "true" : "false";
    }
    return "";
}

static bool region_layout(const std::map<int, int> &meta,
                          size_t meta_used,
                          size_t payload_len,
                          size_t &main_off, size_t &main_sz,
                          bool &has_aux, size_t &aux_off, size_t &aux_sz,
                          std::string &err) {
    auto get_num = [&](int key, int def) -> int {
        auto it = meta.find(key);
        if (it != meta.end()) {
            if (it->second < 0) {
                err = "invalid meta field " + std::to_string(key);
                return -1;
            }
            return it->second;
        }
        return def;
    };

    int m_off = get_num(0, static_cast<int>(meta_used));
    if (m_off < 0) return false;
    main_off = static_cast<size_t>(m_off);

    auto it_aux = meta.find(2);
    has_aux = (it_aux != meta.end() && it_aux->second >= 0);
    int a_off = has_aux ? it_aux->second : -1;
    if (has_aux) aux_off = static_cast<size_t>(a_off);

    std::vector<size_t> starts;
    starts.push_back(main_off);
    if (has_aux) starts.push_back(aux_off);
    starts.push_back(payload_len);

    auto get_sz = [&](size_t off, int explicit_sz) -> int {
        if (explicit_sz >= 0) return explicit_sz;
        size_t next_start = payload_len;
        for (size_t s : starts) {
            if (s > off && s < next_start) {
                next_start = s;
            }
        }
        return static_cast<int>(next_start - off);
    };

    int m_sz_exp = get_num(1, -1);
    int m_sz = get_sz(main_off, m_sz_exp);
    if (m_sz < 0) { err = "region outside the payload"; return false; }
    main_sz = static_cast<size_t>(m_sz);

    if (has_aux) {
        int a_sz_exp = get_num(3, -1);
        int a_sz = get_sz(aux_off, a_sz_exp);
        if (a_sz < 0) { err = "region outside the payload"; return false; }
        aux_sz = static_cast<size_t>(a_sz);
    }

    if (main_off < meta_used || main_off + main_sz > payload_len) {
        err = "region outside the payload";
        return false;
    }
    if (main_sz > 512) {
        err = "region exceeds 512 bytes";
        return false;
    }
    if (has_aux) {
        if (aux_off < meta_used || aux_off + aux_sz > payload_len) {
            err = "region outside the payload";
            return false;
        }
        if (aux_sz > 512) {
            err = "region exceeds 512 bytes";
            return false;
        }
        if (main_off < aux_off + aux_sz && aux_off < main_off + main_sz) {
            err = "main and aux regions overlap";
            return false;
        }
    }
    return true;
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
        case TagType::TYPE5: ntag.tag_type = ndef::TagType::Type5; break;
        default:
            if (is_type2_tag(tag.tag_type)) ntag.tag_type = ndef::TagType::Type2;
            else if (is_type5_tag(tag.tag_type)) ntag.tag_type = ndef::TagType::Type5;
            else if (is_mifare_classic_tag(tag.tag_type)) ntag.tag_type = ndef::TagType::MifareClassic;
            else ntag.tag_type = ndef::TagType::Unknown;
            break;
    }
    ntag.memory = tag.memory;
    ntag.block_size = tag.block_size;
    ntag.user_first_block = tag.user_first_block >= 0 ? tag.user_first_block : 0;
    ntag.user_last_block = tag.user_last_block >= 0 ? tag.user_last_block : 0;
    return ntag;
}

static bool build_image(const cbor::CborValue &main_val,
                        const cbor::CborValue *aux_val,
                        size_t size,
                        size_t block_size,
                        std::vector<uint8_t> &image_out,
                        std::string &err) {
    if (size < 16) {
        err = "size too small";
        return false;
    }
    std::vector<uint8_t> cc = {0xE1, 0x40, static_cast<uint8_t>(size / 8), 0x01};
    uint8_t tlv_term = 0xFE;

    size_t ndef_tlv_header_size = 2;
    size_t ndef_message_length = size - 4 - 1 - ndef_tlv_header_size;
    if (ndef_message_length > 0xFE) {
        ndef_tlv_header_size += 2;
        ndef_message_length -= 2;
    }
    std::vector<uint8_t> ndef_tlv_header;
    if (ndef_message_length <= 0xFE) {
        ndef_tlv_header = {0x03, static_cast<uint8_t>(ndef_message_length)};
    } else {
        ndef_tlv_header = {
            0x03, 0xFF,
            static_cast<uint8_t>(ndef_message_length >> 8),
            static_cast<uint8_t>(ndef_message_length & 0xFF)
        };
    }

    std::string mime = openprinttag_data::MIME_TYPE;
    size_t ndef_header_size = 3 + mime.size();
    size_t ndef_payload_start = 4 + ndef_tlv_header_size + ndef_header_size;
    size_t payload_size = ndef_message_length - ndef_header_size;
    if (payload_size > 255) {
        ndef_header_size += 3;
        ndef_payload_start += 3;
        payload_size -= 3;
    }

    std::vector<uint8_t> payload(payload_size, 0);

    auto align_offset = [&](size_t offset, bool align_up) -> size_t {
        size_t mis = (ndef_payload_start + offset) % block_size;
        if (mis == 0) return offset;
        if (align_up) return offset + block_size - mis;
        return offset - mis;
    };

    size_t aux_region_size = 32;
    int aux_region_offset = -1;
    if (payload_size >= aux_region_size) {
        aux_region_offset = static_cast<int>(align_offset(payload_size - aux_region_size, false));
    }

    cbor::CborValue meta_val;
    meta_val.type = cbor::Type::Map;
    if (aux_region_offset >= 0) {
        meta_val.set(int64_t(2), cbor::CborValue::from_int(aux_region_offset));
    }

    std::vector<uint8_t> meta_bytes = cbor::dumps(meta_val, true, false);
    size_t meta_size = meta_bytes.size();
    if (meta_size > payload.size()) {
        err = "metadata exceeds payload";
        return false;
    }
    std::memcpy(payload.data(), meta_bytes.data(), meta_size);

    size_t main_region_offset = meta_size;
    std::vector<uint8_t> main_bytes = cbor::dumps(main_val, true, true);
    if (main_region_offset + main_bytes.size() > payload.size()) {
        err = "main data exceeds payload";
        return false;
    }
    std::memcpy(&payload[main_region_offset], main_bytes.data(), main_bytes.size());

    if (aux_region_offset >= 0 && static_cast<size_t>(aux_region_offset) < payload.size()) {
        if (aux_val && !aux_val->as_map().empty()) {
            std::vector<uint8_t> aux_bytes = cbor::dumps(*aux_val, true, true);
            if (static_cast<size_t>(aux_region_offset) + aux_bytes.size() <= payload.size()) {
                std::memcpy(&payload[aux_region_offset], aux_bytes.data(), aux_bytes.size());
            }
        } else {
            payload[aux_region_offset] = 0xA0; // Empty map
        }
    }

    ndef::NdefRecord rec(ndef::TNF_MIME, mime, payload);
    std::vector<uint8_t> ndef_data = ndef::encode_records({rec});
    if (ndef_data.size() != ndef_message_length) {
        err = "cannot lay out an OpenPrintTag image";
        return false;
    }

    image_out.clear();
    image_out.insert(image_out.end(), cc.begin(), cc.end());
    image_out.insert(image_out.end(), ndef_tlv_header.begin(), ndef_tlv_header.end());
    image_out.insert(image_out.end(), ndef_data.begin(), ndef_data.end());
    image_out.push_back(tlv_term);
    return true;
}

OpenPrintTagCodec::OpenPrintTagCodec(const CodecOptions &options)
    : options_(options) {}

std::vector<std::string> OpenPrintTagCodec::tag_types() const {
    return {
        TagTypes::ICODE_SLIX,
        TagTypes::ICODE_SLIX2,
        TagTypes::ICODE_SLIX_S,
        TagTypes::ICODE_SLIX_L,
        TagTypes::ICODE_DNA,
        TagTypes::TYPE5
    };
}

bool OpenPrintTagCodec::can_encode(const Tag &tag) {
    bool is_type5 = is_type5_tag(tag.tag_type) || (tag.technology == TagTechnology::ISO15693);
    if (!is_type5) return false;
    size_t user_bytes = (tag.user_last_block + 1) * tag.block_size;
    return user_bytes >= 112;
}

bool OpenPrintTagCodec::decode(const Tag &tag, DecodeResult &out) {
    ndef::Tag ntag = to_ndef_tag(tag);
    ndef::NdefMessage msg;
    if (!ndef::read_message(ntag, msg)) {
        return false;
    }

    const ndef::NdefRecord *mime_rec = nullptr;
    std::string uri_str;
    for (const auto &r : msg.records) {
        if (r.type == std::vector<uint8_t>{'U'} && uri_str.empty()) {
            uri_str = r.uri();
        }
        if (r.is_mime(openprinttag_data::MIME_TYPE) && !mime_rec) {
            mime_rec = &r;
        }
    }

    if (!mime_rec || mime_rec->payload.empty()) {
        return false;
    }

    const auto &payload = mime_rec->payload;

    cbor::CborValue meta_cbor;
    size_t meta_used = 0;
    std::string err;
    if (!cbor::loads(payload.data(), payload.size(), meta_cbor, 0, &meta_used, &err) || !meta_cbor.is_map()) {
        return false;
    }

    std::map<int, int> meta_map;
    for (const auto &kv : meta_cbor.as_map()) {
        if (kv.first.is_int() && kv.second.is_int()) {
            meta_map[static_cast<int>(kv.first.as_int64())] = static_cast<int>(kv.second.as_int64());
        }
    }

    size_t main_off = 0, main_sz = 0;
    bool has_aux = false;
    size_t aux_off = 0, aux_sz = 0;
    if (!region_layout(meta_map, meta_used, payload.size(), main_off, main_sz, has_aux, aux_off, aux_sz, err)) {
        return false;
    }

    cbor::CborValue main_cbor;
    size_t main_used = 0;
    if (!cbor::loads(payload.data() + main_off, main_sz, main_cbor, 0, &main_used, &err) || !main_cbor.is_map()) {
        return false;
    }

    std::map<std::string, std::string> main_dict;
    for (const auto &kv : main_cbor.as_map()) {
        if (kv.first.is_int()) {
            int k = static_cast<int>(kv.first.as_int64());
            const FieldDesc *f = find_field(MAIN_FIELDS, k);
            if (f) {
                main_dict[f->name] = decode_field_val_string(kv.second, f->type, f->enum_table);
            }
        }
    }

    std::map<std::string, std::string> aux_dict;
    if (has_aux && aux_sz > 0) {
        cbor::CborValue aux_cbor;
        size_t aux_used = 0;
        if (cbor::loads(payload.data() + aux_off, aux_sz, aux_cbor, 0, &aux_used, &err) && aux_cbor.is_map()) {
            for (const auto &kv : aux_cbor.as_map()) {
                if (kv.first.is_int()) {
                    int k = static_cast<int>(kv.first.as_int64());
                    const FieldDesc *f = find_field(AUX_FIELDS, k);
                    if (f) {
                        aux_dict[f->name] = decode_field_val_string(kv.second, f->type, f->enum_table);
                    }
                }
            }
        }
    }

    out.recognized = true;
    out.format = "openprinttag";
    Filament &fil = out.filament;

    auto it_brand = main_dict.find("brand_name");
    if (it_brand != main_dict.end()) fil.brand = it_brand->second;

    auto it_mat = main_dict.find("material_type");
    if (it_mat != main_dict.end()) {
        fil.material = it_mat->second;
    } else {
        auto it_abbr = main_dict.find("material_abbreviation");
        if (it_abbr != main_dict.end()) fil.material = it_abbr->second;
    }

    auto it_name = main_dict.find("material_name");
    if (it_name != main_dict.end()) fil.name = it_name->second;

    auto it_color = main_dict.find("primary_color");
    if (it_color != main_dict.end() && it_color->second.size() >= 7) {
        std::string chex = it_color->second.substr(1, 6);
        for (char &c : chex) c = toupper(static_cast<unsigned char>(c));
        fil.color_hex = chex;
        fil.colors.push_back(chex);
    }

    for (int i = 0; i < 5; ++i) {
        std::string sc_name = "secondary_color_" + std::to_string(i);
        auto it_sc = main_dict.find(sc_name);
        if (it_sc != main_dict.end() && it_sc->second.size() >= 7) {
            std::string chex = it_sc->second.substr(1, 6);
            for (char &c : chex) c = toupper(static_cast<unsigned char>(c));
            fil.colors.push_back(chex);
        }
    }

    auto it_diam = main_dict.find("filament_diameter");
    if (it_diam != main_dict.end()) fil.diameter_mm = std::strtod(it_diam->second.c_str(), nullptr);

    auto it_dens = main_dict.find("density");
    if (it_dens != main_dict.end()) fil.density = std::strtod(it_dens->second.c_str(), nullptr);

    auto it_weight = main_dict.find("actual_netto_full_weight");
    if (it_weight == main_dict.end()) it_weight = main_dict.find("nominal_netto_full_weight");
    if (it_weight != main_dict.end()) fil.weight_g = static_cast<int>(std::strtod(it_weight->second.c_str(), nullptr));

    if (fil.weight_g > 0) {
        auto it_consumed = aux_dict.find("consumed_weight");
        if (it_consumed != aux_dict.end()) {
            double cons = std::strtod(it_consumed->second.c_str(), nullptr);
            double rem = fil.weight_g - cons;
            char buf[32];
            snprintf(buf, sizeof(buf), "%g", rem);
            fil.fields["remaining_g"] = buf;
        }
    }

    auto it_spool = main_dict.find("empty_container_weight");
    if (it_spool != main_dict.end()) fil.spool_weight_g = static_cast<int>(std::strtod(it_spool->second.c_str(), nullptr));

    auto it_len = main_dict.find("actual_full_length");
    if (it_len == main_dict.end()) it_len = main_dict.find("nominal_full_length");
    if (it_len != main_dict.end()) fil.length_m = static_cast<int>(std::strtod(it_len->second.c_str(), nullptr) / 1000.0);

    auto it_nmin = main_dict.find("min_print_temperature");
    if (it_nmin != main_dict.end()) fil.nozzle_min = std::atoi(it_nmin->second.c_str());

    auto it_nmax = main_dict.find("max_print_temperature");
    if (it_nmax != main_dict.end()) fil.nozzle_max = std::atoi(it_nmax->second.c_str());

    auto it_bmin = main_dict.find("min_bed_temperature");
    if (it_bmin != main_dict.end()) fil.bed_min = std::atoi(it_bmin->second.c_str());

    auto it_bmax = main_dict.find("max_bed_temperature");
    if (it_bmax != main_dict.end()) fil.bed_max = std::atoi(it_bmax->second.c_str());

    auto it_ch = main_dict.find("chamber_temperature");
    if (it_ch != main_dict.end()) fil.fields["chamber_temp"] = it_ch->second;

    auto it_dry = main_dict.find("drying_temperature");
    if (it_dry != main_dict.end()) fil.drying_temp = std::atoi(it_dry->second.c_str());

    auto it_dryt = main_dict.find("drying_time");
    if (it_dryt != main_dict.end()) fil.drying_time_min = std::atoi(it_dryt->second.c_str());

    auto it_gtin = main_dict.find("gtin");
    if (it_gtin != main_dict.end()) fil.sku = it_gtin->second;

    auto it_mfg = main_dict.find("manufactured_date");
    if (it_mfg != main_dict.end()) {
        time_t t = static_cast<time_t>(std::strtoll(it_mfg->second.c_str(), nullptr, 10));
        struct tm tm_buf;
        if (gmtime_r(&t, &tm_buf)) {
            char buf[32];
            strftime(buf, sizeof(buf), "%Y-%m-%d", &tm_buf);
            fil.fields["manufactured"] = buf;
        }
    }

    // Populate out.info
    for (const auto &kv : main_dict) {
        out.info["main." + kv.first] = kv.second;
    }
    for (const auto &kv : aux_dict) {
        out.info["aux." + kv.first] = kv.second;
    }
    if (!uri_str.empty()) out.info["uri"] = uri_str;

    // Derive UUIDs
    std::string brand_uuid;
    auto it_bu = main_dict.find("brand_uuid");
    if (it_bu != main_dict.end()) {
        brand_uuid = it_bu->second;
    } else if (!fil.brand.empty()) {
        brand_uuid = uuid5(NS_BRAND, std::vector<uint8_t>(fil.brand.begin(), fil.brand.end()));
    }
    if (!brand_uuid.empty()) out.info["brand_uuid"] = brand_uuid;

    std::string material_uuid;
    auto it_mu = main_dict.find("material_uuid");
    if (it_mu != main_dict.end()) {
        material_uuid = it_mu->second;
    } else if (!brand_uuid.empty() && !fil.name.empty()) {
        uint8_t bu_bytes[16];
        if (parse_uuid(brand_uuid, bu_bytes)) {
            std::vector<uint8_t> name_bytes(bu_bytes, bu_bytes + 16);
            name_bytes.insert(name_bytes.end(), fil.name.begin(), fil.name.end());
            material_uuid = uuid5(NS_MATERIAL, name_bytes);
        }
    }
    if (!material_uuid.empty()) out.info["material_uuid"] = material_uuid;

    std::string instance_uuid;
    auto it_iu = main_dict.find("instance_uuid");
    if (it_iu != main_dict.end()) {
        instance_uuid = it_iu->second;
    } else if (!tag.uid.empty()) {
        instance_uuid = uuid5(NS_INSTANCE, tag.uid);
    }
    if (!instance_uuid.empty()) out.info["instance_uuid"] = instance_uuid;

    return true;
}

bool OpenPrintTagCodec::encode(Tag &tag, const Filament &fields, std::string &err) {
    if (!can_encode(tag)) {
        err = "tag cannot be encoded as openprinttag";
        return false;
    }

    cbor::CborValue main_val;
    main_val.type = cbor::Type::Map;
    main_val.set(8, cbor::CborValue::from_int(0)); // material_class: FFF

    if (!fields.material.empty()) {
        std::string mat_upper = fields.material;
        for (char &c : mat_upper) c = toupper(static_cast<unsigned char>(c));
        bool matched = false;
        for (const openprinttag_data::EnumName &type : openprinttag_data::MATERIAL_TYPE) {
            std::string abbrev_upper = type.name;
            for (char &c : abbrev_upper) c = toupper(static_cast<unsigned char>(c));
            if (abbrev_upper == mat_upper) {
                main_val.set(9, cbor::CborValue::from_int(type.key)); // material_type enum
                matched = true;
                break;
            }
        }
        if (!matched && fields.name.empty()) {
            main_val.set(10, cbor::CborValue(fields.material)); // material_name
        }
    }

    if (!fields.name.empty()) {
        main_val.set(10, cbor::CborValue(fields.name));
    }
    if (!fields.brand.empty()) {
        main_val.set(11, cbor::CborValue(fields.brand));
    }

    auto parse_hex_color = [](const std::string &hex_str) -> std::vector<uint8_t> {
        std::string s;
        for (char c : hex_str) {
            if (c != '#') s += c;
        }
        std::vector<uint8_t> bytes;
        for (size_t i = 0; i + 1 < s.size(); i += 2) {
            char b[3] = {s[i], s[i + 1], '\0'};
            bytes.push_back(static_cast<uint8_t>(std::strtoul(b, nullptr, 16)));
        }
        return bytes;
    };

    if (!fields.color_hex.empty()) {
        std::vector<uint8_t> c_bytes = parse_hex_color(fields.color_hex);
        if (!c_bytes.empty()) {
            main_val.set(19, cbor::CborValue::from_bytes(c_bytes));
        }
    }

    if (!fields.colors.empty()) {
        if (!main_val.get(19)) {
            std::vector<uint8_t> c_bytes = parse_hex_color(fields.colors[0]);
            if (!c_bytes.empty()) {
                main_val.set(19, cbor::CborValue::from_bytes(c_bytes));
            }
        }
        for (size_t i = 1; i < fields.colors.size() && i <= 5; ++i) {
            std::vector<uint8_t> c_bytes = parse_hex_color(fields.colors[i]);
            if (!c_bytes.empty()) {
                main_val.set(static_cast<int64_t>(19 + i), cbor::CborValue::from_bytes(c_bytes));
            }
        }
    }

    if (fields.diameter_mm > 0.0) {
        main_val.set(30, compact_float(fields.diameter_mm));
    }
    if (fields.density > 0.0) {
        main_val.set(29, compact_float(fields.density));
    }
    if (fields.weight_g > 0) {
        main_val.set(16, compact_float(fields.weight_g));
    }
    if (fields.spool_weight_g > 0) {
        main_val.set(18, compact_float(fields.spool_weight_g));
    }
    if (fields.length_m > 0) {
        main_val.set(53, compact_float(fields.length_m * 1000));
    }
    if (fields.nozzle_min > 0) {
        main_val.set(34, cbor::CborValue::from_int(fields.nozzle_min));
    }
    if (fields.nozzle_max > 0) {
        main_val.set(35, cbor::CborValue::from_int(fields.nozzle_max));
    }
    if (fields.bed_min > 0) {
        main_val.set(37, cbor::CborValue::from_int(fields.bed_min));
    }
    if (fields.bed_max > 0) {
        main_val.set(38, cbor::CborValue::from_int(fields.bed_max));
    }
    auto it_ch = fields.fields.find("chamber_temp");
    if (it_ch != fields.fields.end() && !it_ch->second.empty()) {
        main_val.set(41, cbor::CborValue::from_int(std::atoi(it_ch->second.c_str())));
    }
    if (fields.drying_temp > 0) {
        main_val.set(57, cbor::CborValue::from_int(fields.drying_temp));
    }
    if (fields.drying_time_min > 0) {
        main_val.set(58, cbor::CborValue::from_int(fields.drying_time_min));
    }
    if (!fields.sku.empty()) {
        std::string digits;
        for (char c : fields.sku) {
            if (std::isdigit(static_cast<unsigned char>(c))) digits += c;
        }
        if (!digits.empty()) {
            main_val.set(4, cbor::CborValue::from_int(std::strtoll(digits.c_str(), nullptr, 10)));
        }
    }
    auto it_mfg = fields.fields.find("manufactured");
    if (it_mfg != fields.fields.end() && !it_mfg->second.empty()) {
        int y = 0, m = 0, d = 0;
        if (std::sscanf(it_mfg->second.c_str(), "%d-%d-%d", &y, &m, &d) == 3) {
            struct tm tm_val = {};
            tm_val.tm_year = y - 1900;
            tm_val.tm_mon = m - 1;
            tm_val.tm_mday = d;
            time_t ts = timegm(&tm_val);
            if (ts > 0) {
                main_val.set(14, cbor::CborValue::from_int(static_cast<int64_t>(ts)));
            }
        }
    }

    cbor::CborValue aux_val;
    aux_val.type = cbor::Type::Map;
    auto it_rem = fields.fields.find("remaining_g");
    if (it_rem != fields.fields.end() && !it_rem->second.empty() && fields.weight_g > 0) {
        double rem = std::strtod(it_rem->second.c_str(), nullptr);
        double consumed = fields.weight_g - rem;
        aux_val.set(int64_t(0), compact_float(consumed));
    }
    std::string wg = options_.get("openprinttag_workgroup");
    if (!wg.empty()) {
        aux_val.set(int64_t(1), cbor::CborValue(wg));
    }

    size_t user_bytes = (tag.user_last_block + 1) * tag.block_size;
    size_t size = std::min((user_bytes / 8) * 8, size_t(2040));
    std::vector<uint8_t> image;
    bool success = false;
    for (int attempt = 0; attempt < 3; ++attempt) {
        if (size < static_cast<size_t>(8 * attempt)) break;
        if (build_image(main_val, &aux_val, size - 8 * attempt, tag.block_size, image, err)) {
            success = true;
            break;
        }
    }
    if (!success) return false;

    if (image.size() > tag.memory.size()) {
        err = "image exceeds tag memory";
        return false;
    }
    std::memcpy(tag.memory.data(), image.data(), image.size());
    size_t last_blk = (image.size() + tag.block_size - 1) / tag.block_size;
    for (size_t b = 0; b < last_blk && b < tag.readable.size(); ++b) {
        tag.readable[b] = true;
    }
    return true;
}

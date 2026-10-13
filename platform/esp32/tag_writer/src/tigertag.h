// Copyright (C) 2026 JR Lomas <lomas.jr@gmail.com>
// Based on the TigerTag reference database (CC0) and SDK (Apache-2.0).
// This file may be distributed under the terms of the GNU GPLv3 license.

// TigerTag format codec and registry tables.
#pragma once

#if __has_include("formatcodec.h")
#include "formatcodec.h"
#else
#include "../lib/formatcodec.h"
#endif

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace tigertag {

constexpr uint32_t ID_TIGERTAG      = 0x5BF59264; // 1542820452
constexpr uint32_t ID_TIGERTAG_PLUS = 0xBC0FCB97; // 3155151767
constexpr uint32_t ID_INIT          = 0x6C41A2E1; // 1816240865

struct VersionInfo {
    uint32_t id;
    const char *name;
};

struct DiameterInfo {
    uint8_t id;
    double diameter_mm;
};

struct TypeInfo {
    uint8_t id;
    const char *name;
};

struct UnitInfo {
    uint8_t id;
    const char *name;
    const char *category;
};

struct AspectInfo {
    uint8_t id;
    const char *label;
    int color_count;
};

struct BrandInfo {
    uint16_t id;
    const char *name;
};

struct MaterialInfo {
    uint16_t id;
    const char *label;
    const char *material_type;
    int nozzle_temp_min;
    int nozzle_temp_max;
    int bed_temp_min;
    int bed_temp_max;
    int drying_temp;
    int drying_time_h;
    double density;
};

extern const VersionInfo VERSIONS[4];
extern const size_t NUM_VERSIONS;

extern const DiameterInfo DIAMETERS[2];
extern const size_t NUM_DIAMETERS;

extern const TypeInfo TYPES[4];
extern const size_t NUM_TYPES;

extern const UnitInfo UNITS[11];
extern const size_t NUM_UNITS;

extern const AspectInfo ASPECTS[25];
extern const size_t NUM_ASPECTS;

extern const BrandInfo BRANDS[136];
extern const size_t NUM_BRANDS;

extern const MaterialInfo MATERIALS[113];
extern const size_t NUM_MATERIALS;

// Registry lookups
const char *lookup_version(uint32_t id);
double lookup_diameter(uint8_t id);
const char *lookup_type(uint8_t id);
const UnitInfo *lookup_unit(uint8_t id);
const AspectInfo *lookup_aspect(uint8_t id);
const char *lookup_brand(uint16_t id);
const MaterialInfo *lookup_material(uint16_t id);

// Convenience aliases
inline const char *get_brand(uint16_t id) { return lookup_brand(id); }
inline const MaterialInfo *get_material(uint16_t id) { return lookup_material(id); }
inline const char *get_version(uint32_t id) { return lookup_version(id); }

// Date conversions
std::string timestamp_to_date(uint32_t ts);
uint32_t date_to_timestamp(const std::string &d_str);

// String similarity matching
std::vector<std::string> get_close_matches(const std::string &word, size_t n = 3, double cutoff = 0.6);

} // namespace tigertag

namespace tigertag_data {
    using namespace tigertag;
}

class TigerTagCodec : public FormatCodec {
public:
    TigerTagCodec() = default;
    explicit TigerTagCodec(const CodecOptions &options) { (void)options; }
    virtual ~TigerTagCodec() override = default;

    const char *name() const override { return "tigertag"; }
    std::vector<std::string> tag_types() const override;
    bool decode(const Tag &tag, DecodeResult &out) override;
    bool can_encode(const Tag &tag) override;
    bool encode(Tag &tag, const Filament &fields, std::string &err) override;

    // Registry static lookups
    static const char *get_brand(uint16_t id) { return tigertag::lookup_brand(id); }
    static const tigertag::MaterialInfo *get_material(uint16_t id) { return tigertag::lookup_material(id); }
    static const char *get_version(uint32_t id) { return tigertag::lookup_version(id); }
};

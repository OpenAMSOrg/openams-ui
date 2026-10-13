// Copyright (C) 2026 JR Lomas <lomas.jr@gmail.com>
// Based on the TigerTag reference database (CC0) and SDK (Apache-2.0).
// This file may be distributed under the terms of the GNU GPLv3 license.

// TigerTag format codec implementation.
#include "tigertag.h"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <strings.h>

namespace tigertag {

const VersionInfo VERSIONS[4] = {
    {0U, "RFID Empty"},
    {1542820452U, "TigerTag"},
    {1816240865U, "TigerTag Init"},
    {3155151767U, "TigerTag+"},
};
const size_t NUM_VERSIONS = 4;

const DiameterInfo DIAMETERS[2] = {
    {56, 1.75},
    {221, 2.85},
};
const size_t NUM_DIAMETERS = 2;

const TypeInfo TYPES[4] = {
    {41, "Spare Part"},
    {116, "Accessories"},
    {142, "Filament"},
    {173, "Resin"},
};
const size_t NUM_TYPES = 4;

const UnitInfo UNITS[11] = {
    {10, "mg", "weight"},
    {21, "g", "weight"},
    {35, "kg", "weight"},
    {48, "ml", "volume"},
    {62, "cl", "volume"},
    {79, "L", "volume"},
    {95, "m³", "volume"},
    {112, "mm", "size"},
    {130, "cm", "size"},
    {149, "m", "size"},
    {170, "m²", "area"},
};
const size_t NUM_UNITS = 11;

const AspectInfo ASPECTS[25] = {
    {0, "-", 0},
    {21, "Clear", 1},
    {24, "Tricolor", 3},
    {64, "Glitter", 1},
    {67, "Translucent", 1},
    {91, "Glow in the Dark", 1},
    {92, "Silk", 1},
    {97, "Lithophane", 1},
    {104, "Basic", 1},
    {123, "Wood", 1},
    {126, "Pearl", 1},
    {129, "Gloss", 1},
    {132, "Fiber", 1},
    {134, "Satin", 1},
    {145, "Rainbow", 3},
    {168, "Thermoreactif", 1},
    {173, "Stone", 1},
    {216, "Neon", 1},
    {220, "Pastel", 1},
    {226, "Metal", 1},
    {232, "Marble", 1},
    {238, "Carbon", 1},
    {247, "Matt", 1},
    {252, "Bicolor", 2},
    {255, "None", 0},
};
const size_t NUM_ASPECTS = 25;

const BrandInfo BRANDS[136] = {
    {1, "Atome3D"},
    {1068, "SainSmart"},
    {1120, "Proto-Pasta"},
    {1421, "3DJake"},
    {2049, "Inslogic"},
    {2517, "Smart Materials 3D"},
    {2833, "Xstrand"},
    {3132, "Hatchbox"},
    {3575, "Conjure"},
    {3924, "FiloAlfa"},
    {4011, "QIDI Tech"},
    {4048, "Owa"},
    {4344, "MatterHackers"},
    {4356, "Landu"},
    {4565, "Valment"},
    {4700, "Filforme"},
    {5048, "Marswork"},
    {6305, "Markforged"},
    {7432, "Aliz"},
    {7674, "Extrudr"},
    {7812, "Jayo"},
    {7980, "Fillamentum"},
    {8182, "Fiberlogy"},
    {8303, "GST3D"},
    {8384, "Taulman3D"},
    {8496, "CaiLab"},
    {8503, "E3D"},
    {8586, "NinjaTek"},
    {8675, "SOVB 3D"},
    {8756, "BlueCast"},
    {8921, "Duramic 3D"},
    {8990, "Ice Filaments"},
    {9192, "3D Solutech"},
    {9299, "FDPlast"},
    {9394, "Gizmo Dorks"},
    {9596, "Ziro"},
    {9798, "AMOLEN"},
    {11379, "Filaments.ca"},
    {11429, "3D4Makers"},
    {11501, "InnovateFil"},
    {11750, "VOXELPLA"},
    {12345, "MakerBot"},
    {12498, "Forshape"},
    {12635, "Snapmaker"},
    {13667, "RS Pro"},
    {14250, "Jessie (Printed Solid)"},
    {14982, "3D-Fuel"},
    {15899, "Kimya"},
    {15962, "Anycubic"},
    {18629, "PrintoMax 3D"},
    {19265, "CC3D"},
    {19961, "Rosa3D"},
    {20523, "Raise3D"},
    {20851, "Tronxy"},
    {22652, "Spectrum"},
    {23181, "ArianePlast"},
    {23456, "Monoprice"},
    {24363, "Tecbears"},
    {26145, "3DMA"},
    {26379, "Formlabs"},
    {26595, "Sovol"},
    {26956, "Creality"},
    {28055, "TAGin3D"},
    {28136, "Polar Filament"},
    {28940, "Eryone"},
    {28988, "KINGROON"},
    {29045, "Yousu"},
    {29302, "IIIDMAX"},
    {31438, "Wondermaker"},
    {32348, "Addnorth"},
    {32587, "Amazon"},
    {33566, "Siraya Tech"},
    {33594, "DEEPLEE"},
    {33788, "Verbatim"},
    {34567, "Push Plastic"},
    {34597, "EconoFil"},
    {35123, "Bambu Lab"},
    {35501, "Zortrax"},
    {35857, "Nobufil"},
    {35882, "Phrozen"},
    {36702, "Tianse"},
    {37434, "Winkle"},
    {38533, "Lotactree"},
    {38953, "Tucab"},
    {39002, "GreenGate3D"},
    {39382, "Longer"},
    {39652, "3DXTech"},
    {41847, "OneFil"},
    {41932, "Jamg He"},
    {42911, "UltiMaker"},
    {44630, "NIT"},
    {45670, "Panchroma"},
    {45678, "Atomic Filament"},
    {46010, "AceAddity"},
    {46203, "Overture"},
    {46392, "Prusament"},
    {47122, "Zimple3D"},
    {47560, "Wanhao"},
    {47930, "eSun"},
    {48261, "Gsun3D"},
    {48804, "R3D"},
    {49784, "GIANTARM"},
    {50311, "G3D Pro"},
    {50604, "Polymaker"},
    {51139, "FusRock"},
    {51443, "BASF"},
    {51857, "Sunlu"},
    {51978, "Inland"},
    {52222, "ColorFabb"},
    {52467, "Geeetech"},
    {52757, "Yumi"},
    {53043, "FormFutura"},
    {53146, "EPAX"},
    {53640, "Magigoo"},
    {53856, "Lattice Medical"},
    {54112, "Kexcelled"},
    {55229, "Filament PM"},
    {55763, "Nanovia"},
    {55869, "Biqu"},
    {56780, "Fiberon"},
    {56789, "Coex 3D"},
    {57209, "FrancoFil"},
    {57632, "ELEGOO"},
    {58231, "IC3D"},
    {58410, "AzureFilm"},
    {58863, "iSANMATE"},
    {58972, "Tinmorry"},
    {59597, "Filamentive"},
    {60073, "3D Lac"},
    {60882, "Recreus"},
    {62436, "Ambrosia"},
    {63024, "Multicomp Pro"},
    {63340, "Flashforge"},
    {63757, "Copper3D"},
    {65178, "Smart3DPT"},
    {65535, "Generic"},
};
const size_t NUM_BRANDS = 136;

const MaterialInfo MATERIALS[113] = {
    {425, "ABS-CF", "ABS", 240, 280, 85, 90, 80, 8, 1.04},
    {735, "ABS-AF", "ABS", 240, 290, 85, 90, 85, 8, 1.04},
    {1173, "PA6-GF", "PA6", 260, 300, 100, 105, 85, 12, 1.52},
    {1680, "PE-GF", "PE", 220, 260, 60, 80, 80, 8, 1.12},
    {2053, "PA12-GF", "PA12", 260, 300, 70, 90, 85, 12, 1.52},
    {3368, "PC-ABS", "PC", 240, 290, 90, 95, 85, 8, 1.19},
    {3481, "PCTG-GF", "PCTG", 240, 270, 70, 75, 65, 8, 1.3},
    {4587, "PC-PBT", "PC", 260, 280, 90, 100, 90, 2, 1.2},
    {5238, "PETG-PTFE", "PETG", 230, 255, 70, 80, 55, 6, 1.27},
    {5733, "TPU-AMS", "TPU", 200, 250, 35, 40, 75, 8, 1.21},
    {6605, "PA11-GF", "PA", 270, 300, 80, 100, 80, 12, 1.2},
    {7649, "PETG HS", "PETG", 220, 270, 70, 75, 70, 8, 1.27},
    {7951, "PETG-rCF", "PETG", 240, 270, 65, 70, 70, 8, 1.27},
    {8345, "PLA+ Silk", "PLA", 190, 240, 45, 55, 50, 8, 1.24},
    {8394, "Castable Resin", "", 0, 0, 0, 0, 0, 0, 1.05},
    {8504, "PPA-CF", "PPA", 280, 320, 100, 105, 110, 12, 1.42},
    {9456, "PLA Marble", "PLA", 190, 260, 45, 55, 50, 8, 1.24},
    {9483, "PVA", "PVA", 190, 240, 30, 60, 85, 12, 1.23},
    {9691, "EVA", "EVA", 175, 220, 55, 60, 50, 4, 0.94},
    {10122, "PEKK-CF", "PEKK", 350, 400, 120, 140, 120, 6, 1.31},
    {10187, "PHA", "PHA", 190, 240, 55, 60, 50, 24, 1.25},
    {10272, "PSU", "PSU", 360, 400, 140, 160, 110, 4, 1.24},
    {10478, "Castable Filament", "", 190, 220, 30, 70, 55, 12, 1.1},
    {10602, "PLA Silk", "PLA", 190, 240, 45, 55, 50, 8, 1.24},
    {10738, "PC-PTFE", "PC", 270, 290, 110, 130, 80, 4, 1.3},
    {11053, "PET-CF", "PET", 260, 290, 90, 95, 85, 12, 1.3},
    {11506, "PLA-AERO", "PLA", 210, 260, 45, 55, 50, 8, 1.24},
    {12216, "PEEK-GF", "PEEK", 380, 420, 130, 160, 120, 6, 1.49},
    {12264, "PA6-CF", "PA6", 260, 300, 100, 105, 85, 12, 1.52},
    {12844, "ASA", "ASA", 240, 280, 95, 100, 80, 8, 1.05},
    {12878, "CoPE", "", 190, 240, 30, 45, 70, 6, 1.2},
    {13850, "PPA", "PPA", 280, 320, 100, 105, 110, 12, 1.15},
    {14508, "PEI-9085", "PEI", 350, 390, 120, 160, 150, 12, 1.34},
    {15041, "PCTG", "PCTG", 240, 270, 70, 75, 65, 8, 1.21},
    {18130, "PS", "PS", 220, 260, 60, 100, 75, 2, 1.05},
    {18451, "PA11-CF", "PA11", 270, 300, 80, 100, 80, 12, 1.09},
    {18703, "PETP", "PET", 220, 260, 90, 95, 70, 8, 1.38},
    {18775, "PE-CF", "PE", 175, 220, 55, 60, 80, 12, 1.1},
    {18922, "PLA-ESD", "PLA", 190, 240, 45, 55, 50, 8, 1.24},
    {20073, "PVC", "PVC", 215, 230, 80, 85, 80, 1, 1.38},
    {20562, "ABS", "ABS", 240, 280, 85, 90, 80, 8, 1.04},
    {20588, "PC-CF", "PC", 270, 310, 100, 120, 90, 8, 1.22},
    {21307, "SBS", "SBS", 195, 250, 0, 55, 50, 4, 1.05},
    {22652, "PAHT-GF", "PA", 270, 310, 90, 110, 90, 12, 1.25},
    {22678, "PET-GF", "PET", 280, 300, 80, 85, 50, 8, 1.4},
    {23080, "PAHT", "PA", 260, 300, 90, 110, 90, 12, 1.05},
    {24115, "SEBS", "SEBS", 245, 260, 70, 80, 65, 3, 0.9},
    {24116, "TPC", "TPC", 220, 245, 60, 90, 75, 8, 1.15},
    {24270, "PPS-CF", "PPS", 310, 340, 110, 115, 130, 12, 1.38},
    {24629, "PLA High Speed", "PLA", 190, 240, 45, 55, 50, 8, 1.24},
    {26029, "HIPS", "HIPS", 220, 270, 90, 95, 80, 6, 1.03},
    {27268, "PCTPE", "PCPTFE", 235, 245, 50, 55, 90, 3, 1.01},
    {27635, "PE", "PE", 175, 220, 55, 60, 0, 0, 0.94},
    {27676, "ASA-CF", "ASA", 250, 280, 90, 95, 80, 8, 1.05},
    {28110, "SBC", "SBC", 220, 260, 65, 70, 45, 6, 1.01},
    {29272, "PA11", "PA11", 260, 290, 80, 100, 80, 12, 1.03},
    {29815, "PEEK", "PEEK", 390, 410, 130, 145, 120, 6, 1.32},
    {30458, "PC", "PC", 260, 290, 110, 115, 85, 8, 1.3},
    {30594, "PA-GF", "PA", 260, 300, 100, 105, 85, 12, 1.52},
    {30884, "PP", "PP", 220, 250, 55, 60, 60, 4, 0.9},
    {31011, "ASA-AERO", "ASA", 240, 280, 95, 100, 80, 8, 1.05},
    {33958, "TPE", "TPE", 210, 260, 45, 60, 65, 8, 1},
    {34049, "BVOH", "BVOH", 190, 240, 55, 60, 85, 12, 1.14},
    {34409, "TPS", "TPS", 280, 290, 70, 90, 80, 24, 1},
    {34944, "PETG-GF", "PETG", 230, 270, 70, 90, 70, 8, 1.27},
    {35100, "ASA-GF", "ASA", 250, 280, 90, 95, 80, 8, 1.05},
    {35377, "PEI-9085-GF", "PEI", 360, 400, 120, 160, 150, 12, 1.45},
    {38219, "PLA", "PLA", 190, 240, 45, 55, 50, 8, 1.24},
    {38250, "PEI-1010-GF", "PEI", 380, 430, 120, 160, 150, 12, 1.42},
    {38256, "PETG", "PETG", 220, 270, 70, 75, 70, 8, 1.27},
    {39667, "PA12-CF", "PA12", 260, 300, 70, 90, 85, 12, 1.52},
    {39944, "PA-CF", "PA", 260, 300, 100, 105, 85, 12, 1.52},
    {41134, "PEI-9085-CF", "PEI", 360, 400, 120, 160, 150, 12, 1.36},
    {42623, "PMMA", "PMMA", 240, 265, 100, 110, 80, 4, 1.18},
    {42962, "PP-GF", "PP", 220, 250, 55, 60, 60, 4, 0.9},
    {43518, "TPU", "TPU", 200, 250, 35, 40, 75, 8, 1.21},
    {45962, "PVB", "PVB", 205, 225, 30, 70, 45, 8, 1.07},
    {46154, "PPS", "PPS", 300, 340, 110, 115, 130, 12, 1.35},
    {46276, "PPA-GF", "PPA", 280, 320, 100, 105, 110, 12, 1.45},
    {46591, "PLA+", "PLA", 190, 240, 45, 55, 50, 8, 1.24},
    {47651, "PC-PBT-CF", "PC", 260, 280, 90, 100, 90, 2, 1.22},
    {48001, "PLA Wood", "PLA", 190, 240, 45, 55, 60, 8, 1.28},
    {48047, "TPU High Speed", "TPU", 200, 250, 35, 40, 75, 8, 1.21},
    {48310, "PLA-CF", "PLA", 190, 240, 45, 55, 60, 8, 1.24},
    {48469, "PEEK-CF", "PEEK", 380, 420, 130, 160, 120, 6, 1.4},
    {48815, "PAHT-CF", "PAHT", 260, 300, 100, 105, 85, 12, 1.52},
    {49074, "ABS-GF", "ABS", 240, 280, 85, 90, 80, 8, 1.04},
    {49152, "PPSU", "PPSU", 360, 400, 140, 170, 130, 12, 1.37},
    {49804, "ASA-AF", "ASA", 250, 280, 90, 95, 80, 8, 1.05},
    {50206, "POM", "POM", 230, 250, 90, 110, 75, 3, 1.4},
    {50497, "PP-CF", "PP", 220, 250, 55, 60, 60, 4, 0.9},
    {51007, "Biopolymer", "", 190, 240, 45, 50, 50, 8, 0},
    {51636, "PES", "PES", 340, 380, 120, 140, 120, 6, 1.37},
    {51861, "PETG-ESD", "PETG", 240, 270, 65, 70, 70, 8, 1.27},
    {52077, "PET", "PET", 260, 290, 65, 70, 85, 12, 1.27},
    {53890, "PCTG-CF", "PCTG", 240, 270, 70, 75, 65, 8, 1.25},
    {53970, "PEKK", "PEKK", 380, 440, 120, 140, 130, 4, 1.28},
    {54568, "ASA+", "ASA", 250, 280, 90, 110, 65, 8, 1.05},
    {55279, "PBT", "PBT", 235, 245, 0, 0, 75, 6, 1.2},
    {55418, "PETG-CF", "PETG", 240, 270, 75, 80, 70, 8, 1.27},
    {55796, "PA12", "PA12", 250, 290, 70, 90, 85, 12, 1.01},
    {56527, "PEI-1010", "PEI-1010", 370, 420, 150, 160, 130, 12, 1.27},
    {56666, "PA6", "PA6", 240, 280, 100, 105, 85, 12, 1.14},
    {57469, "PETG HF", "PETG", 220, 270, 70, 75, 70, 8, 1.27},
    {58142, "TPU-GF", "TPU", 200, 250, 35, 40, 75, 8, 1.3},
    {58498, "PEBA", "PEBA", 225, 250, 95, 100, 70, 3, 1.01},
    {59328, "PA", "PA", 240, 280, 100, 105, 85, 12, 1.14},
    {59849, "PCL", "PCL", 80, 140, 25, 45, 0, 0, 1.15},
    {61048, "PVDF", "PVDF", 240, 265, 90, 110, 0, 0, 1.78},
    {61563, "PC-PBT-GF", "PC", 260, 280, 90, 100, 90, 2, 1.35},
    {62335, "PEI-1010-CF", "PEI-1010", 370, 430, 150, 160, 150, 8, 0},
    {63946, "TPI", "TPI", 360, 390, 0, 0, 120, 24, 1.38},
    {65535, "None", "None", 0, 0, 0, 0, 0, 0, 0.0},
};
const size_t NUM_MATERIALS = 113;


const char *lookup_version(uint32_t id) {
    auto it = std::lower_bound(VERSIONS, VERSIONS + NUM_VERSIONS, id,
        [](const VersionInfo &a, uint32_t val) { return a.id < val; });
    if (it != VERSIONS + NUM_VERSIONS && it->id == id) {
        return it->name;
    }
    return nullptr;
}

double lookup_diameter(uint8_t id) {
    auto it = std::lower_bound(DIAMETERS, DIAMETERS + NUM_DIAMETERS, id,
        [](const DiameterInfo &a, uint8_t val) { return a.id < val; });
    if (it != DIAMETERS + NUM_DIAMETERS && it->id == id) {
        return it->diameter_mm;
    }
    return 0.0;
}

const char *lookup_type(uint8_t id) {
    auto it = std::lower_bound(TYPES, TYPES + NUM_TYPES, id,
        [](const TypeInfo &a, uint8_t val) { return a.id < val; });
    if (it != TYPES + NUM_TYPES && it->id == id) {
        return it->name;
    }
    return nullptr;
}

const UnitInfo *lookup_unit(uint8_t id) {
    auto it = std::lower_bound(UNITS, UNITS + NUM_UNITS, id,
        [](const UnitInfo &a, uint8_t val) { return a.id < val; });
    if (it != UNITS + NUM_UNITS && it->id == id) {
        return it;
    }
    return nullptr;
}

const AspectInfo *lookup_aspect(uint8_t id) {
    auto it = std::lower_bound(ASPECTS, ASPECTS + NUM_ASPECTS, id,
        [](const AspectInfo &a, uint8_t val) { return a.id < val; });
    if (it != ASPECTS + NUM_ASPECTS && it->id == id) {
        return it;
    }
    return nullptr;
}

const char *lookup_brand(uint16_t id) {
    auto it = std::lower_bound(BRANDS, BRANDS + NUM_BRANDS, id,
        [](const BrandInfo &a, uint16_t val) { return a.id < val; });
    if (it != BRANDS + NUM_BRANDS && it->id == id) {
        return it->name;
    }
    return nullptr;
}

const MaterialInfo *lookup_material(uint16_t id) {
    auto it = std::lower_bound(MATERIALS, MATERIALS + NUM_MATERIALS, id,
        [](const MaterialInfo &a, uint16_t val) { return a.id < val; });
    if (it != MATERIALS + NUM_MATERIALS && it->id == id) {
        return it;
    }
    return nullptr;
}

std::string timestamp_to_date(uint32_t ts) {
    if (ts == 0) {
        return "";
    }
    time_t unix_ts = static_cast<time_t>(946684800ULL + ts);
    struct tm tm_buf;
    if (gmtime_r(&unix_ts, &tm_buf) == nullptr) {
        return "";
    }
    char buf[32];
    snprintf(buf, sizeof(buf), "%04d-%02d-%02d",
             tm_buf.tm_year + 1900, tm_buf.tm_mon + 1, tm_buf.tm_mday);
    return std::string(buf);
}

uint32_t date_to_timestamp(const std::string &d_str) {
    if (d_str.empty()) {
        return 0;
    }
    int year = 0, month = 0, day = 0;
    if (sscanf(d_str.c_str(), "%d-%d-%d", &year, &month, &day) < 3) {
        return 0;
    }
    struct tm tm_buf;
    memset(&tm_buf, 0, sizeof(tm_buf));
    tm_buf.tm_year = year - 1900;
    tm_buf.tm_mon = month - 1;
    tm_buf.tm_mday = day;
    time_t unix_ts = timegm(&tm_buf);
    if (unix_ts == -1 || unix_ts < 946684800) {
        return 0;
    }
    return static_cast<uint32_t>(unix_ts - 946684800);
}

static int find_matching_blocks(const std::string &a, size_t a_lo, size_t a_hi,
                                const std::string &b, size_t b_lo, size_t b_hi) {
    if (a_lo >= a_hi || b_lo >= b_hi) return 0;
    size_t best_i = a_lo, best_j = b_lo, best_k = 0;
    for (size_t i = a_lo; i < a_hi; ++i) {
        for (size_t j = b_lo; j < b_hi; ++j) {
            size_t k = 0;
            while (i + k < a_hi && j + k < b_hi && a[i + k] == b[j + k]) {
                ++k;
            }
            if (k > best_k) {
                best_i = i;
                best_j = j;
                best_k = k;
            }
        }
    }
    if (best_k == 0) return 0;
    int matches = static_cast<int>(best_k);
    matches += find_matching_blocks(a, a_lo, best_i, b, b_lo, best_j);
    matches += find_matching_blocks(a, best_i + best_k, a_hi, b, best_j + best_k, b_hi);
    return matches;
}

static double sequence_matcher_ratio(const std::string &a, const std::string &b) {
    if (a.empty() && b.empty()) return 1.0;
    if (a.empty() || b.empty()) return 0.0;
    int m = find_matching_blocks(a, 0, a.size(), b, 0, b.size());
    return (2.0 * m) / static_cast<double>(a.size() + b.size());
}

std::vector<std::string> get_close_matches(const std::string &word, size_t n, double cutoff) {
    std::vector<std::pair<double, std::string>> matches;
    for (size_t i = 0; i < NUM_BRANDS; ++i) {
        double r = sequence_matcher_ratio(word, BRANDS[i].name);
        if (r >= cutoff) {
            matches.push_back({r, BRANDS[i].name});
        }
    }
    std::stable_sort(matches.begin(), matches.end(),
        [](const auto &a, const auto &b) { return a.first > b.first; });
    std::vector<std::string> result;
    for (size_t i = 0; i < matches.size() && i < n; ++i) {
        result.push_back(matches[i].second);
    }
    return result;
}

static void parse_rgb(const std::string &c_hex, uint8_t &r, uint8_t &g, uint8_t &b) {
    r = 0; g = 0; b = 0;
    std::string s = c_hex;
    size_t start = 0;
    while (start < s.size() && (isspace(static_cast<unsigned char>(s[start])) || s[start] == '#')) {
        ++start;
    }
    size_t end = s.size();
    while (end > start && isspace(static_cast<unsigned char>(s[end - 1]))) {
        --end;
    }
    if (end - start < 6) {
        return;
    }
    std::string sub = s.substr(start, 6);
    unsigned int r_val = 0, g_val = 0, b_val = 0;
    if (sscanf(sub.c_str(), "%02x%02x%02x", &r_val, &g_val, &b_val) == 3) {
        r = static_cast<uint8_t>(r_val);
        g = static_cast<uint8_t>(g_val);
        b = static_cast<uint8_t>(b_val);
    }
}

} // namespace tigertag

std::vector<std::string> TigerTagCodec::tag_types() const {
    return {
        TagTypes::NTAG213,
        TagTypes::NTAG215,
        TagTypes::NTAG216,
        TagTypes::ULTRALIGHT,
        TagTypes::ULTRALIGHT_EV1,
        TagTypes::TYPE2
    };
}

bool TigerTagCodec::can_encode(const Tag &tag) {
    return is_type2_tag(tag.tag_type) && tag.user_last_block >= 39;
}

bool TigerTagCodec::decode(const Tag &tag, DecodeResult &out) {
    if (tag.memory.size() < 96) {
        return false;
    }
    const uint8_t *buf = &tag.memory[16];
    size_t buf_len = tag.memory.size() - 16;
    if (buf_len < 80) {
        return false;
    }

    uint32_t id_tag = (static_cast<uint32_t>(buf[0]) << 24) |
                      (static_cast<uint32_t>(buf[1]) << 16) |
                      (static_cast<uint32_t>(buf[2]) << 8)  |
                      static_cast<uint32_t>(buf[3]);

    const char *ver_str = tigertag::lookup_version(id_tag);
    if (id_tag == 0 || ver_str == nullptr) {
        return false;
    }

    if (id_tag == tigertag::ID_INIT) {
        out.recognized = true;
        out.format = "tigertag";
        out.filament = Filament();
        out.info.clear();
        out.info["state"] = "init";
        return true;
    }

    uint32_t id_product = (static_cast<uint32_t>(buf[4]) << 24) |
                          (static_cast<uint32_t>(buf[5]) << 16) |
                          (static_cast<uint32_t>(buf[6]) << 8)  |
                          static_cast<uint32_t>(buf[7]);
    uint16_t id_material = (static_cast<uint16_t>(buf[8]) << 8) | buf[9];
    uint8_t id_aspect_1 = buf[10];
    uint8_t id_aspect_2 = buf[11];
    uint8_t id_type = buf[12];
    uint8_t id_diameter = buf[13];
    uint16_t id_brand = (static_cast<uint16_t>(buf[14]) << 8) | buf[15];

    uint8_t r1 = buf[16], g1 = buf[17], b1 = buf[18];
    uint32_t measure = (static_cast<uint32_t>(buf[20]) << 16) |
                       (static_cast<uint32_t>(buf[21]) << 8)  |
                       buf[22];
    uint8_t id_unit = buf[23];

    uint16_t nozzle_min = (static_cast<uint16_t>(buf[24]) << 8) | buf[25];
    uint16_t nozzle_max = (static_cast<uint16_t>(buf[26]) << 8) | buf[27];

    uint8_t dry_temp = buf[28];
    uint8_t dry_time = buf[29];
    uint8_t bed_min = buf[30];
    uint8_t bed_max = buf[31];

    uint32_t timestamp = (static_cast<uint32_t>(buf[32]) << 24) |
                         (static_cast<uint32_t>(buf[33]) << 16) |
                         (static_cast<uint32_t>(buf[34]) << 8)  |
                         static_cast<uint32_t>(buf[35]);

    uint8_t r2 = buf[36], g2 = buf[37], b2 = buf[38];
    uint8_t tag_info = buf[39];
    uint8_t r3 = buf[40], g3 = buf[41], b3 = buf[42];

    uint16_t td_raw = (static_cast<uint16_t>(buf[44]) << 8) | buf[45];

    std::string msg;
    for (size_t i = 48; i < 76 && i < buf_len; ++i) {
        if (buf[i] == 0) break;
        msg += static_cast<char>(buf[i]);
    }
    size_t m_start = 0;
    while (m_start < msg.size() && isspace(static_cast<unsigned char>(msg[m_start]))) ++m_start;
    size_t m_end = msg.size();
    while (m_end > m_start && isspace(static_cast<unsigned char>(msg[m_end - 1]))) --m_end;
    msg = (m_start < m_end) ? msg.substr(m_start, m_end - m_start) : "";

    uint32_t measure_avail = (static_cast<uint32_t>(buf[76]) << 16) |
                             (static_cast<uint32_t>(buf[77]) << 8)  |
                             buf[78];

    bool is_signed = false;
    if (buf_len >= 144) {
        for (size_t i = 80; i < 144; ++i) {
            if (buf[i] != 0) {
                is_signed = true;
                break;
            }
        }
    }

    const char *brand_name = tigertag::lookup_brand(id_brand);
    const auto *mat_entry = tigertag::lookup_material(id_material);
    const auto *asp1 = tigertag::lookup_aspect(id_aspect_1);
    const auto *asp2 = tigertag::lookup_aspect(id_aspect_2);

    std::string brand = brand_name ? brand_name : "";
    std::string material = (mat_entry && mat_entry->material_type) ? mat_entry->material_type : "";

    const char *asp1_label = asp1 ? asp1->label : nullptr;
    int asp1_count = asp1 ? asp1->color_count : 0;
    const char *asp2_label = asp2 ? asp2->label : nullptr;
    int asp2_count = asp2 ? asp2->color_count : 0;

    std::vector<std::string> aspect_labels;
    std::vector<std::string> name_parts;
    if (mat_entry && mat_entry->label && strlen(mat_entry->label) > 0) {
        name_parts.push_back(mat_entry->label);
    }
    const char *asps[2] = {asp1_label, asp2_label};
    for (int i = 0; i < 2; ++i) {
        if (asps[i] && strcmp(asps[i], "-") != 0 && strcmp(asps[i], "None") != 0 && strlen(asps[i]) > 0) {
            name_parts.push_back(asps[i]);
            aspect_labels.push_back(asps[i]);
        }
    }

    std::string name;
    for (size_t i = 0; i < name_parts.size(); ++i) {
        if (i > 0) name += " ";
        name += name_parts[i];
    }

    int color_count = 1;
    if (asp2_count > 1) {
        color_count = asp2_count;
    } else if (asp1_count > 1) {
        color_count = asp1_count;
    }

    char hexbuf[16];
    snprintf(hexbuf, sizeof(hexbuf), "%02X%02X%02X", r1, g1, b1);
    std::string color_hex = hexbuf;
    std::vector<std::string> colors;
    colors.push_back(color_hex);
    if (color_count >= 2 && (r2 || g2 || b2)) {
        snprintf(hexbuf, sizeof(hexbuf), "%02X%02X%02X", r2, g2, b2);
        colors.push_back(hexbuf);
    }
    if (color_count >= 3 && (r3 || g3 || b3)) {
        snprintf(hexbuf, sizeof(hexbuf), "%02X%02X%02X", r3, g3, b3);
        colors.push_back(hexbuf);
    }

    double diameter_mm = tigertag::lookup_diameter(id_diameter);

    int weight_g = 0;
    int remaining_g = -1;
    const auto *unit_info = tigertag::lookup_unit(id_unit);
    if (unit_info && unit_info->name) {
        if (strcmp(unit_info->name, "g") == 0) {
            weight_g = static_cast<int>(measure);
            if (measure_avail != 0) {
                remaining_g = static_cast<int>(measure_avail);
            }
        } else if (strcmp(unit_info->name, "kg") == 0) {
            weight_g = static_cast<int>(measure * 1000);
            if (measure_avail != 0) {
                remaining_g = static_cast<int>(measure_avail * 1000);
            }
        }
    }

    int n_min = nozzle_min != 0 ? nozzle_min : 0;
    int n_max = nozzle_max != 0 ? nozzle_max : 0;
    int b_min = bed_min != 0 ? bed_min : 0;
    int b_max = bed_max != 0 ? bed_max : 0;
    int d_temp = dry_temp != 0 ? dry_temp : 0;
    int d_time = dry_time != 0 ? (dry_time * 60) : 0;

    double density = mat_entry ? mat_entry->density : 0.0;
    std::string manufactured = tigertag::timestamp_to_date(timestamp);

    Filament fil;
    fil.brand = brand;
    fil.material = material;
    fil.name = name;
    fil.color_hex = color_hex;
    fil.colors = colors;
    fil.diameter_mm = diameter_mm;
    fil.density = density;
    fil.weight_g = weight_g;
    fil.nozzle_min = n_min;
    fil.nozzle_max = n_max;
    fil.bed_min = b_min;
    fil.bed_max = b_max;
    fil.drying_temp = d_temp;
    fil.drying_time_min = d_time;
    if (!manufactured.empty()) {
        fil.fields["manufactured"] = manufactured;
    }
    if (remaining_g >= 0) {
        fil.fields["remaining_g"] = std::to_string(remaining_g);
    }

    out.recognized = true;
    out.format = "tigertag";
    out.filament = fil;
    out.info.clear();
    out.info["version"] = ver_str;
    out.info["product_id"] = std::to_string(id_product);
    out.info["material_id"] = std::to_string(id_material);
    out.info["brand_id"] = std::to_string(id_brand);
    out.info["type_id"] = std::to_string(id_type);
    out.info["diameter_id"] = std::to_string(id_diameter);
    out.info["unit_id"] = std::to_string(id_unit);
    out.info["aspect_1"] = std::to_string(id_aspect_1);
    out.info["aspect_2"] = std::to_string(id_aspect_2);
    out.info["ids.material_id"] = std::to_string(id_material);
    out.info["ids.brand_id"] = std::to_string(id_brand);
    out.info["ids.type_id"] = std::to_string(id_type);
    out.info["ids.diameter_id"] = std::to_string(id_diameter);
    out.info["ids.unit_id"] = std::to_string(id_unit);
    out.info["ids.aspect_1"] = std::to_string(id_aspect_1);
    out.info["ids.aspect_2"] = std::to_string(id_aspect_2);
    if (!aspect_labels.empty()) {
        std::string asp_str;
        for (size_t i = 0; i < aspect_labels.size(); ++i) {
            if (i > 0) asp_str += ", ";
            asp_str += aspect_labels[i];
        }
        out.info["aspect_labels"] = asp_str;
    }
    if (td_raw != 0) {
        char tdbuf[32];
        snprintf(tdbuf, sizeof(tdbuf), "%.1f", td_raw * 0.1);
        out.info["td"] = tdbuf;
    }
    if (!msg.empty()) {
        out.info["message"] = msg;
    }
    if ((tag_info >> 4) != 0) {
        out.info["tag_index"] = std::to_string(tag_info >> 4);
    }
    out.info["signed"] = is_signed ? "true" : "false";
    if (!manufactured.empty()) {
        out.info["manufactured"] = manufactured;
    }
    if (remaining_g >= 0) {
        out.info["remaining_g"] = std::to_string(remaining_g);
    }

    return true;
}

bool TigerTagCodec::encode(Tag &tag, const Filament &fields, std::string &err) {
    if (!can_encode(tag)) {
        err = "tag cannot fit TigerTag layout";
        return false;
    }

    std::string material = !fields.material.empty() ? fields.material :
        (fields.fields.count("material") ? fields.fields.at("material") : "");
    std::string name = !fields.name.empty() ? fields.name :
        (fields.fields.count("name") ? fields.fields.at("name") : "");

    const tigertag::MaterialInfo *mat_info = nullptr;
    for (size_t i = 0; i < tigertag::NUM_MATERIALS; ++i) {
        const auto &m = tigertag::MATERIALS[i];
        if ((!name.empty() && strcasecmp(m.label, name.c_str()) == 0) ||
            (!material.empty() && strcasecmp(m.label, material.c_str()) == 0)) {
            mat_info = &m;
            break;
        }
    }
    if (!mat_info && !material.empty()) {
        for (size_t i = 0; i < tigertag::NUM_MATERIALS; ++i) {
            const auto &m = tigertag::MATERIALS[i];
            if (strcasecmp(m.material_type, material.c_str()) == 0) {
                mat_info = &m;
                break;
            }
        }
    }
    if (!mat_info) {
        std::string missing = !name.empty() ? name : material;
        err = "material '" + missing + "' not in TigerTag database";
        return false;
    }

    std::string brand = !fields.brand.empty() ? fields.brand :
        (fields.fields.count("brand") ? fields.fields.at("brand") : "Generic");
    if (brand.empty()) {
        brand = "Generic";
    }

    const tigertag::BrandInfo *brand_info = nullptr;
    for (size_t i = 0; i < tigertag::NUM_BRANDS; ++i) {
        if (strcasecmp(tigertag::BRANDS[i].name, brand.c_str()) == 0) {
            brand_info = &tigertag::BRANDS[i];
            break;
        }
    }
    if (!brand_info) {
        auto near = tigertag::get_close_matches(brand, 3, 0.6);
        std::string near_str;
        for (size_t i = 0; i < near.size(); ++i) {
            if (i > 0) near_str += ", ";
            near_str += near[i];
        }
        err = "unknown brand '" + brand + "', near matches: " + near_str;
        return false;
    }

    double diam = fields.diameter_mm != 0.0 ? fields.diameter_mm : 1.75;
    uint8_t diam_id = 0;
    if (std::abs(diam - 1.75) < 0.1) {
        diam_id = 56;
    } else if (std::abs(diam - 2.85) < 0.1) {
        diam_id = 221;
    } else {
        char buf[64];
        snprintf(buf, sizeof(buf), "unsupported diameter %g", diam);
        err = buf;
        return false;
    }

    std::string raw_c = fields.color_hex;
    if (raw_c.empty() && fields.fields.count("color_hex")) {
        raw_c = fields.fields.at("color_hex");
    }
    if (raw_c.empty()) {
        err = "color_hex is required";
        return false;
    }
    uint8_t r1 = 0, g1 = 0, b1 = 0;
    tigertag::parse_rgb(raw_c, r1, g1, b1);

    uint8_t r2 = 0, g2 = 0, b2 = 0;
    if (fields.colors.size() > 1) {
        tigertag::parse_rgb(fields.colors[1], r2, g2, b2);
    }
    uint8_t r3 = 0, g3 = 0, b3 = 0;
    if (fields.colors.size() > 2) {
        tigertag::parse_rgb(fields.colors[2], r3, g3, b3);
    }

    int weight_g = fields.weight_g;
    if (weight_g == 0 && fields.fields.count("weight_g")) {
        weight_g = std::stoi(fields.fields.at("weight_g"));
    }
    if (weight_g <= 0) {
        err = "weight_g is required";
        return false;
    }
    uint32_t measure = static_cast<uint32_t>(weight_g);

    uint32_t meas_avail = measure;
    if (fields.fields.count("remaining_g")) {
        int rem_g = std::stoi(fields.fields.at("remaining_g"));
        meas_avail = static_cast<uint32_t>(rem_g >= 0 ? rem_g : 0);
    }

    int n_min = fields.nozzle_min != 0 ? fields.nozzle_min : mat_info->nozzle_temp_min;
    int n_max = fields.nozzle_max != 0 ? fields.nozzle_max : mat_info->nozzle_temp_max;
    int b_min = fields.bed_min != 0 ? fields.bed_min : mat_info->bed_temp_min;
    int b_max = fields.bed_max != 0 ? fields.bed_max : mat_info->bed_temp_max;
    int d_temp = fields.drying_temp != 0 ? fields.drying_temp : mat_info->drying_temp;
    // Round minutes to hours (not truncate) and clamp to the byte's range, so
    // a drying time above 255 h does not wrap when written below.
    int d_time = fields.drying_time_min != 0 ? ((fields.drying_time_min + 30) / 60)
                                             : mat_info->drying_time_h;
    if (d_time < 0) d_time = 0;
    if (d_time > 255) d_time = 255;
    if (d_temp < 0) d_temp = 0;
    if (d_temp > 255) d_temp = 255;

    std::string mfg_str = fields.fields.count("manufactured") ? fields.fields.at("manufactured") : "";
    uint32_t timestamp = tigertag::date_to_timestamp(mfg_str);

    std::vector<uint8_t> buf(144, 0);
    // 0..3: ID_TIGERTAG (0x5BF59264)
    buf[0] = (tigertag::ID_TIGERTAG >> 24) & 0xFF;
    buf[1] = (tigertag::ID_TIGERTAG >> 16) & 0xFF;
    buf[2] = (tigertag::ID_TIGERTAG >> 8)  & 0xFF;
    buf[3] = tigertag::ID_TIGERTAG & 0xFF;

    // 4..7: 0xFFFFFFFF
    buf[4] = 0xFF; buf[5] = 0xFF; buf[6] = 0xFF; buf[7] = 0xFF;

    // 8..9: material_id
    buf[8] = (mat_info->id >> 8) & 0xFF;
    buf[9] = mat_info->id & 0xFF;

    // 10: aspect1 = 0
    buf[10] = 0;
    // 11: aspect2 = 0
    buf[11] = 0;
    // 12: type = 142 (0x8E Filament)
    buf[12] = 142;
    // 13: diameter_id
    buf[13] = diam_id;

    // 14..15: brand_id
    buf[14] = (brand_info->id >> 8) & 0xFF;
    buf[15] = brand_info->id & 0xFF;

    // 16..18: r1, g1, b1
    buf[16] = r1; buf[17] = g1; buf[18] = b1;
    // 19: 0xFF
    buf[19] = 0xFF;

    // 20..22: measure
    buf[20] = (measure >> 16) & 0xFF;
    buf[21] = (measure >> 8)  & 0xFF;
    buf[22] = measure & 0xFF;
    // 23: unit = 21 (g)
    buf[23] = 21;

    // 24..25: nozzle_min
    buf[24] = (n_min >> 8) & 0xFF;
    buf[25] = n_min & 0xFF;
    // 26..27: nozzle_max
    buf[26] = (n_max >> 8) & 0xFF;
    buf[27] = n_max & 0xFF;

    // 28: drying_temp
    buf[28] = static_cast<uint8_t>(d_temp);
    // 29: drying_time
    buf[29] = static_cast<uint8_t>(d_time);
    // 30: bed_min
    buf[30] = static_cast<uint8_t>(b_min);
    // 31: bed_max
    buf[31] = static_cast<uint8_t>(b_max);

    // 32..35: timestamp
    buf[32] = (timestamp >> 24) & 0xFF;
    buf[33] = (timestamp >> 16) & 0xFF;
    buf[34] = (timestamp >> 8)  & 0xFF;
    buf[35] = timestamp & 0xFF;

    // 36..38: r2, g2, b2
    buf[36] = r2; buf[37] = g2; buf[38] = b2;
    // 39: tag_info = 0
    buf[39] = 0;
    // 40..42: r3, g3, b3
    buf[40] = r3; buf[41] = g3; buf[42] = b3;
    // 43: 0
    buf[43] = 0;
    // 44..45: td = 0
    buf[44] = 0; buf[45] = 0;
    // 46..47: 0
    buf[46] = 0; buf[47] = 0;

    // 76..78: meas_avail
    buf[76] = (meas_avail >> 16) & 0xFF;
    buf[77] = (meas_avail >> 8)  & 0xFF;
    buf[78] = meas_avail & 0xFF;
    // 79: 0
    buf[79] = 0;

    if (tag.memory.size() < 16 + 144) {
        tag.memory.resize(16 + 144, 0);
    }
    memcpy(&tag.memory[16], buf.data(), 144);

    size_t bsize = tag.block_size > 0 ? tag.block_size : 4;
    size_t first_block = 16 / bsize;
    size_t last_block = (16 + 144 - 1) / bsize;
    if (tag.readable.size() <= last_block) {
        tag.readable.resize(last_block + 1, false);
    }
    for (size_t b = first_block; b <= last_block; ++b) {
        tag.readable[b] = true;
    }

    return true;
}

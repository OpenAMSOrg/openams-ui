// Copyright (C) 2026 JR Lomas <lomas.jr@gmail.com>
// Based on openams (GPLv3) and OpenRFID (GPLv3).
// This file may be distributed under the terms of the GNU GPLv3 license.

/**
 * @file bambu_view.cpp
 * bambu::TagInfo -> DecodeResult. See bambu_view.h.
 */
#include "bambu_view.h"

#include <stdio.h>

namespace bambu_view {

void to_result(const bambu::TagInfo &info, bool ok, const char *error, DecodeResult &out) {
    out = DecodeResult();
    if (!ok) {
        out.recognized = false;
        out.format = "unknown";
        if (error && *error)
            out.info["error"] = error;
        return;
    }
    if (!info.type[0] && !info.detail[0] && !info.code[0]) {
        out.recognized = false;
        out.format = "blank";
        return;
    }

    // bambu::parse_block NUL-terminates the ASCII fields.
    Filament &f = out.filament;
    f.brand = "Bambu Lab";
    f.material = info.type;
    f.name = info.detail;
    char color[7];
    snprintf(color, sizeof(color), "%02X%02X%02X", (unsigned)(info.rgba >> 24) & 0xFF,
             (unsigned)(info.rgba >> 16) & 0xFF, (unsigned)(info.rgba >> 8) & 0xFF);
    f.color_hex = color;
    f.colors.push_back(f.color_hex);
    if (info.diameter_x100)
        f.diameter_mm = info.diameter_x100 / 100.0;
    f.weight_g = info.weight_g;
    f.length_m = info.length_m;
    f.drying_temp = info.dry_c;
    f.drying_time_min = info.dry_hours * 60;
    if (info.bed_c) {
        f.bed_min = info.bed_c;
        f.bed_max = info.bed_c;
    }
    f.nozzle_min = info.nozzle_min_c;
    f.nozzle_max = info.nozzle_max_c;
    if (info.code[0])
        f.sku = info.code;
    if (info.variant[0])
        f.fields["variant"] = info.variant;
    if (info.production[0]) {
        f.fields["production"] = info.production;
        // "YYYY_MM_DD_HH_MM" -> "YYYY-MM-DD"
        int y, mo, d;
        if (sscanf(info.production, "%4d_%2d_%2d", &y, &mo, &d) == 3) {
            char made[11];
            snprintf(made, sizeof(made), "%04d-%02d-%02d", y, mo, d);
            f.fields["manufactured"] = made;
        }
    }
    if (info.width_x100) {
        char w[16];
        snprintf(w, sizeof(w), "%.2f mm", info.width_x100 / 100.0);
        f.fields["spool_width"] = w;
    }
    if (info.tray_uid[0] || info.tray_uid[1]) {
        char hexuid[33];
        for (int i = 0; i < 16; i++)
            snprintf(hexuid + i * 2, 3, "%02x", info.tray_uid[i]);
        f.fields["tray_uid"] = hexuid;
    }

    out.recognized = true;
    out.format = "bambu";
}

}  // namespace bambu_view

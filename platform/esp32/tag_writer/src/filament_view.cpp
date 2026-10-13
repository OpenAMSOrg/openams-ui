// Copyright (C) 2026 JR Lomas <lomas.jr@gmail.com>
// This file may be distributed under the terms of the GNU GPLv3 license.

// Presentation view-model implementation for decoded filament tags.

#include "filament_view.h"

#include <cctype>
#include <cstdio>
#include <map>
#include <string>
#include <vector>

namespace fview {

bool format_is_writable(const std::string &format) {
    return format == "bambu";
}

std::string humanize_key(const std::string &k) {
    if (k.empty()) {
        return "";
    }
    std::string res = k;
    for (char &c : res) {
        if (c == '_') {
            c = ' ';
        }
    }
    res[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(res[0])));
    return res;
}

std::string humanize_format(const std::string &fmt) {
    static const std::map<std::string, std::string> known = {
        {"openspool", "OpenSpool"},
        {"spoolman", "Spoolman"},
        {"openprinttag", "OpenPrintTag"},
        {"opentag3d", "OpenTag3D"},
        {"spoolease", "SpoolEase"},
        {"tigertag", "TigerTag"},
        {"anycubic", "Anycubic"},
        {"elegoo", "Elegoo"},
        {"creality", "Creality"},
        {"snapmaker", "Snapmaker"},
        {"qidi", "QIDI"},
        {"bambu", "Bambu"},
    };
    auto it = known.find(fmt);
    if (it != known.end()) {
        return it->second;
    }
    if (fmt.empty()) {
        return "";
    }
    std::string res = fmt;
    res[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(res[0])));
    return res;
}

static bool parse_hex_color(const std::string &raw, uint8_t &r, uint8_t &g, uint8_t &b) {
    std::string hex = raw;
    if (!hex.empty() && hex[0] == '#') {
        hex = hex.substr(1);
    }
    if (hex.size() != 6) {
        return false;
    }
    for (char c : hex) {
        if (!std::isxdigit(static_cast<unsigned char>(c))) {
            return false;
        }
    }
    auto hex_val = [](char c) -> uint8_t {
        if (c >= '0' && c <= '9') return static_cast<uint8_t>(c - '0');
        if (c >= 'a' && c <= 'f') return static_cast<uint8_t>(c - 'a' + 10);
        if (c >= 'A' && c <= 'F') return static_cast<uint8_t>(c - 'A' + 10);
        return 0;
    };
    r = static_cast<uint8_t>((hex_val(hex[0]) << 4) | hex_val(hex[1]));
    g = static_cast<uint8_t>((hex_val(hex[2]) << 4) | hex_val(hex[3]));
    b = static_cast<uint8_t>((hex_val(hex[4]) << 4) | hex_val(hex[5]));
    return true;
}

ViewModel build(const DecodeResult &dr, const std::string &tag_type) {
    ViewModel vm;
    vm.format = dr.format;
    vm.tag_type = tag_type;
    vm.recognized = dr.recognized;
    vm.writable = format_is_writable(dr.format);

    const Filament &F = dr.filament;

    // Colour (vm.has_color, vm.color_rgba)
    std::string hex_source;
    if (!F.color_hex.empty()) {
        hex_source = F.color_hex;
    } else if (!F.colors.empty()) {
        hex_source = F.colors[0];
    }

    uint8_t col_r = 0, col_g = 0, col_b = 0;
    if (!hex_source.empty() && parse_hex_color(hex_source, col_r, col_g, col_b)) {
        vm.has_color = true;
        vm.color_rgba = (static_cast<uint32_t>(col_r) << 24) |
                        (static_cast<uint32_t>(col_g) << 16) |
                        (static_cast<uint32_t>(col_b) << 8) |
                        0xFFu;
    } else {
        vm.has_color = false;
        vm.color_rgba = 0;
    }

    // Title (vm.title)
    if (!F.name.empty()) {
        vm.title = F.name;
    } else if (!F.brand.empty() || !F.material.empty()) {
        if (!F.brand.empty() && !F.material.empty()) {
            vm.title = F.brand + " " + F.material;
        } else if (!F.brand.empty()) {
            vm.title = F.brand;
        } else {
            vm.title = F.material;
        }
    } else if (dr.format == "blank") {
        vm.title = "Blank tag";
    } else if (dr.format == "unknown") {
        vm.title = "Unrecognized tag";
    } else {
        vm.title = humanize_format(dr.format) + " tag";
    }

    // Subtitle (vm.subtitle)
    std::vector<std::string> sub_parts;
    if (!F.brand.empty() && vm.title.find(F.brand) == std::string::npos) {
        sub_parts.push_back(F.brand);
    }
    if (!F.material.empty() && vm.title.find(F.material) == std::string::npos) {
        sub_parts.push_back(F.material);
    }

    std::string sub_joined;
    if (sub_parts.size() == 2) {
        sub_joined = sub_parts[0] + " - " + sub_parts[1];
    } else if (sub_parts.size() == 1) {
        sub_joined = sub_parts[0];
    }

    if (sub_joined.empty() && dr.recognized) {
        vm.subtitle = humanize_format(dr.format);
    } else {
        vm.subtitle = sub_joined;
    }

    // Rows (vm.rows, IN THIS ORDER)
    // 1. "Material": F.material — only if F.material non-empty AND F.material is
    //    not a substring of vm.title and not equal to vm.subtitle.
    if (!F.material.empty() &&
        vm.title.find(F.material) == std::string::npos &&
        F.material != vm.subtitle) {
        vm.rows.push_back({"Material", F.material});
    }

    // 2. "Colour": "#" + uppercase 6-hex of the parsed colour — only if has_color.
    if (vm.has_color) {
        char col_buf[16];
        std::snprintf(col_buf, sizeof(col_buf), "#%02X%02X%02X", col_r, col_g, col_b);
        vm.rows.push_back({"Colour", std::string(col_buf)});
    }

    // 3. "Diameter": printf "%.2f mm" (F.diameter_mm) — if F.diameter_mm != 0.
    if (F.diameter_mm != 0.0) {
        char diam_buf[32];
        std::snprintf(diam_buf, sizeof(diam_buf), "%.2f mm", F.diameter_mm);
        vm.rows.push_back({"Diameter", std::string(diam_buf)});
    }

    // 4. "Weight": "<F.weight_g> g" — if F.weight_g != 0.
    if (F.weight_g != 0) {
        vm.rows.push_back({"Weight", std::to_string(F.weight_g) + " g"});
    }

    // 5. "Spool weight": "<F.spool_weight_g> g" — if F.spool_weight_g != 0.
    if (F.spool_weight_g != 0) {
        vm.rows.push_back({"Spool weight", std::to_string(F.spool_weight_g) + " g"});
    }

    // 6. "Length": "<F.length_m> m" — if F.length_m != 0.
    if (F.length_m != 0) {
        vm.rows.push_back({"Length", std::to_string(F.length_m) + " m"});
    }

    // 7. "Density": printf "%.3f g/cm3" (F.density) — if F.density != 0.
    if (F.density != 0.0) {
        char dens_buf[32];
        std::snprintf(dens_buf, sizeof(dens_buf), "%.3f g/cm3", F.density);
        vm.rows.push_back({"Density", std::string(dens_buf)});
    }

    // 8. "Nozzle": if both nozzle_min and nozzle_max != 0 -> "min-max C"
    //    (e.g. "190-230 C"); else if exactly one != 0 -> "<that> C"; else skip.
    if (F.nozzle_min != 0 && F.nozzle_max != 0) {
        vm.rows.push_back({"Nozzle", std::to_string(F.nozzle_min) + "-" +
                                     std::to_string(F.nozzle_max) + " C"});
    } else if (F.nozzle_min != 0) {
        vm.rows.push_back({"Nozzle", std::to_string(F.nozzle_min) + " C"});
    } else if (F.nozzle_max != 0) {
        vm.rows.push_back({"Nozzle", std::to_string(F.nozzle_max) + " C"});
    }

    // 9. "Bed": same rule as Nozzle with bed_min/bed_max.
    if (F.bed_min != 0 && F.bed_max != 0) {
        vm.rows.push_back({"Bed", std::to_string(F.bed_min) + "-" +
                                  std::to_string(F.bed_max) + " C"});
    } else if (F.bed_min != 0) {
        vm.rows.push_back({"Bed", std::to_string(F.bed_min) + " C"});
    } else if (F.bed_max != 0) {
        vm.rows.push_back({"Bed", std::to_string(F.bed_max) + " C"});
    }

    // 10. "Drying": if drying_temp!=0 and drying_time_min!=0 ->
    //     "<temp> C, <time> min"; else if drying_temp!=0 -> "<temp> C";
    //     else if drying_time_min!=0 -> "<time> min"; else skip.
    if (F.drying_temp != 0 && F.drying_time_min != 0) {
        vm.rows.push_back({"Drying", std::to_string(F.drying_temp) + " C, " +
                                     std::to_string(F.drying_time_min) + " min"});
    } else if (F.drying_temp != 0) {
        vm.rows.push_back({"Drying", std::to_string(F.drying_temp) + " C"});
    } else if (F.drying_time_min != 0) {
        vm.rows.push_back({"Drying", std::to_string(F.drying_time_min) + " min"});
    }

    // 11. "SKU": F.sku — if non-empty.
    if (!F.sku.empty()) {
        vm.rows.push_back({"SKU", F.sku});
    }

    // 12. "Spool ID": F.spool_id — if non-empty.
    if (!F.spool_id.empty()) {
        vm.rows.push_back({"Spool ID", F.spool_id});
    }

    // 13. Then, for each (k,v) in F.fields in ascending key order:
    //     row {humanize_key(k), v} when v non-empty.
    for (const auto &kv : F.fields) {
        if (!kv.second.empty()) {
            vm.rows.push_back({humanize_key(kv.first), kv.second});
        }
    }

    // 14. Then, for each (k,v) in dr.info in ascending key order, EXCEPT k=="error":
    //     row {humanize_key(k), v} when v non-empty.
    for (const auto &kv : dr.info) {
        if (kv.first != "error" && !kv.second.empty()) {
            vm.rows.push_back({humanize_key(kv.first), kv.second});
        }
    }

    // Status (vm.status)
    auto err_it = dr.info.find("error");
    if (err_it != dr.info.end() && !err_it->second.empty()) {
        vm.status = "Error: " + err_it->second;
    } else if (dr.format == "blank") {
        vm.status = "Blank tag";
    } else if (!dr.recognized) {
        vm.status = "Unrecognized tag";
    } else if (!vm.writable) {
        vm.status = "Read-only format";
    } else {
        vm.status = "";
    }

    return vm;
}

}  // namespace fview

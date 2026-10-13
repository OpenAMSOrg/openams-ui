// Copyright (C) 2026 JR Lomas <lomas.jr@gmail.com>
// This file may be distributed under the terms of the GNU GPLv3 license.

// Presentation view-model for a decoded tag. Turns the format-agnostic
// DecodeResult (any filament format) into display-ready text, rows, a colour
// swatch and status flags that the touchscreen (ui.cpp) and any other front
// end render directly. Pure logic, no LVGL — unit-tested on the host.
#pragma once

#include <stdint.h>
#include <string>
#include <vector>

#include "formatcodec.h"

namespace fview {

// One label/value detail line, in display order.
struct Row {
    std::string label;   // e.g. "Material", "Nozzle"
    std::string value;   // e.g. "PLA", "190-230 C"
};

struct ViewModel {
    std::string format;        // DecodeResult.format: "bambu","openspool",
                               // "blank","unknown", ...
    std::string tag_type;      // nfc.tag constant: "ntag215", "mifare_classic_1k"
    std::string title;         // main heading: product name, else brand+material,
                               // else a friendly format/blank/unknown label
    std::string subtitle;      // secondary line: "Bambu Lab - PLA", a format
                               // badge, or empty
    bool recognized = false;   // a codec claimed the tag (DecodeResult.recognized)
    bool has_color = false;    // a primary colour is known
    uint32_t color_rgba = 0;   // 0xRRGGBBAA for the swatch (AA = 0xFF opaque)
    std::vector<Row> rows;     // detail rows to list, in order
    bool writable = false;     // the firmware can write this format back
    std::string status;        // one-line status: "Read-only format",
                               // "Blank tag", "Unrecognized tag", or an error
};

// Build the view model from a decode result and the detected tag type
// (tag_type_to_string(...)). Pure function; never touches the reader.
ViewModel build(const DecodeResult &dr, const std::string &tag_type);

// True for formats the firmware can currently write back to a tag.
bool format_is_writable(const std::string &format);

}  // namespace fview

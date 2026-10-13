// Copyright (C) 2026 JR Lomas <lomas.jr@gmail.com>
// This file may be distributed under the terms of the GNU GPLv3 license.

// Filament-tag format codec interface for the tag-writer firmware, ported
// from klipper_rf formats/__init__.py (FormatCodec + decode_tag dispatch).
// Each codec recognizes one on-tag format and decodes it to a common
// Filament model; some can encode (write) it. Codecs read the Tag's memory
// (and parse NDEF/CBOR/AES via the primitive libs) themselves.
#pragma once

#include "tag.h"
#include <map>
#include <string>
#include <vector>

// The decoded filament (mirror of formats/__init__.py filament()). A field is
// "unset" at its zero value; `fields` carries format-specific extras. The app
// maps this onto its UI model.
struct Filament {
    std::string brand, material, name, color_hex;
    std::vector<std::string> colors;          // multi-colour tags
    double diameter_mm = 0.0, density = 0.0;
    int weight_g = 0, spool_weight_g = 0, length_m = 0;
    int nozzle_min = 0, nozzle_max = 0, bed_min = 0, bed_max = 0;
    int drying_temp = 0, drying_time_min = 0;
    std::string sku, spool_id;
    std::map<std::string, std::string> fields;   // catch-all extras
};

struct DecodeResult {
    bool recognized = false;    // a codec claimed the tag
    std::string format;         // codec name, or "blank" / "unknown"
    Filament filament;
    std::map<std::string, std::string> info;     // non-filament info
};

struct CodecOptions {
    // keys from create_codecs(): bambu_key, creality_key/_data_key,
    // snapmaker_key, openprinttag_workgroup, ... (hex decoded by the app).
    std::map<std::string, std::string> values;
    const std::string &get(const std::string &k) const;
    bool has(const std::string &k) const { return values.count(k) != 0; }
};

class FormatCodec {
public:
    virtual ~FormatCodec() {}
    virtual const char *name() const = 0;
    // Tag types this codec applies to (empty = any). As tag.h TagType names.
    virtual std::vector<std::string> tag_types() const { return {}; }
    // MIFARE Classic keys for `uid` (sector -> keyA/keyB), or empty.
    virtual void mifare_keys(const uint8_t *uid, size_t uid_len,
                             std::map<int, std::pair<std::vector<uint8_t>,
                             std::vector<uint8_t>>> &out) { (void)uid;
                             (void)uid_len; (void)out; }
    // Decode the tag; return true and fill `out` if this codec recognizes it.
    virtual bool decode(const Tag &tag, DecodeResult &out) = 0;
    virtual bool can_encode(const Tag &tag) { (void)tag; return false; }
    // Encode `fields` onto `tag` (fills tag memory / plans writes). false +err
    // if unsupported or invalid.
    virtual bool encode(Tag &tag, const Filament &fields,
                        std::string &err) {
        (void)tag; (void)fields;
        err = std::string(name()) + ": writing is not supported";
        return false;
    }
};

// Dispatch (formats/__init__.py decode_tag/create_codecs/find_codec).
// Build the codec set from options, try each against the tag, and return the
// winner's result or {format:"blank"|"unknown"}.
std::vector<FormatCodec *> create_codecs(const CodecOptions &options);
FormatCodec *find_codec(const std::vector<FormatCodec *> &codecs,
                        const std::string &name);
DecodeResult decode_tag(const Tag &tag,
                        const std::vector<FormatCodec *> &codecs);

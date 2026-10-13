// Copyright (C) 2026 JR Lomas <lomas.jr@gmail.com>
// Based on OpenRFID (GPLv3).
// This file may be distributed under the terms of the GNU GPLv3 license.

#pragma once

#include "formatcodec.h"
#include <map>
#include <string>
#include <vector>

namespace qidi_data {

struct MaterialInfo {
    uint8_t code;
    const char *name;
    const char *type;
    int nozzle_temp_min;
    int nozzle_temp_max;
};

struct ColorInfo {
    uint8_t code;
    const char *hex;
};

const MaterialInfo *find_material(uint8_t code);
const char *find_color(uint8_t code);
const char *find_vendor(uint8_t code);

} // namespace qidi_data

class QidiCodec : public FormatCodec {
public:
    explicit QidiCodec(const CodecOptions &options = {});
    ~QidiCodec() override = default;

    const char *name() const override { return "qidi"; }
    std::vector<std::string> tag_types() const override;
    void mifare_keys(const uint8_t *uid, size_t uid_len,
                     std::map<int, std::pair<std::vector<uint8_t>,
                     std::vector<uint8_t>>> &out) override;
    bool can_encode(const Tag &tag) override;
    bool decode(const Tag &tag, DecodeResult &out) override;
    bool encode(Tag &tag, const Filament &fields, std::string &err) override;
};

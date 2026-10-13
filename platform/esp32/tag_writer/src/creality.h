// Copyright (C) 2026 JR Lomas <lomas.jr@gmail.com>
// Based on OpenRFID (GPLv3).
// This file may be distributed under the terms of the GNU GPLv3 license.

#pragma once

#include "formatcodec.h"
#include <map>
#include <string>
#include <vector>

namespace creality_data {

struct MaterialInfo {
    const char *code;
    const char *type;
    const char *modifier1;
    const char *modifier2;
    double nozzle_min;
    double nozzle_max;
    double bed;
    double dry_temp;
    double dry_hours;
};

const MaterialInfo *find_material(const std::string &code);

} // namespace creality_data

class CrealityCodec : public FormatCodec {
public:
    explicit CrealityCodec(const CodecOptions &options = {}, bool validate_key_hash = true);
    ~CrealityCodec() override = default;

    const char *name() const override { return "creality"; }
    std::vector<std::string> tag_types() const override;
    void mifare_keys(const uint8_t *uid, size_t uid_len,
                     std::map<int, std::pair<std::vector<uint8_t>,
                     std::vector<uint8_t>>> &out) override;
    bool can_encode(const Tag &tag) override;
    bool decode(const Tag &tag, DecodeResult &out) override;
    bool encode(Tag &tag, const Filament &fields, std::string &err) override;

    void set_master_key(const std::vector<uint8_t> &key);
    void set_data_key(const std::vector<uint8_t> &key);
    bool has_master_key() const { return has_key_; }
    bool has_data_key() const { return has_data_key_; }

    static const uint8_t WRITABLE_ACCESS[4];

private:
    bool has_key_ = false;
    uint8_t master_key_[16] = {0};
    bool has_data_key_ = false;
    uint8_t data_key_[16] = {0};

    static std::string material_code(const Filament &fields);
};

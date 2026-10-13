// Copyright (C) 2026 JR Lomas <lomas.jr@gmail.com>
// Based on OpenRFID (GPLv3).
// This file may be distributed under the terms of the GNU GPLv3 license.

#pragma once

#include "formatcodec.h"
#include <map>
#include <string>
#include <vector>

namespace snapmaker_data {

const char *find_main_type(uint16_t code);
const char *find_sub_type(uint16_t code);

} // namespace snapmaker_data

class SnapmakerCodec : public FormatCodec {
public:
    explicit SnapmakerCodec(const CodecOptions &options = {}, bool validate_key_hash = true);
    ~SnapmakerCodec() override = default;

    const char *name() const override { return "snapmaker"; }
    std::vector<std::string> tag_types() const override;
    void mifare_keys(const uint8_t *uid, size_t uid_len,
                     std::map<int, std::pair<std::vector<uint8_t>,
                     std::vector<uint8_t>>> &out) override;
    bool can_encode(const Tag &tag) override { (void)tag; return false; }
    bool decode(const Tag &tag, DecodeResult &out) override;
    bool encode(Tag &tag, const Filament &fields, std::string &err) override;

    void set_vendor_salt(const std::vector<uint8_t> &salt);
    bool has_vendor_salt() const { return has_salt_; }

private:
    bool has_salt_ = false;
    std::vector<uint8_t> salt_;
};

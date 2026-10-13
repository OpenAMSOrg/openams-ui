// Copyright (C) 2026 JR Lomas <lomas.jr@gmail.com>
// Based on openams (GPLv3) and OpenRFID (GPLv3).
// This file may be distributed under the terms of the GNU GPLv3 license.

// Bambu Lab MIFARE Classic 1K codec for the universal decode path.
//
// A thin FormatCodec over the firmware's bambu_format: mifare_keys() hands the
// reader this UID's HKDF-derived sector keys (so tag_reader reads the tag with
// them and sets key_source="bambu"), and decode() parses the read blocks into
// the normalized Filament. Writing Bambu tags stays on the existing tag_ops /
// OP_WRITE flow; this codec only decodes. The master key never leaves the
// device.
#pragma once

#include "bambu_format.h"
#include "formatcodec.h"

class BambuCodec : public FormatCodec {
public:
    explicit BambuCodec(const CodecOptions &options = CodecOptions());
    ~BambuCodec() override = default;

    const char *name() const override { return "bambu"; }
    std::vector<std::string> tag_types() const override;
    void mifare_keys(const uint8_t *uid, size_t uid_len,
                     std::map<int, std::pair<std::vector<uint8_t>,
                     std::vector<uint8_t>>> &out) override;
    bool decode(const Tag &tag, DecodeResult &out) override;

private:
    bool has_key_ = false;
    uint8_t master_key_[bambu::MASTER_KEY_SIZE] = {0};
};

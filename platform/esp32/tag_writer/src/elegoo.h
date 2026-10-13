// Copyright (C) 2026 JR Lomas <lomas.jr@gmail.com>
// Based on OpenRFID and canvas-rfid-tool (GPLv3/MIT).
// This file may be distributed under the terms of the GNU GPLv3 license.

#pragma once

#include "formatcodec.h"

class ElegooCodec : public FormatCodec {
public:
    ElegooCodec() = default;
    explicit ElegooCodec(const CodecOptions &options) { (void)options; }
    ~ElegooCodec() override = default;

    const char *name() const override { return "elegoo"; }
    std::vector<std::string> tag_types() const override;
    bool can_encode(const Tag &tag) override { (void)tag; return false; }
    bool decode(const Tag &tag, DecodeResult &out) override;
};

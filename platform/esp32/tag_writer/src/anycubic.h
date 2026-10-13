// Copyright (C) 2026 JR Lomas <lomas.jr@gmail.com>
// Based on OpenRFID and ACE-RFID (GPLv3).
// This file may be distributed under the terms of the GNU GPLv3 license.

#pragma once

#include "formatcodec.h"

class AnycubicCodec : public FormatCodec {
public:
    AnycubicCodec() = default;
    explicit AnycubicCodec(const CodecOptions &options) { (void)options; }
    ~AnycubicCodec() override = default;

    const char *name() const override { return "anycubic"; }
    std::vector<std::string> tag_types() const override;
    bool can_encode(const Tag &tag) override;
    bool decode(const Tag &tag, DecodeResult &out) override;
    bool encode(Tag &tag, const Filament &fields, std::string &err) override;
};

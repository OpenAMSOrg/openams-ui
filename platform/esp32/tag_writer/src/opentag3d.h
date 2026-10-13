// Copyright (C) 2026 JR Lomas <lomas.jr@gmail.com>
// Based on the OpenTag3D spec (GPLv3).
// This file may be distributed under the terms of the GNU GPLv3 license.

#pragma once

#include "formatcodec.h"

class OpenTag3DCodec : public FormatCodec {
public:
    OpenTag3DCodec() = default;
    explicit OpenTag3DCodec(const CodecOptions &options) { (void)options; }
    ~OpenTag3DCodec() override = default;

    const char *name() const override { return "opentag3d"; }
    std::vector<std::string> tag_types() const override;
    bool can_encode(const Tag &tag) override;
    bool decode(const Tag &tag, DecodeResult &out) override;
    bool encode(Tag &tag, const Filament &fields, std::string &err) override;
};

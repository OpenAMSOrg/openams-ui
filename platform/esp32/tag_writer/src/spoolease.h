// Copyright (C) 2026 JR Lomas <lomas.jr@gmail.com>
// Based on OpenRFID (GPLv3).
// This file may be distributed under the terms of the GNU GPLv3 license.

#pragma once

#include "formatcodec.h"

class SpoolEaseCodec : public FormatCodec {
public:
    SpoolEaseCodec() = default;
    explicit SpoolEaseCodec(const CodecOptions &options) { (void)options; }
    ~SpoolEaseCodec() override = default;

    const char *name() const override { return "spoolease"; }
    std::vector<std::string> tag_types() const override;
    bool can_encode(const Tag &tag) override { (void)tag; return false; }
    bool decode(const Tag &tag, DecodeResult &out) override;

private:
    bool parse_uri(const std::string &uri, DecodeResult &out);
};

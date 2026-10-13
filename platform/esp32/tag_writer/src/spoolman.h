// Copyright (C) 2026 JR Lomas <lomas.jr@gmail.com>
// This file may be distributed under the terms of the GNU GPLv3 license.

#pragma once

#include "formatcodec.h"
#include <string>
#include <vector>

class SpoolmanCodec : public FormatCodec {
public:
    explicit SpoolmanCodec(const CodecOptions &options = CodecOptions());
    virtual ~SpoolmanCodec() override {}

    virtual const char *name() const override { return "spoolman"; }
    virtual std::vector<std::string> tag_types() const override;

    virtual bool decode(const Tag &tag, DecodeResult &out) override;
    virtual bool can_encode(const Tag &tag) override;
    virtual bool encode(Tag &tag, const Filament &fields, std::string &err) override;

private:
    CodecOptions options_;
};

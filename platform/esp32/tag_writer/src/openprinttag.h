// Copyright (C) 2026 JR Lomas <lomas.jr@gmail.com>
// This file may be distributed under the terms of the GNU GPLv3 license.

#pragma once

#include "formatcodec.h"
#include <map>
#include <string>
#include <vector>

namespace openprinttag_data {
    extern const char * const MIME_TYPE;

    struct MaterialDefaults {
        int min_print_temperature = 0;
        int max_print_temperature = 0;
        int min_bed_temperature = 0;
        int max_bed_temperature = 0;
        int chamber_temperature = 0;
        int min_chamber_temperature = 0;
        int max_chamber_temperature = 0;
        int drying_temperature = 0;
        int drying_time = 0;
        int preheat_temperature = 0;
        int heatbreak_temperature = 0;
        double density = 0.0;
        double min_nozzle_diameter = 0.0;
    };

    bool lookup_material_defaults(const std::string &material, int &min_temp, int &max_temp);
}

class OpenPrintTagCodec : public FormatCodec {
public:
    explicit OpenPrintTagCodec(const CodecOptions &options = CodecOptions());
    virtual ~OpenPrintTagCodec() override {}

    virtual const char *name() const override { return "openprinttag"; }
    virtual std::vector<std::string> tag_types() const override;

    virtual bool decode(const Tag &tag, DecodeResult &out) override;
    virtual bool can_encode(const Tag &tag) override;
    virtual bool encode(Tag &tag, const Filament &fields, std::string &err) override;

private:
    CodecOptions options_;
};

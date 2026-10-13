// Copyright (C) 2026 JR Lomas <lomas.jr@gmail.com>
// Based on OpenRFID (GPLv3).
// This file may be distributed under the terms of the GNU GPLv3 license.

#pragma once

#include "formatcodec.h"
#include <map>
#include <string>
#include <vector>

namespace json_util {
    enum class Type {
        Null,
        Bool,
        Number,
        String,
        Array,
        Object
    };

    struct Value {
        Type type = Type::Null;
        bool bool_val = false;
        double num_val = 0.0;
        int64_t int_val = 0;
        bool is_int = false;
        std::string str_val;
        std::vector<Value> arr_val;
        std::map<std::string, Value> obj_val;

        bool is_null() const { return type == Type::Null; }
        bool is_bool() const { return type == Type::Bool; }
        bool is_number() const { return type == Type::Number; }
        bool is_string() const { return type == Type::String; }
        bool is_array() const { return type == Type::Array; }
        bool is_object() const { return type == Type::Object; }

        const Value *get(const std::string &key) const;
        std::string as_string(const std::string &def = "") const;
        int64_t as_int64(int64_t def = 0) const;
        double as_double(double def = 0.0) const;
    };

    bool parse(const std::string &src, Value &out);
    std::string stringify(const Value &val);
}

class OpenSpoolCodec : public FormatCodec {
public:
    explicit OpenSpoolCodec(const CodecOptions &options = CodecOptions());
    virtual ~OpenSpoolCodec() override {}

    virtual const char *name() const override { return "openspool"; }
    virtual std::vector<std::string> tag_types() const override;

    virtual bool decode(const Tag &tag, DecodeResult &out) override;
    virtual bool can_encode(const Tag &tag) override;
    virtual bool encode(Tag &tag, const Filament &fields, std::string &err) override;

private:
    CodecOptions options_;
};

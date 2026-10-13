// Copyright (C) 2026 JR Lomas <lomas.jr@gmail.com>
// Based on OpenRFID (GPLv3).
// This file may be distributed under the terms of the GNU GPLv3 license.

#include "openspool.h"
#include "openprinttag.h"
#include "ndef.h"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <cstring>

namespace json_util {

const Value *Value::get(const std::string &key) const {
    if (type != Type::Object) return nullptr;
    auto it = obj_val.find(key);
    if (it != obj_val.end()) return &it->second;
    return nullptr;
}

std::string Value::as_string(const std::string &def) const {
    if (type == Type::String) return str_val;
    if (type == Type::Number) {
        if (is_int) return std::to_string(int_val);
        char buf[64];
        snprintf(buf, sizeof(buf), "%g", num_val);
        return buf;
    }
    if (type == Type::Bool) return bool_val ? "true" : "false";
    return def;
}

int64_t Value::as_int64(int64_t def) const {
    if (type == Type::Number) return int_val;
    if (type == Type::String) {
        try {
            return std::stoll(str_val);
        } catch (...) {
            return def;
        }
    }
    if (type == Type::Bool) return bool_val ? 1 : 0;
    return def;
}

double Value::as_double(double def) const {
    if (type == Type::Number) return num_val;
    if (type == Type::String) {
        try {
            return std::stod(str_val);
        } catch (...) {
            return def;
        }
    }
    if (type == Type::Bool) return bool_val ? 1.0 : 0.0;
    return def;
}

static void skip_ws(const std::string &s, size_t &pos) {
    while (pos < s.size() && (s[pos] == ' ' || s[pos] == '\t' || s[pos] == '\n' || s[pos] == '\r')) {
        ++pos;
    }
}

static bool parse_string(const std::string &s, size_t &pos, std::string &out) {
    out.clear();
    if (pos >= s.size() || s[pos] != '"') return false;
    ++pos;
    while (pos < s.size()) {
        char c = s[pos++];
        if (c == '"') {
            return true;
        }
        if (c == '\\') {
            if (pos >= s.size()) return false;
            char esc = s[pos++];
            switch (esc) {
                case '"': out += '"'; break;
                case '\\': out += '\\'; break;
                case '/': out += '/'; break;
                case 'b': out += '\b'; break;
                case 'f': out += '\f'; break;
                case 'n': out += '\n'; break;
                case 'r': out += '\r'; break;
                case 't': out += '\t'; break;
                case 'u': {
                    if (pos + 4 > s.size()) return false;
                    std::string hex_str = s.substr(pos, 4);
                    pos += 4;
                    unsigned long cp = std::strtoul(hex_str.c_str(), nullptr, 16);
                    if (cp < 0x80) {
                        out += static_cast<char>(cp);
                    } else if (cp < 0x800) {
                        out += static_cast<char>(0xC0 | (cp >> 6));
                        out += static_cast<char>(0x80 | (cp & 0x3F));
                    } else {
                        out += static_cast<char>(0xE0 | (cp >> 12));
                        out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
                        out += static_cast<char>(0x80 | (cp & 0x3F));
                    }
                    break;
                }
                default:
                    out += esc;
                    break;
            }
        } else {
            out += c;
        }
    }
    return false;
}

static const int JSON_MAX_DEPTH = 8;  // bounds recursion on the 6 KB reader task
static bool parse_val(const std::string &s, size_t &pos, Value &val, int depth);

static bool parse_number(const std::string &s, size_t &pos, Value &val) {
    size_t start = pos;
    if (pos < s.size() && s[pos] == '-') ++pos;
    bool has_digits = false;
    while (pos < s.size() && std::isdigit(static_cast<unsigned char>(s[pos]))) {
        has_digits = true;
        ++pos;
    }
    if (!has_digits) return false;
    bool is_float = false;
    if (pos < s.size() && s[pos] == '.') {
        is_float = true;
        ++pos;
        while (pos < s.size() && std::isdigit(static_cast<unsigned char>(s[pos]))) {
            ++pos;
        }
    }
    if (pos < s.size() && (s[pos] == 'e' || s[pos] == 'E')) {
        is_float = true;
        ++pos;
        if (pos < s.size() && (s[pos] == '+' || s[pos] == '-')) ++pos;
        while (pos < s.size() && std::isdigit(static_cast<unsigned char>(s[pos]))) {
            ++pos;
        }
    }
    std::string num_str = s.substr(start, pos - start);
    val.type = Type::Number;
    char *endptr = nullptr;
    if (!is_float) {
        val.int_val = std::strtoll(num_str.c_str(), &endptr, 10);
        val.num_val = static_cast<double>(val.int_val);
        val.is_int = true;
    } else {
        val.num_val = std::strtod(num_str.c_str(), &endptr);
        // A non-finite or huge value would make the int cast undefined.
        val.int_val = (val.num_val >= -9.2e18 && val.num_val <= 9.2e18)
                          ? static_cast<int64_t>(val.num_val) : 0;
        val.is_int = false;
    }
    return true;
}

static bool parse_array(const std::string &s, size_t &pos, Value &val, int depth) {
    if (pos >= s.size() || s[pos] != '[') return false;
    ++pos;
    val.type = Type::Array;
    val.arr_val.clear();
    skip_ws(s, pos);
    if (pos < s.size() && s[pos] == ']') {
        ++pos;
        return true;
    }
    while (pos < s.size()) {
        Value elem;
        if (!parse_val(s, pos, elem, depth)) return false;
        val.arr_val.push_back(std::move(elem));
        skip_ws(s, pos);
        if (pos < s.size() && s[pos] == ']') {
            ++pos;
            return true;
        }
        if (pos >= s.size() || s[pos] != ',') return false;
        ++pos;
        skip_ws(s, pos);
    }
    return false;
}

static bool parse_object(const std::string &s, size_t &pos, Value &val, int depth) {
    if (pos >= s.size() || s[pos] != '{') return false;
    ++pos;
    val.type = Type::Object;
    val.obj_val.clear();
    skip_ws(s, pos);
    if (pos < s.size() && s[pos] == '}') {
        ++pos;
        return true;
    }
    while (pos < s.size()) {
        skip_ws(s, pos);
        std::string key;
        if (!parse_string(s, pos, key)) return false;
        skip_ws(s, pos);
        if (pos >= s.size() || s[pos] != ':') return false;
        ++pos;
        skip_ws(s, pos);
        Value elem;
        if (!parse_val(s, pos, elem, depth)) return false;
        val.obj_val[key] = std::move(elem);
        skip_ws(s, pos);
        if (pos < s.size() && s[pos] == '}') {
            ++pos;
            return true;
        }
        if (pos >= s.size() || s[pos] != ',') return false;
        ++pos;
        skip_ws(s, pos);
    }
    return false;
}

static bool parse_val(const std::string &s, size_t &pos, Value &val, int depth) {
    if (depth > JSON_MAX_DEPTH) return false;
    skip_ws(s, pos);
    if (pos >= s.size()) return false;
    char c = s[pos];
    if (c == '"') {
        val.type = Type::String;
        return parse_string(s, pos, val.str_val);
    }
    if (c == '{') {
        return parse_object(s, pos, val, depth + 1);
    }
    if (c == '[') {
        return parse_array(s, pos, val, depth + 1);
    }
    if (c == '-' || std::isdigit(static_cast<unsigned char>(c))) {
        return parse_number(s, pos, val);
    }
    if (s.compare(pos, 4, "true") == 0) {
        pos += 4;
        val.type = Type::Bool;
        val.bool_val = true;
        return true;
    }
    if (s.compare(pos, 5, "false") == 0) {
        pos += 5;
        val.type = Type::Bool;
        val.bool_val = false;
        return true;
    }
    if (s.compare(pos, 4, "null") == 0) {
        pos += 4;
        val.type = Type::Null;
        return true;
    }
    return false;
}

bool parse(const std::string &src, Value &out) {
    size_t pos = 0;
    skip_ws(src, pos);
    if (!parse_val(src, pos, out, 0)) return false;
    skip_ws(src, pos);
    return pos == src.size();
}

std::string stringify(const Value &val) {
    switch (val.type) {
        case Type::Null:
            return "null";
        case Type::Bool:
            return val.bool_val ? "true" : "false";
        case Type::Number: {
            if (val.is_int) return std::to_string(val.int_val);
            char buf[64];
            snprintf(buf, sizeof(buf), "%g", val.num_val);
            return buf;
        }
        case Type::String: {
            std::string out = "\"";
            for (char c : val.str_val) {
                if (c == '"') out += "\\\"";
                else if (c == '\\') out += "\\\\";
                else if (c == '\b') out += "\\b";
                else if (c == '\f') out += "\\f";
                else if (c == '\n') out += "\\n";
                else if (c == '\r') out += "\\r";
                else if (c == '\t') out += "\\t";
                else if ((unsigned char)c < 0x20) {
                    char u[8];
                    snprintf(u, sizeof(u), "\\u%04x", (unsigned char)c);
                    out += u;
                } else out += c;
            }
            out += "\"";
            return out;
        }
        case Type::Array: {
            std::string out = "[";
            for (size_t i = 0; i < val.arr_val.size(); ++i) {
                if (i > 0) out += ",";
                out += stringify(val.arr_val[i]);
            }
            out += "]";
            return out;
        }
        case Type::Object: {
            std::string out = "{";
            bool first = true;
            for (const auto &kv : val.obj_val) {
                if (!first) out += ",";
                first = false;
                Value k_val;
                k_val.type = Type::String;
                k_val.str_val = kv.first;
                out += stringify(k_val) + ":" + stringify(kv.second);
            }
            out += "}";
            return out;
        }
    }
    return "null";
}

} // namespace json_util

static std::string clean_color(const std::string &c) {
    if (c.empty()) return "";
    size_t start = 0;
    while (start < c.size() && std::isspace(static_cast<unsigned char>(c[start]))) {
        ++start;
    }
    if (start < c.size() && c[start] == '#') {
        ++start;
    }
    size_t end = c.size();
    while (end > start && std::isspace(static_cast<unsigned char>(c[end - 1]))) {
        --end;
    }
    std::string s = c.substr(start, end - start);
    if (s.size() < 6) return "";
    std::string out;
    for (size_t i = 0; i < 6; ++i) {
        out += static_cast<char>(std::toupper(static_cast<unsigned char>(s[i])));
    }
    return out;
}

static ndef::Tag to_ndef_tag(const Tag &tag) {
    ndef::Tag ntag;
    if (tag.technology == TagTechnology::ISO14443A) {
        ntag.technology = ndef::Technology::ISO14443A;
    } else if (tag.technology == TagTechnology::ISO15693) {
        ntag.technology = ndef::Technology::ISO15693;
    } else {
        ntag.technology = ndef::Technology::Unknown;
    }
    switch (tag.tag_type) {
        case TagType::NTAG213: ntag.tag_type = ndef::TagType::NTAG213; break;
        case TagType::NTAG215: ntag.tag_type = ndef::TagType::NTAG215; break;
        case TagType::NTAG216: ntag.tag_type = ndef::TagType::NTAG216; break;
        case TagType::ICODE_SLIX2: ntag.tag_type = ndef::TagType::ICODE_SLIX2; break;
        case TagType::TYPE5: ntag.tag_type = ndef::TagType::Type5; break;
        default:
            if (is_type2_tag(tag.tag_type)) ntag.tag_type = ndef::TagType::Type2;
            else if (is_type5_tag(tag.tag_type)) ntag.tag_type = ndef::TagType::Type5;
            else if (is_mifare_classic_tag(tag.tag_type)) ntag.tag_type = ndef::TagType::MifareClassic;
            else ntag.tag_type = ndef::TagType::Unknown;
            break;
    }
    ntag.memory = tag.memory;
    ntag.block_size = tag.block_size;
    ntag.user_first_block = tag.user_first_block >= 0 ? tag.user_first_block : 0;
    ntag.user_last_block = tag.user_last_block >= 0 ? tag.user_last_block : 0;
    return ntag;
}

OpenSpoolCodec::OpenSpoolCodec(const CodecOptions &options)
    : options_(options) {}

std::vector<std::string> OpenSpoolCodec::tag_types() const {
    return {
        TagTypes::NTAG213,
        TagTypes::NTAG215,
        TagTypes::NTAG216,
        TagTypes::ULTRALIGHT,
        TagTypes::ULTRALIGHT_EV1,
        TagTypes::TYPE2,
        TagTypes::ICODE_SLIX,
        TagTypes::ICODE_SLIX2,
        TagTypes::ICODE_SLIX_S,
        TagTypes::ICODE_SLIX_L,
        TagTypes::ICODE_DNA,
        TagTypes::TYPE5
    };
}

bool OpenSpoolCodec::can_encode(const Tag &tag) {
    bool match_type = false;
    std::string tname = tag_type_to_string(tag.tag_type);
    for (const auto &t : tag_types()) {
        if (t == tname) {
            match_type = true;
            break;
        }
    }
    if (!match_type &&
        tag.technology != TagTechnology::ISO14443A &&
        tag.technology != TagTechnology::ISO15693) {
        return false;
    }
    ndef::Tag ntag = to_ndef_tag(tag);
    ndef::NdefMessage msg;
    if (ndef::read_message(ntag, msg)) {
        if (msg.capacity < 120) {
            return false;
        }
    }
    if (tag.user_capacity() > 0 && tag.user_capacity() < 120) {
        return false;
    }
    return true;
}

bool OpenSpoolCodec::decode(const Tag &tag, DecodeResult &out) {
    ndef::Tag ntag = to_ndef_tag(tag);
    ndef::NdefMessage msg;
    if (!ndef::read_message(ntag, msg)) {
        return false;
    }

    json_util::Value target;
    bool found = false;
    for (const auto &rec : msg.records) {
        if (rec.is_mime("application/json")) {
            std::string payload_str(rec.payload.begin(), rec.payload.end());
            json_util::Value obj;
            if (json_util::parse(payload_str, obj) && obj.is_object()) {
                const auto *p = obj.get("protocol");
                if (p && p->as_string() == "openspool") {
                    target = std::move(obj);
                    found = true;
                    break;
                }
            }
        } else if (rec.is_text()) {
            std::string text_str = rec.text();
            json_util::Value obj;
            if (json_util::parse(text_str, obj) && obj.is_object()) {
                const auto *p = obj.get("protocol");
                if (p && p->as_string() == "openspool") {
                    target = std::move(obj);
                    found = true;
                    break;
                }
            }
        }
    }

    if (!found) return false;

    out.recognized = true;
    out.format = "openspool";
    Filament &fil = out.filament;

    const auto *mat_type = target.get("type");
    if (mat_type) {
        fil.material = mat_type->as_string();
        for (char &c : fil.material) c = toupper(static_cast<unsigned char>(c));
    }

    std::string subtype;
    const auto *st = target.get("subtype");
    if (st) {
        subtype = st->as_string();
    }
    if (!subtype.empty() && mat_type) {
        fil.name = subtype + " " + mat_type->as_string();
    }

    const auto *brand = target.get("brand");
    if (brand) {
        fil.brand = brand->as_string();
    }

    const auto *c_hex = target.get("color_hex");
    if (c_hex) {
        fil.color_hex = clean_color(c_hex->as_string());
        if (!fil.color_hex.empty()) {
            fil.colors.push_back(fil.color_hex);
        }
    }

    const auto *extra_colors = target.get("additional_color_hexes");
    if (extra_colors && extra_colors->is_array()) {
        for (const auto &c : extra_colors->arr_val) {
            std::string cleaned = clean_color(c.as_string());
            if (!cleaned.empty()) {
                fil.colors.push_back(cleaned);
            }
        }
    }

    const auto *min_temp = target.get("min_temp");
    if (min_temp) {
        fil.nozzle_min = static_cast<int>(min_temp->as_int64());
    }
    const auto *max_temp = target.get("max_temp");
    if (max_temp) {
        fil.nozzle_max = static_cast<int>(max_temp->as_int64());
    }

    const auto *bed_min = target.get("bed_min_temp");
    if (bed_min) {
        fil.bed_min = static_cast<int>(bed_min->as_int64());
    }
    const auto *bed_max = target.get("bed_max_temp");
    if (bed_max) {
        fil.bed_max = static_cast<int>(bed_max->as_int64());
    }

    const auto *weight = target.get("weight");
    if (weight) {
        fil.weight_g = static_cast<int>(weight->as_double());
    }

    const auto *diameter = target.get("diameter");
    if (diameter) {
        fil.diameter_mm = diameter->as_double();
    }

    const auto *spool_id = target.get("spool_id");
    if (spool_id) {
        fil.spool_id = spool_id->as_string();
    }

    for (const auto &kv : target.obj_val) {
        out.info[kv.first] = kv.second.as_string();
    }

    return true;
}

bool OpenSpoolCodec::encode(Tag &tag, const Filament &fields, std::string &err) {
    if (fields.material.empty()) {
        err = "material is required";
        return false;
    }
    if (fields.color_hex.empty()) {
        err = "color_hex is required";
        return false;
    }
    std::string color_hex = clean_color(fields.color_hex);
    if (color_hex.empty()) {
        err = "invalid color_hex: " + fields.color_hex;
        return false;
    }

    std::string brand = fields.brand.empty() ? "Generic" : fields.brand;

    int min_t = fields.nozzle_min;
    int max_t = fields.nozzle_max;
    if (min_t == 0 || max_t == 0) {
        if (!openprinttag_data::lookup_material_defaults(fields.material, min_t, max_t)) {
            err = "nozzle temps missing and unknown material: " + fields.material;
            return false;
        }
    }

    json_util::Value data;
    data.type = json_util::Type::Object;
    data.obj_val["protocol"] = json_util::Value{json_util::Type::String, false, 0, 0, false, "openspool", {}, {}};
    data.obj_val["version"] = json_util::Value{json_util::Type::String, false, 0, 0, false, "1.0", {}, {}};
    data.obj_val["type"] = json_util::Value{json_util::Type::String, false, 0, 0, false, fields.material, {}, {}};
    data.obj_val["color_hex"] = json_util::Value{json_util::Type::String, false, 0, 0, false, color_hex, {}, {}};
    data.obj_val["brand"] = json_util::Value{json_util::Type::String, false, 0, 0, false, brand, {}, {}};
    data.obj_val["min_temp"] = json_util::Value{json_util::Type::String, false, 0, 0, false, std::to_string(min_t), {}, {}};
    data.obj_val["max_temp"] = json_util::Value{json_util::Type::String, false, 0, 0, false, std::to_string(max_t), {}, {}};

    auto it_subtype = fields.fields.find("subtype");
    if (it_subtype != fields.fields.end() && !it_subtype->second.empty()) {
        data.obj_val["subtype"] = json_util::Value{json_util::Type::String, false, 0, 0, false, it_subtype->second, {}, {}};
    }

    if (fields.colors.size() > 1) {
        json_util::Value extras;
        extras.type = json_util::Type::Array;
        for (size_t i = 1; i < fields.colors.size(); ++i) {
            std::string cleaned = clean_color(fields.colors[i]);
            if (!cleaned.empty()) {
                extras.arr_val.push_back(json_util::Value{json_util::Type::String, false, 0, 0, false, cleaned, {}, {}});
            }
        }
        if (!extras.arr_val.empty()) {
            data.obj_val["additional_color_hexes"] = extras;
        }
    }

    if (fields.bed_min != 0) {
        data.obj_val["bed_min_temp"] = json_util::Value{json_util::Type::Number, false, static_cast<double>(fields.bed_min), fields.bed_min, true, "", {}, {}};
    }
    if (fields.bed_max != 0) {
        data.obj_val["bed_max_temp"] = json_util::Value{json_util::Type::Number, false, static_cast<double>(fields.bed_max), fields.bed_max, true, "", {}, {}};
    }
    if (fields.weight_g != 0) {
        data.obj_val["weight"] = json_util::Value{json_util::Type::Number, false, static_cast<double>(fields.weight_g), fields.weight_g, true, "", {}, {}};
    }
    if (fields.diameter_mm != 0.0) {
        data.obj_val["diameter"] = json_util::Value{json_util::Type::Number, false, fields.diameter_mm, static_cast<int64_t>(fields.diameter_mm), false, "", {}, {}};
    }
    if (!fields.spool_id.empty()) {
        try {
            int64_t sid = std::stoll(fields.spool_id);
            data.obj_val["spool_id"] = json_util::Value{json_util::Type::Number, false, static_cast<double>(sid), sid, true, "", {}, {}};
        } catch (...) {
            data.obj_val["spool_id"] = json_util::Value{json_util::Type::String, false, 0, 0, false, fields.spool_id, {}, {}};
        }
    }

    std::string json_str = json_util::stringify(data);
    std::vector<uint8_t> payload_bytes(json_str.begin(), json_str.end());
    ndef::NdefRecord rec = ndef::mime_record("application/json", payload_bytes);

    ndef::Tag ntag = to_ndef_tag(tag);
    std::vector<std::pair<size_t, std::vector<uint8_t>>> patches;
    if (!ndef::message_patches(ntag, {rec}, patches, &err)) {
        return false;
    }

    for (const auto &p : patches) {
        if (p.first + p.second.size() > tag.memory.size()) {
            err = "patch exceeds tag memory";
            return false;
        }
        std::memcpy(&tag.memory[p.first], p.second.data(), p.second.size());
        size_t b_start = p.first / tag.block_size;
        size_t b_end = (p.first + p.second.size() - 1) / tag.block_size;
        for (size_t b = b_start; b <= b_end && b < tag.readable.size(); ++b) {
            tag.readable[b] = true;
        }
    }
    return true;
}

// Copyright (C) 2026 JR Lomas <lomas.jr@gmail.com>
// This file may be distributed under the terms of the GNU GPLv3 license.

#include "cbor.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>

namespace cbor {

Tagged::Tagged() : tag(0), value(nullptr) {}

Tagged::Tagged(uint64_t tag, const CborValue& value)
    : tag(tag), value(std::make_shared<CborValue>(value)) {}

bool Tagged::operator==(const Tagged& other) const {
    if (tag != other.tag) {
        return false;
    }
    if (!value && !other.value) {
        return true;
    }
    if (!value || !other.value) {
        return false;
    }
    return *value == *other.value;
}

CborValue::CborValue(int val) : type(Type::Integer) {
    if (val < 0) {
        is_negative = true;
        int_val = static_cast<uint64_t>(-1 - val);
    } else {
        is_negative = false;
        int_val = static_cast<uint64_t>(val);
    }
}

CborValue::CborValue(long val) : type(Type::Integer) {
    if (val < 0) {
        is_negative = true;
        int_val = static_cast<uint64_t>(-1 - val);
    } else {
        is_negative = false;
        int_val = static_cast<uint64_t>(val);
    }
}

CborValue::CborValue(long long val) : type(Type::Integer) {
    if (val < 0) {
        is_negative = true;
        int_val = static_cast<uint64_t>(-1 - val);
    } else {
        is_negative = false;
        int_val = static_cast<uint64_t>(val);
    }
}

CborValue::CborValue(unsigned int val)
    : type(Type::Integer), is_negative(false), int_val(val) {}

CborValue::CborValue(unsigned long val)
    : type(Type::Integer), is_negative(false), int_val(val) {}

CborValue::CborValue(unsigned long long val)
    : type(Type::Integer), is_negative(false), int_val(val) {}

CborValue::CborValue(double val)
    : type(Type::Float), float_val(val) {}

CborValue::CborValue(float val)
    : type(Type::Float), float_val(static_cast<double>(val)) {}

CborValue::CborValue(bool val)
    : type(Type::Simple), simple_type(val ? SimpleType::True : SimpleType::False) {}

CborValue::CborValue(const char* val)
    : type(Type::Text), text_val(val ? val : "") {}

CborValue::CborValue(const std::string& val)
    : type(Type::Text), text_val(val) {}

CborValue::CborValue(std::vector<uint8_t> val)
    : type(Type::Bytes), bytes_val(std::move(val)) {}

CborValue::CborValue(std::vector<CborValue> val)
    : type(Type::Array), array_val(std::move(val)) {}

CborValue::CborValue(std::vector<std::pair<CborValue, CborValue>> val)
    : type(Type::Map), map_val(std::move(val)) {}

CborValue::CborValue(Tagged val)
    : type(Type::Tagged), tagged_val(std::move(val)) {}

CborValue::CborValue(Raw val)
    : type(Type::Raw), raw_val(std::move(val)) {}

CborValue CborValue::from_int(int64_t val) {
    return CborValue(val);
}

CborValue CborValue::from_uint(uint64_t val) {
    CborValue res;
    res.type = Type::Integer;
    res.is_negative = false;
    res.int_val = val;
    return res;
}

CborValue CborValue::from_neg_int(uint64_t val) {
    CborValue res;
    res.type = Type::Integer;
    res.is_negative = true;
    res.int_val = val;
    return res;
}

CborValue CborValue::from_float(double val) {
    return CborValue(val);
}

CborValue CborValue::from_bool(bool val) {
    return CborValue(val);
}

CborValue CborValue::null() {
    CborValue res;
    res.type = Type::Simple;
    res.simple_type = SimpleType::Null;
    return res;
}

CborValue CborValue::undefined() {
    CborValue res;
    res.type = Type::Simple;
    res.simple_type = SimpleType::Undefined;
    return res;
}

CborValue CborValue::simple(uint8_t val) {
    CborValue res;
    res.type = Type::Simple;
    if (val == 20) {
        res.simple_type = SimpleType::False;
    } else if (val == 21) {
        res.simple_type = SimpleType::True;
    } else if (val == 22) {
        res.simple_type = SimpleType::Null;
    } else if (val == 23) {
        res.simple_type = SimpleType::Undefined;
    } else {
        res.simple_type = SimpleType::Other;
        res.simple_val = val;
    }
    return res;
}

CborValue CborValue::from_bytes(std::vector<uint8_t> val) {
    return CborValue(std::move(val));
}

CborValue CborValue::from_text(std::string val) {
    return CborValue(std::move(val));
}

CborValue CborValue::from_array(std::vector<CborValue> val) {
    return CborValue(std::move(val));
}

CborValue CborValue::from_map(std::vector<std::pair<CborValue, CborValue>> val) {
    return CborValue(std::move(val));
}

CborValue CborValue::from_tagged(uint64_t tag, const CborValue& val) {
    return CborValue(Tagged(tag, val));
}

CborValue CborValue::from_raw(std::vector<uint8_t> val) {
    return CborValue(Raw(std::move(val)));
}

const CborValue UNDEFINED = CborValue::undefined();

int64_t CborValue::as_int64() const {
    if (is_negative) {
        return -1 - static_cast<int64_t>(int_val);
    }
    return static_cast<int64_t>(int_val);
}

uint64_t CborValue::as_uint64() const {
    return int_val;
}

const CborValue* CborValue::get(const CborValue& key) const {
    if (type != Type::Map) {
        return nullptr;
    }
    for (const auto& kv : map_val) {
        if (kv.first == key) {
            return &kv.second;
        }
    }
    return nullptr;
}

void CborValue::set(const CborValue& key, const CborValue& val) {
    if (type != Type::Map) {
        type = Type::Map;
        map_val.clear();
    }
    for (auto& kv : map_val) {
        if (kv.first == key) {
            kv.second = val;
            return;
        }
    }
    map_val.push_back({key, val});
}

CborValue& CborValue::operator[](const std::string& key) {
    if (type != Type::Map) {
        type = Type::Map;
        map_val.clear();
    }
    for (auto& kv : map_val) {
        if (kv.first.is_text() && kv.first.text_val == key) {
            return kv.second;
        }
    }
    map_val.push_back({CborValue(key), CborValue::null()});
    return map_val.back().second;
}

const CborValue& CborValue::operator[](const std::string& key) const {
    const CborValue* v = get(key);
    if (!v) {
        static const CborValue null_val;
        return null_val;
    }
    return *v;
}

CborValue& CborValue::operator[](size_t index) {
    if (type != Type::Array) {
        type = Type::Array;
        array_val.clear();
    }
    if (index >= array_val.size()) {
        array_val.resize(index + 1);
    }
    return array_val[index];
}

const CborValue& CborValue::operator[](size_t index) const {
    if (type == Type::Array && index < array_val.size()) {
        return array_val[index];
    }
    static const CborValue null_val;
    return null_val;
}

bool CborValue::operator==(const CborValue& other) const {
    if (type != other.type) {
        return false;
    }
    switch (type) {
        case Type::Integer:
            if (is_negative != other.is_negative) {
                return false;
            }
            return int_val == other.int_val;
        case Type::Float:
            if (std::isnan(float_val) && std::isnan(other.float_val)) {
                return true;
            }
            if (float_val == 0.0 && other.float_val == 0.0) {
                return std::signbit(float_val) == std::signbit(other.float_val);
            }
            return float_val == other.float_val;
        case Type::Bytes:
            return bytes_val == other.bytes_val;
        case Type::Text:
            return text_val == other.text_val;
        case Type::Array:
            return array_val == other.array_val;
        case Type::Map: {
            if (map_val.size() != other.map_val.size()) {
                return false;
            }
            for (const auto& kv : map_val) {
                const CborValue* val2 = other.get(kv.first);
                if (!val2 || !(*val2 == kv.second)) {
                    return false;
                }
            }
            return true;
        }
        case Type::Tagged:
            return tagged_val == other.tagged_val;
        case Type::Simple:
            if (simple_type != other.simple_type) {
                return false;
            }
            if (simple_type == SimpleType::Other) {
                return simple_val == other.simple_val;
            }
            return true;
        case Type::Raw:
            return raw_val == other.raw_val;
    }
    return false;
}

// Internal float encoding / decoding helpers
static inline double half_to_double(uint16_t h) {
    uint32_t sign = (h >> 15) & 0x0001;
    uint32_t exp  = (h >> 10) & 0x001F;
    uint32_t mant = h & 0x03FF;

    if (exp == 0) {
        if (mant == 0) {
            return sign ? -0.0 : 0.0;
        } else {
            double val = std::ldexp(static_cast<double>(mant), -24);
            return sign ? -val : val;
        }
    } else if (exp == 31) {
        if (mant == 0) {
            return sign ? -std::numeric_limits<double>::infinity()
                        : std::numeric_limits<double>::infinity();
        } else {
            return std::numeric_limits<double>::quiet_NaN();
        }
    } else {
        double val = std::ldexp(static_cast<double>(mant | 0x0400), static_cast<int>(exp) - 25);
        return sign ? -val : val;
    }
}

static bool try_encode_half(double v, uint16_t& out_h) {
    if (std::isnan(v)) {
        out_h = 0x7E00;
        return true;
    }
    if (std::isinf(v)) {
        out_h = (v < 0) ? 0xFC00 : 0x7C00;
        return true;
    }
    if (v == 0.0) {
        out_h = std::signbit(v) ? 0x8000 : 0x0000;
        return true;
    }

    uint64_t bits;
    std::memcpy(&bits, &v, sizeof(bits));
    uint32_t sign = static_cast<uint32_t>((bits >> 63) & 1);
    int exp = static_cast<int>((bits >> 52) & 0x7FF) - 1023;
    uint64_t mant = bits & 0xFFFFFFFFFFFFFULL;

    if (exp >= -14 && exp <= 15) {
        if ((mant & 0x3FFFFFFFFFFULL) != 0) {
            return false;
        }
        uint16_t h_mant = static_cast<uint16_t>(mant >> 42);
        uint16_t h_exp = static_cast<uint16_t>(exp + 15);
        out_h = (sign << 15) | (h_exp << 10) | h_mant;
        double check = half_to_double(out_h);
        return (check == v && std::signbit(check) == std::signbit(v));
    } else if (exp >= -24 && exp < -14) {
        uint64_t eff_mant = (1ULL << 52) | mant;
        int shift = 28 - exp;
        uint64_t mask = (1ULL << shift) - 1;
        if ((eff_mant & mask) != 0) {
            return false;
        }
        uint16_t h_mant = static_cast<uint16_t>(eff_mant >> shift);
        if (h_mant >= 1024) {
            return false;
        }
        out_h = (sign << 15) | h_mant;
        double check = half_to_double(out_h);
        return (check == v && std::signbit(check) == std::signbit(v));
    }
    return false;
}

static bool try_encode_single(double v, float& out_f) {
    if (std::isnan(v)) {
        out_f = std::numeric_limits<float>::quiet_NaN();
        return true;
    }
    if (std::isinf(v)) {
        out_f = (v < 0) ? -std::numeric_limits<float>::infinity()
                        : std::numeric_limits<float>::infinity();
        return true;
    }
    float f = static_cast<float>(v);
    if (static_cast<double>(f) == v) {
        if (v == 0.0) {
            if (std::signbit(f) == std::signbit(v)) {
                out_f = f;
                return true;
            }
            return false;
        }
        out_f = f;
        return true;
    }
    return false;
}

static bool is_valid_utf8(const uint8_t* data, size_t len) {
    size_t i = 0;
    while (i < len) {
        uint8_t b = data[i++];
        if (b <= 0x7F) {
            continue;
        } else if ((b & 0xE0) == 0xC0) {
            if (b < 0xC2) return false;
            if (i >= len) return false;
            uint8_t b2 = data[i++];
            if ((b2 & 0xC0) != 0x80) return false;
        } else if ((b & 0xF0) == 0xE0) {
            if (i + 1 >= len) return false;
            uint8_t b2 = data[i++];
            uint8_t b3 = data[i++];
            if ((b2 & 0xC0) != 0x80 || (b3 & 0xC0) != 0x80) return false;
            if (b == 0xE0 && b2 < 0xA0) return false;
            if (b == 0xED && b2 >= 0xA0) return false;
        } else if ((b & 0xF8) == 0xF0) {
            if (i + 2 >= len) return false;
            uint8_t b2 = data[i++];
            uint8_t b3 = data[i++];
            uint8_t b4 = data[i++];
            if ((b2 & 0xC0) != 0x80 || (b3 & 0xC0) != 0x80 || (b4 & 0xC0) != 0x80) return false;
            if (b == 0xF0 && b2 < 0x90) return false;
            if (b == 0xF4 && b2 > 0x8F) return false;
            if (b > 0xF4) return false;
        } else {
            return false;
        }
    }
    return true;
}

static void encode_head(uint8_t major, uint64_t val, std::vector<uint8_t>& out) {
    uint8_t major_bits = static_cast<uint8_t>(major << 5);
    if (val < 24) {
        out.push_back(major_bits | static_cast<uint8_t>(val));
    } else if (val <= 0xFF) {
        out.push_back(major_bits | 24);
        out.push_back(static_cast<uint8_t>(val));
    } else if (val <= 0xFFFF) {
        out.push_back(major_bits | 25);
        out.push_back(static_cast<uint8_t>((val >> 8) & 0xFF));
        out.push_back(static_cast<uint8_t>(val & 0xFF));
    } else if (val <= 0xFFFFFFFF) {
        out.push_back(major_bits | 26);
        out.push_back(static_cast<uint8_t>((val >> 24) & 0xFF));
        out.push_back(static_cast<uint8_t>((val >> 16) & 0xFF));
        out.push_back(static_cast<uint8_t>((val >> 8) & 0xFF));
        out.push_back(static_cast<uint8_t>(val & 0xFF));
    } else {
        out.push_back(major_bits | 27);
        for (int i = 7; i >= 0; --i) {
            out.push_back(static_cast<uint8_t>((val >> (i * 8)) & 0xFF));
        }
    }
}

static void encode_float(double v, std::vector<uint8_t>& out) {
    if (std::isnan(v)) {
        out.push_back(0xf9);
        out.push_back(0x7e);
        out.push_back(0x00);
        return;
    }
    uint16_t h = 0;
    if (try_encode_half(v, h)) {
        out.push_back(0xf9);
        out.push_back(static_cast<uint8_t>((h >> 8) & 0xFF));
        out.push_back(static_cast<uint8_t>(h & 0xFF));
        return;
    }
    float f = 0.0f;
    if (try_encode_single(v, f)) {
        out.push_back(0xfa);
        uint32_t f_bits;
        std::memcpy(&f_bits, &f, sizeof(f_bits));
        out.push_back(static_cast<uint8_t>((f_bits >> 24) & 0xFF));
        out.push_back(static_cast<uint8_t>((f_bits >> 16) & 0xFF));
        out.push_back(static_cast<uint8_t>((f_bits >> 8) & 0xFF));
        out.push_back(static_cast<uint8_t>(f_bits & 0xFF));
        return;
    }
    out.push_back(0xfb);
    uint64_t d_bits;
    std::memcpy(&d_bits, &v, sizeof(d_bits));
    for (int i = 7; i >= 0; --i) {
        out.push_back(static_cast<uint8_t>((d_bits >> (i * 8)) & 0xFF));
    }
}

bool dumps(const CborValue& value, std::vector<uint8_t>& out, bool canonical, bool indefinite, std::string* err) {
    switch (value.type) {
        case Type::Raw: {
            out.insert(out.end(), value.raw_val.data.begin(), value.raw_val.data.end());
            return true;
        }
        case Type::Simple: {
            switch (value.simple_type) {
                case SimpleType::False:
                    out.push_back(0xf4);
                    return true;
                case SimpleType::True:
                    out.push_back(0xf5);
                    return true;
                case SimpleType::Null:
                    out.push_back(0xf6);
                    return true;
                case SimpleType::Undefined:
                    out.push_back(0xf7);
                    return true;
                case SimpleType::Other:
                    encode_head(7, value.simple_val, out);
                    return true;
            }
            return true;
        }
        case Type::Integer: {
            if (!value.is_negative) {
                encode_head(0, value.int_val, out);
            } else {
                encode_head(1, value.int_val, out);
            }
            return true;
        }
        case Type::Float: {
            encode_float(value.float_val, out);
            return true;
        }
        case Type::Bytes: {
            encode_head(2, value.bytes_val.size(), out);
            out.insert(out.end(), value.bytes_val.begin(), value.bytes_val.end());
            return true;
        }
        case Type::Text: {
            encode_head(3, value.text_val.size(), out);
            out.insert(out.end(), value.text_val.begin(), value.text_val.end());
            return true;
        }
        case Type::Array: {
            std::vector<uint8_t> items;
            for (const auto& item : value.array_val) {
                if (!dumps(item, items, canonical, indefinite, err)) {
                    return false;
                }
            }
            if (indefinite) {
                out.push_back(0x9f);
                out.insert(out.end(), items.begin(), items.end());
                out.push_back(0xff);
            } else {
                encode_head(4, value.array_val.size(), out);
                out.insert(out.end(), items.begin(), items.end());
            }
            return true;
        }
        case Type::Map: {
            std::vector<std::pair<std::vector<uint8_t>, std::vector<uint8_t>>> pairs;
            pairs.reserve(value.map_val.size());
            for (const auto& kv : value.map_val) {
                std::vector<uint8_t> ek;
                std::vector<uint8_t> ev;
                if (!dumps(kv.first, ek, canonical, indefinite, err)) {
                    return false;
                }
                if (!dumps(kv.second, ev, canonical, indefinite, err)) {
                    return false;
                }
                pairs.push_back({std::move(ek), std::move(ev)});
            }
            if (canonical) {
                std::sort(pairs.begin(), pairs.end(),
                          [](const std::pair<std::vector<uint8_t>, std::vector<uint8_t>>& a,
                             const std::pair<std::vector<uint8_t>, std::vector<uint8_t>>& b) {
                              if (a.first.size() != b.first.size()) {
                                  return a.first.size() < b.first.size();
                              }
                              return a.first < b.first;
                          });
            }
            std::vector<uint8_t> content;
            for (const auto& p : pairs) {
                content.insert(content.end(), p.first.begin(), p.first.end());
                content.insert(content.end(), p.second.begin(), p.second.end());
            }
            if (indefinite) {
                out.push_back(0xbf);
                out.insert(out.end(), content.begin(), content.end());
                out.push_back(0xff);
            } else {
                encode_head(5, value.map_val.size(), out);
                out.insert(out.end(), content.begin(), content.end());
            }
            return true;
        }
        case Type::Tagged: {
            if (!value.tagged_val.value) {
                if (err) *err = "tagged value is null";
                return false;
            }
            encode_head(6, value.tagged_val.tag, out);
            return dumps(*value.tagged_val.value, out, canonical, indefinite, err);
        }
    }
    if (err) *err = "unknown CBOR value type";
    return false;
}

std::vector<uint8_t> dumps(const CborValue& value, bool canonical, bool indefinite) {
    std::vector<uint8_t> out;
    if (dumps(value, out, canonical, indefinite, nullptr)) {
        return out;
    }
    return {};
}

// Internal decoder implementation
constexpr uint64_t INDEFINITE_LEN = 0xFFFFFFFFFFFFFFFFULL;

struct HeadResult {
    uint8_t major = 0;
    uint64_t val = 0;
    bool is_indefinite = false;
    size_t new_offset = 0;
    CborValue simple_or_float;
    bool has_direct_value = false;
};

static bool decode_head(const uint8_t* data, size_t len, size_t offset, HeadResult& res, std::string* err) {
    if (offset >= len) {
        if (err) *err = "unexpected end of data";
        return false;
    }
    uint8_t b = data[offset++];
    uint8_t major = static_cast<uint8_t>(b >> 5);
    uint8_t info = static_cast<uint8_t>(b & 0x1F);

    res.major = major;
    res.has_direct_value = false;
    res.is_indefinite = false;

    if (major == 7) {
        if (info < 20) {
            res.has_direct_value = true;
            res.simple_or_float = CborValue::simple(info);
            res.new_offset = offset;
            return true;
        } else if (info == 20) {
            res.has_direct_value = true;
            res.simple_or_float = CborValue(false);
            res.new_offset = offset;
            return true;
        } else if (info == 21) {
            res.has_direct_value = true;
            res.simple_or_float = CborValue(true);
            res.new_offset = offset;
            return true;
        } else if (info == 22) {
            res.has_direct_value = true;
            res.simple_or_float = CborValue::null();
            res.new_offset = offset;
            return true;
        } else if (info == 23) {
            res.has_direct_value = true;
            res.simple_or_float = CborValue::undefined();
            res.new_offset = offset;
            return true;
        } else if (info == 24) {
            if (offset >= len) {
                if (err) *err = "truncated simple value";
                return false;
            }
            res.has_direct_value = true;
            res.simple_or_float = CborValue::simple(data[offset++]);
            res.new_offset = offset;
            return true;
        } else if (info == 25) {
            if (offset + 2 > len) {
                if (err) *err = "truncated half float";
                return false;
            }
            uint16_t h = (static_cast<uint16_t>(data[offset]) << 8) | data[offset + 1];
            offset += 2;
            res.has_direct_value = true;
            res.simple_or_float = CborValue(half_to_double(h));
            res.new_offset = offset;
            return true;
        } else if (info == 26) {
            if (offset + 4 > len) {
                if (err) *err = "truncated single float";
                return false;
            }
            uint32_t f_bits = (static_cast<uint32_t>(data[offset]) << 24) |
                              (static_cast<uint32_t>(data[offset + 1]) << 16) |
                              (static_cast<uint32_t>(data[offset + 2]) << 8) |
                              data[offset + 3];
            offset += 4;
            float f;
            std::memcpy(&f, &f_bits, sizeof(f));
            res.has_direct_value = true;
            res.simple_or_float = CborValue(static_cast<double>(f));
            res.new_offset = offset;
            return true;
        } else if (info == 27) {
            if (offset + 8 > len) {
                if (err) *err = "truncated double float";
                return false;
            }
            uint64_t d_bits = 0;
            for (int i = 0; i < 8; ++i) {
                d_bits = (d_bits << 8) | data[offset + i];
            }
            offset += 8;
            double d;
            std::memcpy(&d, &d_bits, sizeof(d));
            res.has_direct_value = true;
            res.simple_or_float = CborValue(d);
            res.new_offset = offset;
            return true;
        } else if (info >= 28 && info <= 30) {
            if (err) *err = "reserved additional info " + std::to_string(info);
            return false;
        }
        res.is_indefinite = true;
        res.new_offset = offset;
        return true;
    }

    if (info < 24) {
        res.val = info;
        res.new_offset = offset;
        return true;
    } else if (info == 24) {
        if (offset >= len) {
            if (err) *err = "truncated additional info 24";
            return false;
        }
        res.val = data[offset++];
        res.new_offset = offset;
        return true;
    } else if (info == 25) {
        if (offset + 2 > len) {
            if (err) *err = "truncated additional info 25";
            return false;
        }
        res.val = (static_cast<uint64_t>(data[offset]) << 8) | data[offset + 1];
        offset += 2;
        res.new_offset = offset;
        return true;
    } else if (info == 26) {
        if (offset + 4 > len) {
            if (err) *err = "truncated additional info 26";
            return false;
        }
        res.val = (static_cast<uint64_t>(data[offset]) << 24) |
                  (static_cast<uint64_t>(data[offset + 1]) << 16) |
                  (static_cast<uint64_t>(data[offset + 2]) << 8) |
                  data[offset + 3];
        offset += 4;
        res.new_offset = offset;
        return true;
    } else if (info == 27) {
        if (offset + 8 > len) {
            if (err) *err = "truncated additional info 27";
            return false;
        }
        uint64_t v = 0;
        for (int i = 0; i < 8; ++i) {
            v = (v << 8) | data[offset + i];
        }
        offset += 8;
        res.val = v;
        res.new_offset = offset;
        return true;
    } else if (info >= 28 && info <= 30) {
        if (err) *err = "reserved additional info " + std::to_string(info);
        return false;
    }

    res.is_indefinite = true;
    res.new_offset = offset;
    return true;
}

static bool decode_item(const uint8_t* data, size_t len, size_t& offset, int depth, CborValue& out, std::string* err);

static bool decode_bytes(const uint8_t* data, size_t len, size_t& offset, bool indefinite, uint64_t val, CborValue& out, std::string* err) {
    if (!indefinite) {
        if (val > len - offset) {
            if (err) *err = "truncated byte string";
            return false;
        }
        out = CborValue::from_bytes(std::vector<uint8_t>(data + offset, data + offset + val));
        offset += val;
        return true;
    }
    std::vector<uint8_t> chunks;
    while (true) {
        if (offset >= len) {
            if (err) *err = "truncated indefinite byte string";
            return false;
        }
        if (data[offset] == 0xFF) {
            offset += 1;
            out = CborValue::from_bytes(std::move(chunks));
            return true;
        }
        HeadResult h;
        if (!decode_head(data, len, offset, h, err)) {
            return false;
        }
        offset = h.new_offset;
        if (h.major != 2 || h.is_indefinite) {
            if (err) *err = "invalid chunk in indefinite byte string";
            return false;
        }
        if (h.val > len - offset) {
            if (err) *err = "truncated chunk in indefinite byte string";
            return false;
        }
        chunks.insert(chunks.end(), data + offset, data + offset + h.val);
        offset += h.val;
    }
}

static bool decode_text(const uint8_t* data, size_t len, size_t& offset, bool indefinite, uint64_t val, CborValue& out, std::string* err) {
    if (!indefinite) {
        if (val > len - offset) {
            if (err) *err = "truncated text string";
            return false;
        }
        if (!is_valid_utf8(data + offset, val)) {
            if (err) *err = "invalid utf-8 text: malformed sequence";
            return false;
        }
        out = CborValue::from_text(std::string(reinterpret_cast<const char*>(data + offset), val));
        offset += val;
        return true;
    }
    std::string result;
    while (true) {
        if (offset >= len) {
            if (err) *err = "truncated indefinite text string";
            return false;
        }
        if (data[offset] == 0xFF) {
            offset += 1;
            out = CborValue::from_text(std::move(result));
            return true;
        }
        HeadResult h;
        if (!decode_head(data, len, offset, h, err)) {
            return false;
        }
        offset = h.new_offset;
        if (h.major != 3 || h.is_indefinite) {
            if (err) *err = "invalid chunk in indefinite text string";
            return false;
        }
        if (h.val > len - offset) {
            if (err) *err = "truncated chunk in indefinite text string";
            return false;
        }
        if (!is_valid_utf8(data + offset, h.val)) {
            if (err) *err = "invalid utf-8 text chunk: malformed sequence";
            return false;
        }
        result.append(reinterpret_cast<const char*>(data + offset), h.val);
        offset += h.val;
    }
}

static uint64_t reserve_cap(uint64_t val, size_t len, size_t offset) {
    // Each item is at least one byte, so the remaining bytes bound the count;
    // never reserve for more than the tag could actually carry.
    uint64_t room = (uint64_t)(len - offset);
    return val < room ? val : room;
}

static bool decode_array(const uint8_t* data, size_t len, size_t& offset, bool indefinite, uint64_t val, int depth, CborValue& out, std::string* err) {
    std::vector<CborValue> res;
    if (!indefinite) {
        res.reserve(reserve_cap(val, len, offset));
        for (uint64_t i = 0; i < val; ++i) {
            CborValue item;
            if (!decode_item(data, len, offset, depth + 1, item, err)) {
                return false;
            }
            res.push_back(std::move(item));
        }
        out = CborValue::from_array(std::move(res));
        return true;
    }
    while (true) {
        if (offset >= len) {
            if (err) *err = "truncated indefinite array";
            return false;
        }
        if (data[offset] == 0xFF) {
            offset += 1;
            out = CborValue::from_array(std::move(res));
            return true;
        }
        CborValue item;
        if (!decode_item(data, len, offset, depth + 1, item, err)) {
            return false;
        }
        res.push_back(std::move(item));
    }
}

static bool decode_map(const uint8_t* data, size_t len, size_t& offset, bool indefinite, uint64_t val, int depth, CborValue& out, std::string* err) {
    std::vector<std::pair<CborValue, CborValue>> res;
    if (!indefinite) {
        res.reserve(reserve_cap(val, len, offset));
        for (uint64_t i = 0; i < val; ++i) {
            CborValue k;
            if (!decode_item(data, len, offset, depth + 1, k, err)) {
                return false;
            }
            if (k.type == Type::Array || k.type == Type::Map) {
                if (err) *err = "unhashable map key";
                return false;
            }
            CborValue v;
            if (!decode_item(data, len, offset, depth + 1, v, err)) {
                return false;
            }
            res.push_back({std::move(k), std::move(v)});
        }
        out = CborValue::from_map(std::move(res));
        return true;
    }
    while (true) {
        if (offset >= len) {
            if (err) *err = "truncated indefinite map";
            return false;
        }
        if (data[offset] == 0xFF) {
            offset += 1;
            out = CborValue::from_map(std::move(res));
            return true;
        }
        CborValue k;
        if (!decode_item(data, len, offset, depth + 1, k, err)) {
            return false;
        }
        if (offset >= len) {
            if (err) *err = "truncated map key-value pair";
            return false;
        }
        if (data[offset] == 0xFF) {
            if (err) *err = "stray break inside map entry";
            return false;
        }
        if (k.type == Type::Array || k.type == Type::Map) {
            if (err) *err = "unhashable map key";
            return false;
        }
        CborValue v;
        if (!decode_item(data, len, offset, depth + 1, v, err)) {
            return false;
        }
        res.push_back({std::move(k), std::move(v)});
    }
}

static bool decode_item(const uint8_t* data, size_t len, size_t& offset, int depth, CborValue& out, std::string* err) {
    if (depth > 4) {   // OpenPrintTag nests 2; cap low, each level is ~0.8 KB of the reader task's 6 KB stack
        if (err) *err = "nesting deeper than 16";
        return false;
    }
    if (offset < len && data[offset] == 0xFF) {
        if (err) *err = "stray break stop code";
        return false;
    }
    HeadResult h;
    if (!decode_head(data, len, offset, h, err)) {
        return false;
    }
    offset = h.new_offset;

    if (h.major == 0) {
        if (h.is_indefinite) {
            if (err) *err = "indefinite length not allowed for integer";
            return false;
        }
        out = CborValue::from_uint(h.val);
        return true;
    }
    if (h.major == 1) {
        if (h.is_indefinite) {
            if (err) *err = "indefinite length not allowed for negative int";
            return false;
        }
        out = CborValue::from_neg_int(h.val);
        return true;
    }
    if (h.major == 2) {
        return decode_bytes(data, len, offset, h.is_indefinite, h.val, out, err);
    }
    if (h.major == 3) {
        return decode_text(data, len, offset, h.is_indefinite, h.val, out, err);
    }
    if (h.major == 4) {
        return decode_array(data, len, offset, h.is_indefinite, h.val, depth, out, err);
    }
    if (h.major == 5) {
        return decode_map(data, len, offset, h.is_indefinite, h.val, depth, out, err);
    }
    if (h.major == 6) {
        if (h.is_indefinite) {
            if (err) *err = "indefinite length not allowed for tag";
            return false;
        }
        CborValue inner;
        if (!decode_item(data, len, offset, depth + 1, inner, err)) {
            return false;
        }
        out = CborValue::from_tagged(h.val, inner);
        return true;
    }
    if (h.major == 7) {
        if (h.is_indefinite) {
            if (err) *err = "stray break stop code";
            return false;
        }
        out = h.simple_or_float;
        return true;
    }

    if (err) *err = "unknown major type " + std::to_string(h.major);
    return false;
}

bool loads(const uint8_t* data, size_t len, CborValue& out, size_t offset, size_t* end_offset, std::string* err) {
    if (!data && len > 0) {
        if (err) *err = "null data pointer";
        return false;
    }
    size_t cur = offset;
    if (!decode_item(data, len, cur, 0, out, err)) {
        return false;
    }
    if (end_offset) {
        *end_offset = cur;
    }
    return true;
}

bool loads(const std::vector<uint8_t>& data, CborValue& out, size_t offset, size_t* end_offset, std::string* err) {
    return loads(data.data(), data.size(), out, offset, end_offset, err);
}

} // namespace cbor

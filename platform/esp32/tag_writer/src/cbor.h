// Copyright (C) 2026 JR Lomas <lomas.jr@gmail.com>
// This file may be distributed under the terms of the GNU GPLv3 license.

#ifndef CBOR_H
#define CBOR_H

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace cbor {

enum class Type {
    Integer,
    Float,
    Bytes,
    Text,
    Array,
    Map,
    Tagged,
    Simple,
    Raw
};

enum class SimpleType {
    False = 20,
    True = 21,
    Null = 22,
    Undefined = 23,
    Other = 24
};

class CborValue;

struct Tagged {
    uint64_t tag = 0;
    std::shared_ptr<CborValue> value;

    Tagged();
    Tagged(uint64_t tag, const CborValue& value);
    bool operator==(const Tagged& other) const;
    bool operator!=(const Tagged& other) const { return !(*this == other); }
};

struct Raw {
    std::vector<uint8_t> data;

    Raw() = default;
    explicit Raw(std::vector<uint8_t> data) : data(std::move(data)) {}
    explicit Raw(const uint8_t* ptr, size_t len) : data(ptr, ptr + len) {}
    bool operator==(const Raw& other) const { return data == other.data; }
    bool operator!=(const Raw& other) const { return !(*this == other); }
};

class CborValue {
public:
    Type type = Type::Simple;

    bool is_negative = false;
    uint64_t int_val = 0;
    double float_val = 0.0;
    std::vector<uint8_t> bytes_val;
    std::string text_val;
    std::vector<CborValue> array_val;
    std::vector<std::pair<CborValue, CborValue>> map_val;
    Tagged tagged_val;
    SimpleType simple_type = SimpleType::Null;
    uint8_t simple_val = 22;
    Raw raw_val;

    CborValue() : type(Type::Simple), simple_type(SimpleType::Null) {}
    CborValue(int val);
    CborValue(long val);
    CborValue(long long val);
    CborValue(unsigned int val);
    CborValue(unsigned long val);
    CborValue(unsigned long long val);
    CborValue(double val);
    CborValue(float val);
    CborValue(bool val);
    CborValue(const char* val);
    CborValue(const std::string& val);
    CborValue(std::vector<uint8_t> val);
    CborValue(std::vector<CborValue> val);
    CborValue(std::vector<std::pair<CborValue, CborValue>> val);
    CborValue(Tagged val);
    CborValue(Raw val);

    static CborValue from_int(int64_t val);
    static CborValue from_uint(uint64_t val);
    static CborValue from_neg_int(uint64_t val); // represents -1 - val
    static CborValue from_float(double val);
    static CborValue from_bool(bool val);
    static CborValue null();
    static CborValue undefined();
    static CborValue simple(uint8_t val);
    static CborValue from_bytes(std::vector<uint8_t> val);
    static CborValue from_text(std::string val);
    static CborValue from_array(std::vector<CborValue> val);
    static CborValue from_map(std::vector<std::pair<CborValue, CborValue>> val);
    static CborValue from_tagged(uint64_t tag, const CborValue& val);
    static CborValue from_raw(std::vector<uint8_t> val);

    bool is_int() const { return type == Type::Integer; }
    bool is_float() const { return type == Type::Float; }
    bool is_bytes() const { return type == Type::Bytes; }
    bool is_text() const { return type == Type::Text; }
    bool is_array() const { return type == Type::Array; }
    bool is_map() const { return type == Type::Map; }
    bool is_tagged() const { return type == Type::Tagged; }
    bool is_bool() const { return type == Type::Simple && (simple_type == SimpleType::False || simple_type == SimpleType::True); }
    bool is_null() const { return type == Type::Simple && simple_type == SimpleType::Null; }
    bool is_undefined() const { return type == Type::Simple && simple_type == SimpleType::Undefined; }
    bool is_raw() const { return type == Type::Raw; }

    int64_t as_int64() const;
    uint64_t as_uint64() const;
    double as_float() const { return float_val; }
    bool as_bool() const { return simple_type == SimpleType::True; }
    const std::vector<uint8_t>& as_bytes() const { return bytes_val; }
    const std::string& as_text() const { return text_val; }
    const std::vector<CborValue>& as_array() const { return array_val; }
    std::vector<CborValue>& as_array() { return array_val; }
    const std::vector<std::pair<CborValue, CborValue>>& as_map() const { return map_val; }
    std::vector<std::pair<CborValue, CborValue>>& as_map() { return map_val; }
    const Tagged& as_tagged() const { return tagged_val; }
    const Raw& as_raw() const { return raw_val; }

    const CborValue* get(const CborValue& key) const;
    const CborValue* get(const char* key) const { return get(CborValue(key)); }
    const CborValue* get(const std::string& key) const { return get(CborValue(key)); }
    const CborValue* get(int64_t key) const { return get(from_int(key)); }

    void set(const CborValue& key, const CborValue& val);
    void set(const char* key, const CborValue& val) { set(CborValue(key), val); }
    void set(const std::string& key, const CborValue& val) { set(CborValue(key), val); }
    void set(int64_t key, const CborValue& val) { set(from_int(key), val); }

    CborValue& operator[](const std::string& key);
    const CborValue& operator[](const std::string& key) const;
    CborValue& operator[](size_t index);
    const CborValue& operator[](size_t index) const;

    bool operator==(const CborValue& other) const;
    bool operator!=(const CborValue& other) const { return !(*this == other); }
};

extern const CborValue UNDEFINED;

bool loads(const uint8_t* data, size_t len, CborValue& out, size_t offset = 0, size_t* end_offset = nullptr, std::string* err = nullptr);
bool loads(const std::vector<uint8_t>& data, CborValue& out, size_t offset = 0, size_t* end_offset = nullptr, std::string* err = nullptr);

bool dumps(const CborValue& value, std::vector<uint8_t>& out, bool canonical = true, bool indefinite = false, std::string* err = nullptr);
std::vector<uint8_t> dumps(const CborValue& value, bool canonical = true, bool indefinite = false);

} // namespace cbor

#endif // CBOR_H

// Copyright (C) 2026 JR Lomas <lomas.jr@gmail.com>
// Based on OpenRFID (GPLv3).
// This file may be distributed under the terms of the GNU GPLv3 license.

#include "creality.h"
#include "aes.h"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <set>
#include <stdexcept>

const uint8_t CrealityCodec::WRITABLE_ACCESS[4] = {0xFF, 0x07, 0x80, 0x69};

static const char * const CREALITY_KEY_HASH =
    "e544d94feb16159bbd7bc227df1e283eca1f38f2bb2015dfcc6161b74473b5c2";
static const char * const CREALITY_DATA_KEY_HASH =
    "acec2106007458579ba522b25610b2cf509ae59d7879cb975f65c45228e5c9a1";

namespace {

static inline uint32_t rotr(uint32_t x, uint32_t n) {
    return (x >> n) | (x << (32 - n));
}

static inline uint32_t ch(uint32_t x, uint32_t y, uint32_t z) {
    return (x & y) ^ (~x & z);
}

static inline uint32_t maj(uint32_t x, uint32_t y, uint32_t z) {
    return (x & y) ^ (x & z) ^ (y & z);
}

static inline uint32_t sigma0(uint32_t x) {
    return rotr(x, 2) ^ rotr(x, 13) ^ rotr(x, 22);
}

static inline uint32_t sigma1(uint32_t x) {
    return rotr(x, 6) ^ rotr(x, 11) ^ rotr(x, 25);
}

static inline uint32_t gamma0(uint32_t x) {
    return rotr(x, 7) ^ rotr(x, 18) ^ (x >> 3);
}

static inline uint32_t gamma1(uint32_t x) {
    return rotr(x, 17) ^ rotr(x, 19) ^ (x >> 10);
}

static const uint32_t K[64] = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
    0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
    0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
    0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
    0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
    0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2
};

static std::vector<uint8_t> sha256(const uint8_t *data, size_t len) {
    uint32_t h[8] = {
        0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
        0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19
    };

    uint64_t total_bits = static_cast<uint64_t>(len) * 8;
    size_t new_len = len + 1;
    while ((new_len % 64) != 56) {
        new_len++;
    }
    std::vector<uint8_t> msg(new_len + 8, 0);
    if (len > 0 && data) {
        std::memcpy(msg.data(), data, len);
    }
    msg[len] = 0x80;
    for (int i = 0; i < 8; ++i) {
        msg[new_len + i] = static_cast<uint8_t>((total_bits >> (56 - i * 8)) & 0xff);
    }

    for (size_t chunk = 0; chunk < msg.size(); chunk += 64) {
        uint32_t w[64];
        for (int i = 0; i < 16; ++i) {
            size_t idx = chunk + i * 4;
            w[i] = (static_cast<uint32_t>(msg[idx]) << 24) |
                   (static_cast<uint32_t>(msg[idx + 1]) << 16) |
                   (static_cast<uint32_t>(msg[idx + 2]) << 8) |
                   (static_cast<uint32_t>(msg[idx + 3]));
        }
        for (int i = 16; i < 64; ++i) {
            w[i] = gamma1(w[i - 2]) + w[i - 7] + gamma0(w[i - 15]) + w[i - 16];
        }

        uint32_t a = h[0], b = h[1], c = h[2], d = h[3];
        uint32_t e = h[4], f = h[5], g = h[6], h_val = h[7];

        for (int i = 0; i < 64; ++i) {
            uint32_t temp1 = h_val + sigma1(e) + ch(e, f, g) + K[i] + w[i];
            uint32_t temp2 = sigma0(a) + maj(a, b, c);
            h_val = g;
            g = f;
            f = e;
            e = d + temp1;
            d = c;
            c = b;
            b = a;
            a = temp1 + temp2;
        }

        h[0] += a;
        h[1] += b;
        h[2] += c;
        h[3] += d;
        h[4] += e;
        h[5] += f;
        h[6] += g;
        h[7] += h_val;
    }

    std::vector<uint8_t> out(32);
    for (int i = 0; i < 8; ++i) {
        out[i * 4 + 0] = static_cast<uint8_t>((h[i] >> 24) & 0xff);
        out[i * 4 + 1] = static_cast<uint8_t>((h[i] >> 16) & 0xff);
        out[i * 4 + 2] = static_cast<uint8_t>((h[i] >> 8) & 0xff);
        out[i * 4 + 3] = static_cast<uint8_t>(h[i] & 0xff);
    }
    return out;
}

static std::string sha256_hex(const uint8_t *data, size_t len) {
    auto digest = sha256(data, len);
    std::string out;
    out.reserve(digest.size() * 2);
    for (uint8_t b : digest) {
        char two[3];
        snprintf(two, sizeof(two), "%02x", b);
        out += two;
    }
    return out;
}

static std::string trim(const std::string &s) {
    size_t start = 0;
    while (start < s.size() && (s[start] == ' ' || s[start] == '\t' || s[start] == '\r' || s[start] == '\n')) {
        start++;
    }
    size_t end = s.size();
    while (end > start && (s[end - 1] == ' ' || s[end - 1] == '\t' || s[end - 1] == '\r' || s[end - 1] == '\n')) {
        end--;
    }
    return s.substr(start, end - start);
}

static std::string to_lower(const std::string &s) {
    std::string out = s;
    for (char &c : out) {
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    }
    return out;
}

static std::string to_upper(const std::string &s) {
    std::string out = s;
    for (char &c : out) {
        if (c >= 'a' && c <= 'z') c = static_cast<char>(c - 'a' + 'A');
    }
    return out;
}

static std::vector<uint8_t> parse_key_bytes(const std::string &s) {
    if (s.size() == 16) {
        return std::vector<uint8_t>(s.begin(), s.end());
    }
    if (s.size() == 32) {
        bool all_hex = true;
        for (char c : s) {
            if (!isxdigit(static_cast<unsigned char>(c))) {
                all_hex = false;
                break;
            }
        }
        if (all_hex) {
            std::vector<uint8_t> out;
            out.reserve(16);
            for (size_t i = 0; i < 32; i += 2) {
                out.push_back(static_cast<uint8_t>(std::stoi(s.substr(i, 2), nullptr, 16)));
            }
            return out;
        }
    }
    return std::vector<uint8_t>(s.begin(), s.end());
}

} // anonymous namespace

namespace creality_data {

static const MaterialInfo MATERIALS_TABLE[] = {
    {"01001", "PLA", "Hyper", nullptr, 190.0, 240.0, 50.0, 50.0, 8.0},
    {"02001", "PLA-CF", "Hyper", nullptr, 190.0, 240.0, 50.0, 50.0, 8.0},
    {"06002", "PETG", "Hyper", nullptr, 220.0, 270.0, 70.0, 60.0, 8.0},
    {"03001", "ABS", "Hyper", nullptr, 240.0, 280.0, 80.0, 80.0, 8.0},
    {"09002", "PLA", "Ender", "Fast", 190.0, 240.0, 50.0, 50.0, 6.0},
    {"04001", "PLA", "CR", nullptr, 190.0, 240.0, 50.0, 50.0, 8.0},
    {"05001", "PLA", "CR", "Silk", 190.0, 240.0, 50.0, 50.0, 8.0},
    {"06001", "PETG", "CR", nullptr, 220.0, 270.0, 70.0, 60.0, 8.0},
    {"07001", "ABS", "CR", nullptr, 240.0, 280.0, 100.0, 80.0, 8.0},
    {"08001", "PLA", "Ender", nullptr, 190.0, 240.0, 50.0, 50.0, 8.0},
    {"09001", "PLA", "EN", "PLA+", 190.0, 240.0, 50.0, 50.0, 8.0},
    {"10001", "TPU", "HP", nullptr, 190.0, 240.0, 40.0, 65.0, 8.0},
    {"11001", "PA", "CR", "Nylon", 250.0, 270.0, 50.0, 80.0, 8.0},
    {"13001", "PLA", "CR", "Carbon", 190.0, 240.0, 50.0, 50.0, 8.0},
    {"14001", "PLA", "CR", "Matte", 190.0, 240.0, 50.0, 50.0, 8.0},
    {"15001", "PLA", "CR", "Fluo", 190.0, 240.0, 50.0, 50.0, 8.0},
    {"16001", "TPU", "CR", nullptr, 210.0, 240.0, 40.0, 65.0, 8.0},
    {"17001", "PLA", "CR", "Wood", 190.0, 240.0, 50.0, 50.0, 8.0},
    {"18001", "PLA", "HP", "Ultra", 190.0, 240.0, 50.0, 50.0, 8.0},
    {"19001", "ASA", "HP", nullptr, 240.0, 280.0, 90.0, 80.0, 8.0},
    {"12003", "PA-CF", "Hyper", "PAHT", 280.0, 320.0, 90.0, 80.0, 10.0},
    {"12002", "PA-CF", "Hyper", "PPA", 280.0, 320.0, 100.0, 100.0, 8.0},
    {"07002", "PC", "Hyper", nullptr, 250.0, 270.0, 110.0, 80.0, 8.0},
    {"01601", "PLA", "Soleyin", "Ultra", 190.0, 240.0, 50.0, 50.0, 8.0},
};

const MaterialInfo *find_material(const std::string &code) {
    for (const auto &item : MATERIALS_TABLE) {
        if (code == item.code) {
            return &item;
        }
    }
    return nullptr;
}

} // namespace creality_data

CrealityCodec::CrealityCodec(const CodecOptions &options, bool validate_key_hash) {
    if (options.has("allow_test_keys") || options.has("skip_key_hash") || options.has("test_keys")) {
        validate_key_hash = false;
    }
    auto it_k = options.values.find("creality_key");
    if (it_k != options.values.end()) {
        std::vector<uint8_t> k = parse_key_bytes(it_k->second);
        if (k.size() != 16) {
            throw std::invalid_argument("creality_key is not the Creality master key");
        }
        if (validate_key_hash) {
            std::string h = sha256_hex(k.data(), k.size());
            if (h != CREALITY_KEY_HASH) {
                throw std::invalid_argument("creality_key is not the Creality master key");
            }
        }
        std::memcpy(master_key_, k.data(), 16);
        has_key_ = true;
    }
    auto it_dk = options.values.find("creality_data_key");
    if (it_dk != options.values.end()) {
        std::vector<uint8_t> dk = parse_key_bytes(it_dk->second);
        if (dk.size() != 16) {
            throw std::invalid_argument("creality_data_key is not the Creality data key");
        }
        if (validate_key_hash) {
            std::string h = sha256_hex(dk.data(), dk.size());
            if (h != CREALITY_DATA_KEY_HASH) {
                throw std::invalid_argument("creality_data_key is not the Creality data key");
            }
        }
        std::memcpy(data_key_, dk.data(), 16);
        has_data_key_ = true;
    }
}

void CrealityCodec::set_master_key(const std::vector<uint8_t> &key) {
    if (key.size() == 16) {
        std::memcpy(master_key_, key.data(), 16);
        has_key_ = true;
    }
}

void CrealityCodec::set_data_key(const std::vector<uint8_t> &key) {
    if (key.size() == 16) {
        std::memcpy(data_key_, key.data(), 16);
        has_data_key_ = true;
    }
}

std::vector<std::string> CrealityCodec::tag_types() const {
    return {TagTypes::MIFARE_CLASSIC_1K};
}

void CrealityCodec::mifare_keys(const uint8_t *uid, size_t uid_len,
                                std::map<int, std::pair<std::vector<uint8_t>,
                                std::vector<uint8_t>>> &out) {
    if (!has_key_ || uid_len != 4 || !uid) {
        return;
    }
    std::vector<uint8_t> uid4(16);
    for (int i = 0; i < 4; ++i) {
        std::memcpy(&uid4[i * 4], uid, 4);
    }
    std::vector<uint8_t> master(master_key_, master_key_ + 16);
    std::vector<uint8_t> enc = aes::ecb_encrypt(master, uid4);
    if (enc.size() < 6) return;
    std::vector<uint8_t> k(enc.begin(), enc.begin() + 6);

    out[1] = {k, k};
    out[2] = {
        {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF},
        {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF}
    };
}

bool CrealityCodec::can_encode(const Tag &tag) {
    if (tag.tag_type != TagType::MIFARE_CLASSIC_1K
            || !has_key_ || !has_data_key_
            || tag.uid.size() != 4
            || tag.sector_keys.find(1) == tag.sector_keys.end()) {
        return false;
    }
    if (tag.readable.size() < 8) {
        return false;
    }
    if (!tag.readable[4] || !tag.readable[5] || !tag.readable[6] || !tag.readable[7]) {
        return false;
    }
    std::vector<uint8_t> b7 = tag.get_block(7);
    if (b7.size() < 9) return false;
    return (b7[6] == WRITABLE_ACCESS[0] &&
            b7[7] == WRITABLE_ACCESS[1] &&
            b7[8] == WRITABLE_ACCESS[2]);
}

std::string CrealityCodec::material_code(const Filament &fields) {
    if (!fields.sku.empty()) {
        if (creality_data::find_material(fields.sku) != nullptr) {
            return fields.sku;
        }
    }
    std::string name = to_lower(trim(fields.name));
    std::string material = to_upper(trim(fields.material));
    if (!name.empty()) {
        for (const auto &item : creality_data::MATERIALS_TABLE) {
            std::string full_name;
            if (item.modifier1 && item.modifier2) {
                full_name = std::string(item.modifier1) + " " + item.modifier2 + " " + item.type;
            } else if (item.modifier1) {
                full_name = std::string(item.modifier1) + " " + item.type;
            } else {
                full_name = item.type;
            }
            std::string reversed;
            if (item.modifier1 && item.modifier2) {
                reversed = std::string(item.type) + " " + item.modifier1 + " " + item.modifier2;
            } else if (item.modifier1) {
                reversed = std::string(item.type) + " " + item.modifier1;
            } else {
                reversed = item.type;
            }
            if (name == to_lower(full_name) || name == to_lower(reversed)) {
                return item.code;
            }
        }
    }
    if (!material.empty()) {
        for (const auto &item : creality_data::MATERIALS_TABLE) {
            if (std::string(item.type) == material) {
                return item.code;
            }
        }
    }
    std::set<std::string> names_set;
    for (const auto &item : creality_data::MATERIALS_TABLE) {
        std::string full_name;
        if (item.modifier1 && item.modifier2) {
            full_name = std::string(item.modifier1) + " " + item.modifier2 + " " + item.type;
        } else if (item.modifier1) {
            full_name = std::string(item.modifier1) + " " + item.type;
        } else {
            full_name = item.type;
        }
        names_set.insert(full_name);
    }
    std::string joined;
    for (const auto &n : names_set) {
        if (!joined.empty()) joined += ", ";
        joined += n;
    }
    throw std::runtime_error("unknown Creality material; use NAME= one of: " + joined);
}

bool CrealityCodec::encode(Tag &tag, const Filament &fields, std::string &err) {
    if (!can_encode(tag)) {
        err = "creality: needs creality_key, creality_data_key and a blank MIFARE Classic 1K tag (or one written by this module)";
        return false;
    }
    std::string code;
    try {
        code = material_code(fields);
    } catch (const std::exception &e) {
        err = e.what();
        return false;
    }

    std::string color = trim(fields.color_hex);
    if (!color.empty() && color[0] == '#') {
        color = color.substr(1);
    }
    color = to_upper(color);
    if (color.size() != 6) {
        err = "color_hex (RRGGBB) is required";
        return false;
    }
    for (char c : color) {
        if (!isxdigit(static_cast<unsigned char>(c))) {
            err = "color_hex (RRGGBB) is required";
            return false;
        }
    }

    int length = 0;
    if (fields.length_m > 0) {
        length = fields.length_m;
    } else if (fields.weight_g > 0) {
        if (fields.weight_g == 1000) length = 330;
        else if (fields.weight_g == 500) length = 165;
        else if (fields.weight_g == 250) length = 80;
        else length = static_cast<int>(std::round(fields.weight_g * 0.33));
    } else {
        length = 330;
    }

    int year = 2026, month = 1, day = 1;
    auto it_m = fields.fields.find("manufactured");
    bool parsed_date = false;
    if (it_m != fields.fields.end() && !it_m->second.empty()) {
        int y = 0, m = 0, d = 0;
        if (std::sscanf(it_m->second.c_str(), "%d-%d-%d", &y, &m, &d) == 3) {
            if (y >= 2000 && m >= 1 && m <= 12 && d >= 1 && d <= 31) {
                year = y;
                month = m;
                day = d;
                parsed_date = true;
            }
        }
    }
    if (!parsed_date) {
        std::time_t t = std::time(nullptr);
        std::tm *tm = std::localtime(&t);
        if (tm) {
            year = 1900 + tm->tm_year;
            month = 1 + tm->tm_mon;
            day = tm->tm_mday;
        }
    }

    char text[49];
    std::snprintf(text, sizeof(text), "%s%02d%X%02d%s%s0%s%04d%s%s",
                  "000", year % 100, month, day, "0276", code.c_str(),
                  color.c_str(), std::min(length, 9999), "000001", "00000000000000");

    std::vector<uint8_t> text_bytes(text, text + 48);
    std::vector<uint8_t> data_key(data_key_, data_key_ + 16);
    std::vector<uint8_t> encrypted_data = aes::ecb_encrypt(data_key, text_bytes);
    if (encrypted_data.size() != 48) {
        err = "creality: encryption failed";
        return false;
    }

    std::vector<uint8_t> uid4(16);
    for (int i = 0; i < 4; ++i) {
        std::memcpy(&uid4[i * 4], tag.uid.data(), 4);
    }
    std::vector<uint8_t> master_key(master_key_, master_key_ + 16);
    std::vector<uint8_t> k_enc = aes::ecb_encrypt(master_key, uid4);
    if (k_enc.size() < 6) {
        err = "creality: key derivation failed";
        return false;
    }

    std::vector<uint8_t> trailer(16);
    std::memcpy(&trailer[0], k_enc.data(), 6);
    std::memcpy(&trailer[6], WRITABLE_ACCESS, 4);
    std::memcpy(&trailer[10], k_enc.data(), 6);

    tag.set_block(4, encrypted_data.data() + 0, 16);
    tag.set_block(5, encrypted_data.data() + 16, 16);
    tag.set_block(6, encrypted_data.data() + 32, 16);
    tag.set_block(7, trailer.data(), 16);
    return true;
}

bool CrealityCodec::decode(const Tag &tag, DecodeResult &out) {
    if (tag.tag_type != TagType::MIFARE_CLASSIC_1K
            || tag.readable.size() < 7) {
        return false;
    }
    if (!tag.readable[4] || !tag.readable[5] || !tag.readable[6]) {
        return false;
    }

    std::vector<uint8_t> d = tag.get_bytes(4 * 16, 48);
    if (d.size() != 48) {
        return false;
    }

    bool is_plaintext = (d[3] == 0x32 && (d[17] == 0x30 || d[17] == 0x23));
    bool encrypted = !is_plaintext;

    if (encrypted) {
        if (!has_data_key_) {
            if (tag.key_source == "creality") {
                out.recognized = true;
                out.format = "creality";
                out.filament = Filament{};
                out.info.clear();
                out.info["error"] = "creality_data_key is not configured";
                return true;
            }
            return false;
        }
        std::vector<uint8_t> dk(data_key_, data_key_ + 16);
        d = aes::ecb_decrypt(dk, d);
        if (d.size() != 48) {
            return false;
        }
    }

    for (uint8_t ch : d) {
        if (ch < 0x20 || ch > 0x7E) {
            return false;
        }
    }
    std::string s(d.begin(), d.end());
    if (s.size() != 48) {
        return false;
    }

    std::string batch = s.substr(0, 3);
    std::string year_str = s.substr(3, 2);
    char month_char = s[5];
    std::string day_str = s.substr(6, 2);
    std::string supplier = s.substr(8, 4);
    std::string mat_code = s.substr(12, 5);
    char prefix = s[17];
    std::string color_hex = to_upper(s.substr(18, 6));
    std::string len_str = s.substr(24, 4);
    std::string serial = s.substr(28, 6);

    if (!isdigit(static_cast<unsigned char>(year_str[0])) ||
        !isdigit(static_cast<unsigned char>(year_str[1]))) {
        return false;
    }
    if (!isdigit(static_cast<unsigned char>(day_str[0])) ||
        !isdigit(static_cast<unsigned char>(day_str[1]))) {
        return false;
    }
    if (!isdigit(static_cast<unsigned char>(len_str[0])) ||
        !isdigit(static_cast<unsigned char>(len_str[1])) ||
        !isdigit(static_cast<unsigned char>(len_str[2])) ||
        !isdigit(static_cast<unsigned char>(len_str[3]))) {
        return false;
    }
    if (prefix != '0' && prefix != '#') {
        return false;
    }
    if (!isxdigit(static_cast<unsigned char>(month_char))) {
        return false;
    }
    int month = std::stoi(std::string(1, month_char), nullptr, 16);
    if (month < 1 || month > 12) {
        return false;
    }
    int day = std::stoi(day_str);
    if (day < 1 || day > 31) {
        return false;
    }
    for (char c : color_hex) {
        if (!isxdigit(static_cast<unsigned char>(c))) {
            return false;
        }
    }

    int year = 2000 + std::stoi(year_str);
    char date_buf[32];
    std::snprintf(date_buf, sizeof(date_buf), "%04d-%02d-%02d", year, month, day);
    std::string manufactured = date_buf;
    int length_m = std::stoi(len_str);

    int weight_g = 0;
    if (length_m == 330) {
        weight_g = 1000;
    } else if (length_m == 165) {
        weight_g = 500;
    } else if (length_m == 80 || length_m == 82) {
        weight_g = 250;
    }

    std::string material;
    std::string name;
    int nozzle_min = 0;
    int nozzle_max = 0;
    int bed_min = 0;
    int bed_max = 0;
    int drying_temp = 0;
    int drying_time_min = 0;

    const auto *mat = creality_data::find_material(mat_code);
    if (mat) {
        material = mat->type;
        if (mat->modifier1 && mat->modifier2) {
            name = std::string(mat->modifier1) + " " + mat->modifier2 + " " + mat->type;
        } else if (mat->modifier1) {
            name = std::string(mat->modifier1) + " " + mat->type;
        } else {
            name = mat->type;
        }
        nozzle_min = static_cast<int>(mat->nozzle_min);
        nozzle_max = static_cast<int>(mat->nozzle_max);
        bed_min = static_cast<int>(mat->bed);
        bed_max = static_cast<int>(mat->bed);
        drying_temp = static_cast<int>(mat->dry_temp);
        drying_time_min = static_cast<int>(mat->dry_hours * 60);
    }

    out.recognized = true;
    out.format = "creality";
    out.filament = Filament{};
    out.filament.brand = "Creality";
    out.filament.material = material;
    out.filament.name = name;
    out.filament.color_hex = color_hex;
    out.filament.colors = { color_hex };
    out.filament.diameter_mm = 1.75;
    out.filament.weight_g = weight_g;
    out.filament.length_m = length_m;
    out.filament.nozzle_min = nozzle_min;
    out.filament.nozzle_max = nozzle_max;
    out.filament.bed_min = bed_min;
    out.filament.bed_max = bed_max;
    out.filament.drying_temp = drying_temp;
    out.filament.drying_time_min = drying_time_min;
    out.filament.sku = mat_code;
    out.filament.fields["manufactured"] = manufactured;

    out.info.clear();
    out.info["batch"] = batch;
    out.info["supplier"] = supplier;
    out.info["serial"] = serial;
    out.info["encrypted"] = encrypted ? "true" : "false";

    if (tag.readable.size() > 10 && tag.readable[8] && tag.readable[9] && tag.readable[10]) {
        std::vector<uint8_t> model_bytes = tag.get_bytes(8 * 16, 48);
        std::string model;
        for (uint8_t b : model_bytes) {
            if (b >= 0x20 && b <= 0x7E) {
                model.push_back(static_cast<char>(b));
            } else if (b == 0) {
                break;
            }
        }
        out.info["printer_model"] = trim(model);
    }
    return true;
}

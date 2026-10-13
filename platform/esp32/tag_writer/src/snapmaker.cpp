// Copyright (C) 2026 JR Lomas <lomas.jr@gmail.com>
// Based on OpenRFID (GPLv3).
// This file may be distributed under the terms of the GNU GPLv3 license.

#include "snapmaker.h"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <stdexcept>

static const char * const SNAPMAKER_KEY_HASH =
    "19aee31a8bcadb0becc08bbaa9197ab403f9c7a2097197d127a2ab7010a7867a";

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

static std::vector<uint8_t> hmac_sha256(const uint8_t *key, size_t key_len,
                                        const uint8_t *data, size_t data_len) {
    uint8_t k[64] = {0};
    if (key_len > 64) {
        auto h = sha256(key, key_len);
        std::memcpy(k, h.data(), 32);
    } else {
        std::memcpy(k, key, key_len);
    }

    uint8_t ipad[64];
    uint8_t opad[64];
    for (int i = 0; i < 64; ++i) {
        ipad[i] = k[i] ^ 0x36;
        opad[i] = k[i] ^ 0x5c;
    }

    std::vector<uint8_t> inner(64 + data_len);
    std::memcpy(inner.data(), ipad, 64);
    if (data_len > 0 && data) {
        std::memcpy(inner.data() + 64, data, data_len);
    }
    auto inner_hash = sha256(inner.data(), inner.size());

    std::vector<uint8_t> outer(64 + 32);
    std::memcpy(outer.data(), opad, 64);
    std::memcpy(outer.data() + 64, inner_hash.data(), 32);
    return sha256(outer.data(), outer.size());
}

static std::vector<uint8_t> parse_key_bytes(const std::string &s) {
    if (s.size() == 64) {
        bool all_hex = true;
        for (char c : s) {
            if (!isxdigit(static_cast<unsigned char>(c))) {
                all_hex = false;
                break;
            }
        }
        if (all_hex) {
            std::vector<uint8_t> out;
            out.reserve(32);
            for (size_t i = 0; i < 64; i += 2) {
                out.push_back(static_cast<uint8_t>(std::stoi(s.substr(i, 2), nullptr, 16)));
            }
            return out;
        }
    }
    return std::vector<uint8_t>(s.begin(), s.end());
}

} // anonymous namespace

namespace snapmaker_data {

const char *find_main_type(uint16_t code) {
    switch (code) {
        case 1: return "PLA";
        case 2: return "PETG";
        case 3: return "ABS";
        case 4: return "TPU";
        case 5: return "PVA";
        default: return nullptr;
    }
}

const char *find_sub_type(uint16_t code) {
    switch (code) {
        case 1: return "Basic";
        case 2: return "Matte";
        case 3: return "SnapSpeed";
        case 4: return "Silk";
        case 5: return "Support";
        case 6: return "HF";
        case 7: return "95A";
        case 8: return "95A HF";
        default: return nullptr;
    }
}

} // namespace snapmaker_data

SnapmakerCodec::SnapmakerCodec(const CodecOptions &options, bool validate_key_hash) {
    if (options.has("allow_test_keys") || options.has("skip_key_hash") || options.has("test_keys")) {
        validate_key_hash = false;
    }
    auto it = options.values.find("snapmaker_key");
    if (it != options.values.end()) {
        std::vector<uint8_t> key = parse_key_bytes(it->second);
        if (validate_key_hash) {
            std::string h = sha256_hex(key.data(), key.size());
            if (h != SNAPMAKER_KEY_HASH) {
                throw std::invalid_argument("snapmaker_key is not the Snapmaker vendor salt");
            }
        }
        salt_ = key;
        has_salt_ = true;
    }
}

void SnapmakerCodec::set_vendor_salt(const std::vector<uint8_t> &salt) {
    salt_ = salt;
    has_salt_ = true;
}

std::vector<std::string> SnapmakerCodec::tag_types() const {
    return {TagTypes::MIFARE_CLASSIC_1K};
}

void SnapmakerCodec::mifare_keys(const uint8_t *uid, size_t uid_len,
                                 std::map<int, std::pair<std::vector<uint8_t>,
                                 std::vector<uint8_t>>> &out) {
    if (!has_salt_ || uid_len < 4 || !uid) {
        return;
    }
    std::vector<uint8_t> ikm(uid, uid + 4);
    std::vector<uint8_t> salt(salt_.begin(), salt_.begin() + std::min(salt_.size(), static_cast<size_t>(25)));
    if (!salt.empty() && salt.back() == 0) {
        salt.pop_back();
    }
    std::vector<uint8_t> prk = hmac_sha256(salt.data(), salt.size(), ikm.data(), ikm.size());

    for (int s = 0; s < 16; ++s) {
        std::string info_str = "key_a_" + std::to_string(s);
        std::vector<uint8_t> info(info_str.begin(), info_str.end());
        info.push_back(0x01);
        std::vector<uint8_t> h = hmac_sha256(prk.data(), prk.size(), info.data(), info.size());
        std::vector<uint8_t> key(h.begin(), h.begin() + 6);
        out[s] = {key, {}};
    }
}

bool SnapmakerCodec::encode(Tag &tag, const Filament &fields, std::string &err) {
    (void)tag;
    (void)fields;
    err = "snapmaker: writing is not supported";
    return false;
}

static std::string read_str(const Tag &tag, size_t offset, size_t length) {
    std::vector<uint8_t> raw = tag.get_bytes(offset, length);
    std::string out;
    for (uint8_t b : raw) {
        if (b == 0) break;
        if (b >= 0x20 && b <= 0x7E) {
            out.push_back(static_cast<char>(b));
        } else {
            out.push_back('?');
        }
    }
    return out;
}

static uint16_t read_u16(const Tag &tag, size_t offset) {
    std::vector<uint8_t> b = tag.get_bytes(offset, 2);
    return static_cast<uint16_t>(b[0]) | (static_cast<uint16_t>(b[1]) << 8);
}

static uint32_t read_u32(const Tag &tag, size_t offset) {
    std::vector<uint8_t> b = tag.get_bytes(offset, 4);
    return static_cast<uint32_t>(b[0]) |
           (static_cast<uint32_t>(b[1]) << 8) |
           (static_cast<uint32_t>(b[2]) << 16) |
           (static_cast<uint32_t>(b[3]) << 24);
}

bool SnapmakerCodec::decode(const Tag &tag, DecodeResult &out) {
    if (tag.tag_type != TagType::MIFARE_CLASSIC_1K
            || tag.key_source != "snapmaker"
            || tag.readable.size() < 12) {
        return false;
    }
    const int required_blocks[] = {0, 1, 2, 4, 5, 6, 8, 9, 10};
    for (int b : required_blocks) {
        if (!tag.readable[b]) {
            return false;
        }
    }

    std::string vendor = read_str(tag, 16, 16);
    std::string manufacturer = read_str(tag, 32, 16);

    uint16_t main_type_code = read_u16(tag, 66);
    uint16_t sub_type_code = read_u16(tag, 68);
    uint16_t tray = read_u16(tag, 70);

    if (tag.memory.size() < 74) {
        return false;
    }
    uint8_t color_nums = tag.memory[72];
    uint8_t raw_alpha = tag.memory[73];
    uint8_t alpha = static_cast<uint8_t>((0xFF - raw_alpha) & 0xFF);

    std::vector<std::string> colors;
    size_t num_colors = std::min(static_cast<size_t>(color_nums), static_cast<size_t>(5));
    for (size_t i = 0; i < num_colors; ++i) {
        std::vector<uint8_t> rgb = tag.get_bytes(80 + i * 3, 3);
        char hex_buf[8];
        std::snprintf(hex_buf, sizeof(hex_buf), "%02X%02X%02X", rgb[0], rgb[1], rgb[2]);
        colors.push_back(hex_buf);
    }
    std::string color_hex = colors.empty() ? "" : colors[0];

    uint32_t sku_val = read_u32(tag, 96);
    std::string sku = (sku_val != 0) ? std::to_string(sku_val) : "";

    uint16_t diameter_raw = read_u16(tag, 128);
    double diameter_mm = (diameter_raw != 0) ? (std::round((diameter_raw / 100.0) * 1000.0) / 1000.0) : 0.0;
    uint16_t weight_raw = read_u16(tag, 130);
    int weight_g = static_cast<int>(weight_raw);
    uint16_t length_raw = read_u16(tag, 132);
    int length_m = static_cast<int>(length_raw);

    uint16_t drying_temp = read_u16(tag, 144);
    uint16_t drying_time_h = read_u16(tag, 146);
    uint16_t hotend_max = read_u16(tag, 148);
    uint16_t hotend_min = read_u16(tag, 150);
    uint16_t bed_type = read_u16(tag, 152);
    uint16_t bed_temp = read_u16(tag, 154);
    uint16_t first_layer = read_u16(tag, 156);
    uint16_t other_layer = read_u16(tag, 158);

    const char *main_type_ptr = snapmaker_data::find_main_type(main_type_code);
    const char *sub_type_ptr = snapmaker_data::find_sub_type(sub_type_code);
    std::string main_type = main_type_ptr ? main_type_ptr : "";
    std::string sub_type = sub_type_ptr ? sub_type_ptr : "";

    std::string name;
    if (!sub_type.empty() && !main_type.empty()) {
        name = sub_type + " " + main_type;
    } else if (!main_type.empty()) {
        name = main_type;
    }

    int nozzle_min = 0;
    int nozzle_max = 0;
    if (first_layer != 0 && other_layer != 0) {
        nozzle_min = std::min(static_cast<int>(first_layer), static_cast<int>(other_layer));
        nozzle_max = std::max(static_cast<int>(first_layer), static_cast<int>(other_layer));
    } else if (first_layer != 0) {
        nozzle_min = nozzle_max = static_cast<int>(first_layer);
    } else if (other_layer != 0) {
        nozzle_min = nozzle_max = static_cast<int>(other_layer);
    }

    out.recognized = true;
    out.format = "snapmaker";
    out.filament = Filament{};
    out.filament.brand = !vendor.empty() ? vendor : "Snapmaker";
    out.filament.material = main_type;
    out.filament.name = name;
    out.filament.color_hex = color_hex;
    out.filament.colors = colors;
    out.filament.diameter_mm = diameter_mm;
    out.filament.weight_g = weight_g;
    out.filament.length_m = length_m;
    out.filament.nozzle_min = nozzle_min;
    out.filament.nozzle_max = nozzle_max;
    out.filament.bed_min = static_cast<int>(bed_temp);
    out.filament.bed_max = static_cast<int>(bed_temp);
    out.filament.drying_temp = static_cast<int>(drying_temp);
    out.filament.drying_time_min = static_cast<int>(drying_time_h * 60);
    out.filament.sku = sku;

    out.info.clear();
    out.info["vendor"] = vendor;
    out.info["manufacturer"] = manufacturer;
    out.info["main_type"] = main_type;
    out.info["sub_type"] = sub_type;
    out.info["tray"] = std::to_string(tray);
    out.info["alpha"] = std::to_string(alpha);
    out.info["bed_type"] = std::to_string(bed_type);
    out.info["first_layer_temp"] = std::to_string(first_layer);
    out.info["other_layer_temp"] = std::to_string(other_layer);
    out.info["hotend_min"] = std::to_string(hotend_min);
    out.info["hotend_max"] = std::to_string(hotend_max);
    out.info["signature_verified"] = "false";
    return true;
}

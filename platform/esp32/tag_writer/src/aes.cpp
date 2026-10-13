// Copyright (C) 2026 JR Lomas <lomas.jr@gmail.com>
// This file may be distributed under the terms of the GNU GPLv3 license.

#include "aes.h"
#include <cstring>

namespace aes {

static const uint8_t S_BOX[256] = {
    0x63, 0x7c, 0x77, 0x7b, 0xf2, 0x6b, 0x6f, 0xc5, 0x30, 0x01, 0x67, 0x2b, 0xfe, 0xd7, 0xab, 0x76,
    0xca, 0x82, 0xc9, 0x7d, 0xfa, 0x59, 0x47, 0xf0, 0xad, 0xd4, 0xa2, 0xaf, 0x9c, 0xa4, 0x72, 0xc0,
    0xb7, 0xfd, 0x93, 0x26, 0x36, 0x3f, 0xf7, 0xcc, 0x34, 0xa5, 0xe5, 0xf1, 0x71, 0xd8, 0x31, 0x15,
    0x04, 0xc7, 0x23, 0xc3, 0x18, 0x96, 0x05, 0x9a, 0x07, 0x12, 0x80, 0xe2, 0xeb, 0x27, 0xb2, 0x75,
    0x09, 0x83, 0x2c, 0x1a, 0x1b, 0x6e, 0x5a, 0xa0, 0x52, 0x3b, 0xd6, 0xb3, 0x29, 0xe3, 0x2f, 0x84,
    0x53, 0xd1, 0x00, 0xed, 0x20, 0xfc, 0xb1, 0x5b, 0x6a, 0xcb, 0xbe, 0x39, 0x4a, 0x4c, 0x58, 0xcf,
    0xd0, 0xef, 0xaa, 0xfb, 0x43, 0x4d, 0x33, 0x85, 0x45, 0xf9, 0x02, 0x7f, 0x50, 0x3c, 0x9f, 0xa8,
    0x51, 0xa3, 0x40, 0x8f, 0x92, 0x9d, 0x38, 0xf5, 0xbc, 0xb6, 0xda, 0x21, 0x10, 0xff, 0xf3, 0xd2,
    0xcd, 0x0c, 0x13, 0xec, 0x5f, 0x97, 0x44, 0x17, 0xc4, 0xa7, 0x7e, 0x3d, 0x64, 0x5d, 0x19, 0x73,
    0x60, 0x81, 0x4f, 0xdc, 0x22, 0x2a, 0x90, 0x88, 0x46, 0xee, 0xb8, 0x14, 0xde, 0x5e, 0x0b, 0xdb,
    0xe0, 0x32, 0x3a, 0x0a, 0x49, 0x06, 0x24, 0x5c, 0xc2, 0xd3, 0xac, 0x62, 0x91, 0x95, 0xe4, 0x79,
    0xe7, 0xc8, 0x37, 0x6d, 0x8d, 0xd5, 0x4e, 0xa9, 0x6c, 0x56, 0xf4, 0xea, 0x65, 0x7a, 0xae, 0x08,
    0xba, 0x78, 0x25, 0x2e, 0x1c, 0xa6, 0xb4, 0xc6, 0xe8, 0xdd, 0x74, 0x1f, 0x4b, 0xbd, 0x8b, 0x8a,
    0x70, 0x3e, 0xb5, 0x66, 0x48, 0x03, 0xf6, 0x0e, 0x61, 0x35, 0x57, 0xb9, 0x86, 0xc1, 0x1d, 0x9e,
    0xe1, 0xf8, 0x98, 0x11, 0x69, 0xd9, 0x8e, 0x94, 0x9b, 0x1e, 0x87, 0xe9, 0xce, 0x55, 0x28, 0xdf,
    0x8c, 0xa1, 0x89, 0x0d, 0xbf, 0xe6, 0x42, 0x68, 0x41, 0x99, 0x2d, 0x0f, 0xb0, 0x54, 0xbb, 0x16
};

static const uint8_t INV_S_BOX[256] = {
    0x52, 0x09, 0x6a, 0xd5, 0x30, 0x36, 0xa5, 0x38, 0xbf, 0x40, 0xa3, 0x9e, 0x81, 0xf3, 0xd7, 0xfb,
    0x7c, 0xe3, 0x39, 0x82, 0x9b, 0x2f, 0xff, 0x87, 0x34, 0x8e, 0x43, 0x44, 0xc4, 0xde, 0xe9, 0xcb,
    0x54, 0x7b, 0x94, 0x32, 0xa6, 0xc2, 0x23, 0x3d, 0xee, 0x4c, 0x95, 0x0b, 0x42, 0xfa, 0xc3, 0x4e,
    0x08, 0x2e, 0xa1, 0x66, 0x28, 0xd9, 0x24, 0xb2, 0x76, 0x5b, 0xa2, 0x49, 0x6d, 0x8b, 0xd1, 0x25,
    0x72, 0xf8, 0xf6, 0x64, 0x86, 0x68, 0x98, 0x16, 0xd4, 0xa4, 0x5c, 0xcc, 0x5d, 0x65, 0xb6, 0x92,
    0x6c, 0x70, 0x48, 0x50, 0xfd, 0xed, 0xb9, 0xda, 0x5e, 0x15, 0x46, 0x57, 0xa7, 0x8d, 0x9d, 0x84,
    0x90, 0xd8, 0xab, 0x00, 0x8c, 0xbc, 0xd3, 0x0a, 0xf7, 0xe4, 0x58, 0x05, 0xb8, 0xb3, 0x45, 0x06,
    0xd0, 0x2c, 0x1e, 0x8f, 0xca, 0x3f, 0x0f, 0x02, 0xc1, 0xaf, 0xbd, 0x03, 0x01, 0x13, 0x8a, 0x6b,
    0x3a, 0x91, 0x11, 0x41, 0x4f, 0x67, 0xdc, 0xea, 0x97, 0xf2, 0xcf, 0xce, 0xf0, 0xb4, 0xe6, 0x73,
    0x96, 0xac, 0x74, 0x22, 0xe7, 0xad, 0x35, 0x85, 0xe2, 0xf9, 0x37, 0xe8, 0x1c, 0x75, 0xdf, 0x6e,
    0x47, 0xf1, 0x1a, 0x71, 0x1d, 0x29, 0xc5, 0x89, 0x6f, 0xb7, 0x62, 0x0e, 0xaa, 0x18, 0xbe, 0x1b,
    0xfc, 0x56, 0x3e, 0x4b, 0xc6, 0xd2, 0x79, 0x20, 0x9a, 0xdb, 0xc0, 0xfe, 0x78, 0xcd, 0x5a, 0xf4,
    0x1f, 0xdd, 0xa8, 0x33, 0x88, 0x07, 0xc7, 0x31, 0xb1, 0x12, 0x10, 0x59, 0x27, 0x80, 0xec, 0x5f,
    0x60, 0x51, 0x7f, 0xa9, 0x19, 0xb5, 0x4a, 0x0d, 0x2d, 0xe5, 0x7a, 0x9f, 0x93, 0xc9, 0x9c, 0xef,
    0xa0, 0xe0, 0x3b, 0x4d, 0xae, 0x2a, 0xf5, 0xb0, 0xc8, 0xeb, 0xbb, 0x3c, 0x83, 0x53, 0x99, 0x61,
    0x17, 0x2b, 0x04, 0x7e, 0xba, 0x77, 0xd6, 0x26, 0xe1, 0x69, 0x14, 0x63, 0x55, 0x21, 0x0c, 0x7d
};

static const uint8_t RCON[10] = {
    0x01, 0x02, 0x04, 0x08, 0x10, 0x20, 0x40, 0x80, 0x1b, 0x36
};

static inline uint8_t xtime(uint8_t a) {
    return (a & 0x80) ? static_cast<uint8_t>(((a << 1) ^ 0x1b) & 0xff)
                      : static_cast<uint8_t>((a << 1) & 0xff);
}

static inline uint8_t mul(uint8_t a, uint8_t b) {
    uint8_t res = 0;
    while (b) {
        if (b & 1) {
            res ^= a;
        }
        a = xtime(a);
        b >>= 1;
    }
    return res;
}

static void key_expansion(const uint8_t key16[16], uint8_t w[176]) {
    std::memcpy(w, key16, 16);
    for (int i = 4; i < 44; ++i) {
        int k = (i - 1) * 4;
        uint8_t b0 = w[k];
        uint8_t b1 = w[k + 1];
        uint8_t b2 = w[k + 2];
        uint8_t b3 = w[k + 3];
        if (i % 4 == 0) {
            uint8_t tmp0 = S_BOX[b1] ^ RCON[(i / 4) - 1];
            uint8_t tmp1 = S_BOX[b2];
            uint8_t tmp2 = S_BOX[b3];
            uint8_t tmp3 = S_BOX[b0];
            b0 = tmp0;
            b1 = tmp1;
            b2 = tmp2;
            b3 = tmp3;
        }
        int j = (i - 4) * 4;
        w[i * 4 + 0] = w[j + 0] ^ b0;
        w[i * 4 + 1] = w[j + 1] ^ b1;
        w[i * 4 + 2] = w[j + 2] ^ b2;
        w[i * 4 + 3] = w[j + 3] ^ b3;
    }
}

bool encrypt_block(const uint8_t* key16, const uint8_t* block16, uint8_t* out16) {
    if (!key16 || !block16 || !out16) {
        return false;
    }
    uint8_t w[176];
    key_expansion(key16, w);

    uint8_t s[16];
    for (int i = 0; i < 16; ++i) {
        s[i] = block16[i] ^ w[i];
    }

    for (int rnd = 1; rnd <= 9; ++rnd) {
        for (int i = 0; i < 16; ++i) {
            s[i] = S_BOX[s[i]];
        }
        // ShiftRows:
        // s[1], s[5], s[9], s[13] = s[5], s[9], s[13], s[1]
        uint8_t t1 = s[1];
        s[1] = s[5];
        s[5] = s[9];
        s[9] = s[13];
        s[13] = t1;

        // s[2], s[6], s[10], s[14] = s[10], s[14], s[2], s[6]
        uint8_t t2 = s[2];
        uint8_t t6 = s[6];
        s[2] = s[10];
        s[6] = s[14];
        s[10] = t2;
        s[14] = t6;

        // s[3], s[7], s[11], s[15] = s[15], s[3], s[7], s[11]
        uint8_t t15 = s[15];
        s[15] = s[11];
        s[11] = s[7];
        s[7] = s[3];
        s[3] = t15;

        // MixColumns
        for (int c = 0; c < 4; ++c) {
            int idx = c * 4;
            uint8_t s0 = s[idx], s1 = s[idx + 1], s2 = s[idx + 2], s3 = s[idx + 3];
            s[idx]     = xtime(s0) ^ xtime(s1) ^ s1 ^ s2 ^ s3;
            s[idx + 1] = s0 ^ xtime(s1) ^ xtime(s2) ^ s2 ^ s3;
            s[idx + 2] = s0 ^ s1 ^ xtime(s2) ^ xtime(s3) ^ s3;
            s[idx + 3] = xtime(s0) ^ s0 ^ s1 ^ s2 ^ xtime(s3);
        }

        int rw = rnd * 16;
        for (int i = 0; i < 16; ++i) {
            s[i] ^= w[rw + i];
        }
    }

    // Round 10
    for (int i = 0; i < 16; ++i) {
        s[i] = S_BOX[s[i]];
    }
    uint8_t t1 = s[1];
    s[1] = s[5];
    s[5] = s[9];
    s[9] = s[13];
    s[13] = t1;

    uint8_t t2 = s[2];
    uint8_t t6 = s[6];
    s[2] = s[10];
    s[6] = s[14];
    s[10] = t2;
    s[14] = t6;

    uint8_t t15 = s[15];
    s[15] = s[11];
    s[11] = s[7];
    s[7] = s[3];
    s[3] = t15;

    for (int i = 0; i < 16; ++i) {
        out16[i] = s[i] ^ w[160 + i];
    }
    return true;
}

bool decrypt_block(const uint8_t* key16, const uint8_t* block16, uint8_t* out16) {
    if (!key16 || !block16 || !out16) {
        return false;
    }
    uint8_t w[176];
    key_expansion(key16, w);

    uint8_t s[16];
    for (int i = 0; i < 16; ++i) {
        s[i] = block16[i] ^ w[160 + i];
    }

    for (int rnd = 9; rnd >= 1; --rnd) {
        // InvShiftRows:
        // s[5], s[9], s[13], s[1] = s[1], s[5], s[9], s[13]
        uint8_t t13 = s[13];
        s[13] = s[9];
        s[9] = s[5];
        s[5] = s[1];
        s[1] = t13;

        // s[10], s[14], s[2], s[6] = s[2], s[6], s[10], s[14]
        uint8_t t10 = s[10];
        uint8_t t14 = s[14];
        s[10] = s[2];
        s[14] = s[6];
        s[2] = t10;
        s[6] = t14;

        // s[15], s[3], s[7], s[11] = s[3], s[7], s[11], s[15]
        uint8_t t3 = s[3];
        s[3] = s[7];
        s[7] = s[11];
        s[11] = s[15];
        s[15] = t3;

        for (int i = 0; i < 16; ++i) {
            s[i] = INV_S_BOX[s[i]];
        }

        int rw = rnd * 16;
        for (int i = 0; i < 16; ++i) {
            s[i] ^= w[rw + i];
        }

        // InvMixColumns
        for (int c = 0; c < 4; ++c) {
            int idx = c * 4;
            uint8_t s0 = s[idx], s1 = s[idx + 1], s2 = s[idx + 2], s3 = s[idx + 3];
            s[idx]     = mul(s0, 0x0e) ^ mul(s1, 0x0b) ^ mul(s2, 0x0d) ^ mul(s3, 0x09);
            s[idx + 1] = mul(s0, 0x09) ^ mul(s1, 0x0e) ^ mul(s2, 0x0b) ^ mul(s3, 0x0d);
            s[idx + 2] = mul(s0, 0x0d) ^ mul(s1, 0x09) ^ mul(s2, 0x0e) ^ mul(s3, 0x0b);
            s[idx + 3] = mul(s0, 0x0b) ^ mul(s1, 0x0d) ^ mul(s2, 0x09) ^ mul(s3, 0x0e);
        }
    }

    // Round 0
    uint8_t t13 = s[13];
    s[13] = s[9];
    s[9] = s[5];
    s[5] = s[1];
    s[1] = t13;

    uint8_t t10 = s[10];
    uint8_t t14 = s[14];
    s[10] = s[2];
    s[14] = s[6];
    s[2] = t10;
    s[6] = t14;

    uint8_t t3 = s[3];
    s[3] = s[7];
    s[7] = s[11];
    s[11] = s[15];
    s[15] = t3;

    for (int i = 0; i < 16; ++i) {
        s[i] = INV_S_BOX[s[i]];
    }

    for (int i = 0; i < 16; ++i) {
        out16[i] = s[i] ^ w[i];
    }
    return true;
}

bool encrypt_block(const std::vector<uint8_t>& key16,
                   const std::vector<uint8_t>& block16,
                   std::vector<uint8_t>& out16) {
    if (key16.size() != 16 || block16.size() != 16) {
        return false;
    }
    out16.resize(16);
    return encrypt_block(key16.data(), block16.data(), out16.data());
}

bool decrypt_block(const std::vector<uint8_t>& key16,
                   const std::vector<uint8_t>& block16,
                   std::vector<uint8_t>& out16) {
    if (key16.size() != 16 || block16.size() != 16) {
        return false;
    }
    out16.resize(16);
    return decrypt_block(key16.data(), block16.data(), out16.data());
}

std::vector<uint8_t> encrypt_block(const std::vector<uint8_t>& key16,
                                   const std::vector<uint8_t>& block16) {
    std::vector<uint8_t> out;
    if (encrypt_block(key16, block16, out)) {
        return out;
    }
    return {};
}

std::vector<uint8_t> decrypt_block(const std::vector<uint8_t>& key16,
                                   const std::vector<uint8_t>& block16) {
    std::vector<uint8_t> out;
    if (decrypt_block(key16, block16, out)) {
        return out;
    }
    return {};
}

bool ecb_encrypt(const std::vector<uint8_t>& key16,
                 const std::vector<uint8_t>& data,
                 std::vector<uint8_t>& out) {
    if (key16.size() != 16 || (data.size() % 16) != 0) {
        return false;
    }
    out.resize(data.size());
    for (size_t offset = 0; offset < data.size(); offset += 16) {
        if (!encrypt_block(key16.data(), data.data() + offset, out.data() + offset)) {
            return false;
        }
    }
    return true;
}

bool ecb_decrypt(const std::vector<uint8_t>& key16,
                 const std::vector<uint8_t>& data,
                 std::vector<uint8_t>& out) {
    if (key16.size() != 16 || (data.size() % 16) != 0) {
        return false;
    }
    out.resize(data.size());
    for (size_t offset = 0; offset < data.size(); offset += 16) {
        if (!decrypt_block(key16.data(), data.data() + offset, out.data() + offset)) {
            return false;
        }
    }
    return true;
}

std::vector<uint8_t> ecb_encrypt(const std::vector<uint8_t>& key16,
                                 const std::vector<uint8_t>& data) {
    std::vector<uint8_t> out;
    if (ecb_encrypt(key16, data, out)) {
        return out;
    }
    return {};
}

std::vector<uint8_t> ecb_decrypt(const std::vector<uint8_t>& key16,
                                 const std::vector<uint8_t>& data) {
    std::vector<uint8_t> out;
    if (ecb_decrypt(key16, data, out)) {
        return out;
    }
    return {};
}

bool cbc_encrypt(const std::vector<uint8_t>& key16,
                 const std::vector<uint8_t>& iv16,
                 const std::vector<uint8_t>& data,
                 std::vector<uint8_t>& out) {
    if (key16.size() != 16 || iv16.size() != 16 || (data.size() % 16) != 0) {
        return false;
    }
    out.resize(data.size());
    uint8_t prev[16];
    std::memcpy(prev, iv16.data(), 16);
    uint8_t block[16];
    for (size_t offset = 0; offset < data.size(); offset += 16) {
        for (int i = 0; i < 16; ++i) {
            block[i] = data[offset + i] ^ prev[i];
        }
        if (!encrypt_block(key16.data(), block, out.data() + offset)) {
            return false;
        }
        std::memcpy(prev, out.data() + offset, 16);
    }
    return true;
}

bool cbc_decrypt(const std::vector<uint8_t>& key16,
                 const std::vector<uint8_t>& iv16,
                 const std::vector<uint8_t>& data,
                 std::vector<uint8_t>& out) {
    if (key16.size() != 16 || iv16.size() != 16 || (data.size() % 16) != 0) {
        return false;
    }
    out.resize(data.size());
    uint8_t prev[16];
    std::memcpy(prev, iv16.data(), 16);
    uint8_t block[16];
    for (size_t offset = 0; offset < data.size(); offset += 16) {
        if (!decrypt_block(key16.data(), data.data() + offset, block)) {
            return false;
        }
        for (int i = 0; i < 16; ++i) {
            out[offset + i] = block[i] ^ prev[i];
        }
        std::memcpy(prev, data.data() + offset, 16);
    }
    return true;
}

std::vector<uint8_t> cbc_encrypt(const std::vector<uint8_t>& key16,
                                 const std::vector<uint8_t>& iv16,
                                 const std::vector<uint8_t>& data) {
    std::vector<uint8_t> out;
    if (cbc_encrypt(key16, iv16, data, out)) {
        return out;
    }
    return {};
}

std::vector<uint8_t> cbc_decrypt(const std::vector<uint8_t>& key16,
                                 const std::vector<uint8_t>& iv16,
                                 const std::vector<uint8_t>& data) {
    std::vector<uint8_t> out;
    if (cbc_decrypt(key16, iv16, data, out)) {
        return out;
    }
    return {};
}

} // namespace aes

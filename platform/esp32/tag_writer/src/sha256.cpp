// Copyright (C) 2026 JR Lomas <lomas.jr@gmail.com>
// This file may be distributed under the terms of the GNU GPLv3 license.

#include "sha256.h"

#include <string.h>

#include "progmem_compat.h"

static const uint32_t K[64] PROGMEM = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1,
    0x923f82a4, 0xab1c5ed5, 0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3,
    0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174, 0xe49b69c1, 0xefbe4786,
    0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147,
    0x06ca6351, 0x14292967, 0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13,
    0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85, 0xa2bfe8a1, 0xa81a664b,
    0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a,
    0x5b9cca4f, 0x682e6ff3, 0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208,
    0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2,
};

static inline uint32_t rotr(uint32_t x, uint8_t n) {
    return (x >> n) | (x << (32 - n));
}

void Sha256::begin() {
    static const uint32_t initial[8] = {
        0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
        0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19,
    };
    memcpy(state_, initial, sizeof(state_));
    bits_low_ = bits_high_ = 0;
    used_ = 0;
}

void Sha256::compress() {
    // 16-word rolling message schedule keeps the AVR stack small.
    uint32_t w[16];
    for (uint8_t i = 0; i < 16; i++) {
        w[i] = ((uint32_t)block_[i * 4] << 24) |
               ((uint32_t)block_[i * 4 + 1] << 16) |
               ((uint32_t)block_[i * 4 + 2] << 8) | block_[i * 4 + 3];
    }
    uint32_t a = state_[0], b = state_[1], c = state_[2], d = state_[3];
    uint32_t e = state_[4], f = state_[5], g = state_[6], h = state_[7];
    for (uint8_t i = 0; i < 64; i++) {
        uint32_t wi;
        if (i < 16) {
            wi = w[i];
        } else {
            uint32_t w15 = w[(i + 1) & 15], w2 = w[(i + 14) & 15];
            uint32_t s0 = rotr(w15, 7) ^ rotr(w15, 18) ^ (w15 >> 3);
            uint32_t s1 = rotr(w2, 17) ^ rotr(w2, 19) ^ (w2 >> 10);
            wi = w[i & 15] += s0 + w[(i + 9) & 15] + s1;
        }
        uint32_t s1 = rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25);
        uint32_t ch = (e & f) ^ (~e & g);
        uint32_t t1 = h + s1 + ch + pgm_read_dword(&K[i]) + wi;
        uint32_t s0 = rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22);
        uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
        uint32_t t2 = s0 + maj;
        h = g; g = f; f = e; e = d + t1;
        d = c; c = b; b = a; a = t1 + t2;
    }
    state_[0] += a; state_[1] += b; state_[2] += c; state_[3] += d;
    state_[4] += e; state_[5] += f; state_[6] += g; state_[7] += h;
}

void Sha256::update(const uint8_t *data, size_t length) {
    while (length--) {
        block_[used_++] = *data++;
        if ((bits_low_ += 8) < 8)
            bits_high_++;
        if (used_ == 64) {
            compress();
            used_ = 0;
        }
    }
}

void Sha256::finish(uint8_t digest[32]) {
    uint32_t low = bits_low_, high = bits_high_;
    uint8_t pad = 0x80;
    update(&pad, 1);
    pad = 0;
    while (used_ != 56)
        update(&pad, 1);
    uint8_t length[8] = {
        (uint8_t)(high >> 24), (uint8_t)(high >> 16), (uint8_t)(high >> 8),
        (uint8_t)high, (uint8_t)(low >> 24), (uint8_t)(low >> 16),
        (uint8_t)(low >> 8), (uint8_t)low,
    };
    update(length, 8);
    for (uint8_t i = 0; i < 8; i++) {
        digest[i * 4] = state_[i] >> 24;
        digest[i * 4 + 1] = state_[i] >> 16;
        digest[i * 4 + 2] = state_[i] >> 8;
        digest[i * 4 + 3] = state_[i];
    }
}

void HmacSha256::begin(const uint8_t *key, size_t length) {
    memset(key_, 0, sizeof(key_));
    if (length > sizeof(key_)) {
        sha_.begin();
        sha_.update(key, length);
        sha_.finish(key_);
    } else {
        memcpy(key_, key, length);
    }
    uint8_t pad[64];
    for (uint8_t i = 0; i < 64; i++)
        pad[i] = key_[i] ^ 0x36;
    sha_.begin();
    sha_.update(pad, sizeof(pad));
}

void HmacSha256::finish(uint8_t mac[32]) {
    uint8_t inner[32];
    sha_.finish(inner);
    uint8_t pad[64];
    for (uint8_t i = 0; i < 64; i++)
        pad[i] = key_[i] ^ 0x5c;
    sha_.begin();
    sha_.update(pad, sizeof(pad));
    sha_.update(inner, sizeof(inner));
    sha_.finish(mac);
}

void hkdf_sha256(const uint8_t *salt, size_t salt_length,
                 const uint8_t *ikm, size_t ikm_length,
                 const uint8_t *info, size_t info_length,
                 uint8_t *out, size_t out_length) {
    HmacSha256 hmac;
    uint8_t prk[32];
    hmac.begin(salt, salt_length);
    hmac.update(ikm, ikm_length);
    hmac.finish(prk);

    uint8_t previous[32];
    uint8_t previous_length = 0;
    uint8_t counter = 0;
    while (out_length) {
        counter++;
        hmac.begin(prk, sizeof(prk));
        hmac.update(previous, previous_length);
        hmac.update(info, info_length);
        hmac.update(&counter, 1);
        hmac.finish(previous);
        previous_length = sizeof(previous);
        size_t take = out_length < sizeof(previous) ? out_length : sizeof(previous);
        memcpy(out, previous, take);
        out += take;
        out_length -= take;
    }
}

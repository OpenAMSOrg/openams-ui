// Copyright (C) 2026 JR Lomas <lomas.jr@gmail.com>
// This file may be distributed under the terms of the GNU GPLv3 license.

// SHA-256, HMAC-SHA256 and HKDF-SHA256 (RFC 6234 / RFC 2104 / RFC 5869).
// Portable: builds for the AVR target and for the host-side tests.
#pragma once

#include <stddef.h>
#include <stdint.h>

class Sha256 {
public:
    void begin();
    void update(const uint8_t *data, size_t length);
    void finish(uint8_t digest[32]);

private:
    void compress();

    uint32_t state_[8];
    uint32_t bits_low_;
    uint32_t bits_high_;
    uint8_t block_[64];
    uint8_t used_;
};

class HmacSha256 {
public:
    void begin(const uint8_t *key, size_t length);
    void update(const uint8_t *data, size_t length) { sha_.update(data, length); }
    void finish(uint8_t mac[32]);

private:
    Sha256 sha_;
    uint8_t key_[64];
};

// Writes `out_length` bytes of output keying material (at most 255 * 32).
void hkdf_sha256(const uint8_t *salt, size_t salt_length,
                 const uint8_t *ikm, size_t ikm_length,
                 const uint8_t *info, size_t info_length,
                 uint8_t *out, size_t out_length);

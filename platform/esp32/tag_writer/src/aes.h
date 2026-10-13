// Copyright (C) 2026 JR Lomas <lomas.jr@gmail.com>
// This file may be distributed under the terms of the GNU GPLv3 license.

#ifndef AES_H
#define AES_H

#include <cstddef>
#include <cstdint>
#include <vector>

namespace aes {

constexpr size_t BLOCK_SIZE = 16;
constexpr size_t KEY_SIZE = 16;

// Low-level block operations on 16-byte buffers
bool encrypt_block(const uint8_t* key16, const uint8_t* block16, uint8_t* out16);
bool decrypt_block(const uint8_t* key16, const uint8_t* block16, uint8_t* out16);

// Vector block operations
bool encrypt_block(const std::vector<uint8_t>& key16,
                   const std::vector<uint8_t>& block16,
                   std::vector<uint8_t>& out16);
bool decrypt_block(const std::vector<uint8_t>& key16,
                   const std::vector<uint8_t>& block16,
                   std::vector<uint8_t>& out16);

std::vector<uint8_t> encrypt_block(const std::vector<uint8_t>& key16,
                                   const std::vector<uint8_t>& block16);
std::vector<uint8_t> decrypt_block(const std::vector<uint8_t>& key16,
                                   const std::vector<uint8_t>& block16);

// ECB mode encryption/decryption (data size must be a multiple of 16)
bool ecb_encrypt(const std::vector<uint8_t>& key16,
                 const std::vector<uint8_t>& data,
                 std::vector<uint8_t>& out);
bool ecb_decrypt(const std::vector<uint8_t>& key16,
                 const std::vector<uint8_t>& data,
                 std::vector<uint8_t>& out);

std::vector<uint8_t> ecb_encrypt(const std::vector<uint8_t>& key16,
                                 const std::vector<uint8_t>& data);
std::vector<uint8_t> ecb_decrypt(const std::vector<uint8_t>& key16,
                                 const std::vector<uint8_t>& data);

// CBC mode encryption/decryption with IV (IV must be 16 bytes, data multiple of 16)
bool cbc_encrypt(const std::vector<uint8_t>& key16,
                 const std::vector<uint8_t>& iv16,
                 const std::vector<uint8_t>& data,
                 std::vector<uint8_t>& out);
bool cbc_decrypt(const std::vector<uint8_t>& key16,
                 const std::vector<uint8_t>& iv16,
                 const std::vector<uint8_t>& data,
                 std::vector<uint8_t>& out);

std::vector<uint8_t> cbc_encrypt(const std::vector<uint8_t>& key16,
                                 const std::vector<uint8_t>& iv16,
                                 const std::vector<uint8_t>& data);
std::vector<uint8_t> cbc_decrypt(const std::vector<uint8_t>& key16,
                                 const std::vector<uint8_t>& iv16,
                                 const std::vector<uint8_t>& data);

} // namespace aes

#endif // AES_H

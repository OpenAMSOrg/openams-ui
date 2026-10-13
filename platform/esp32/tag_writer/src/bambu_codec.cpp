// Copyright (C) 2026 JR Lomas <lomas.jr@gmail.com>
// Based on openams (GPLv3) and OpenRFID (GPLv3).
// This file may be distributed under the terms of the GNU GPLv3 license.

#include "bambu_codec.h"
#include "bambu_view.h"

#include <stdio.h>
#include <string.h>

#include "sha256.h"

// SHA-256 of the genuine Bambu Lab master key (public one-way hash; NOT the
// key). The codec stays inert unless the configured key matches, so a wrong
// or placeholder key never derives bogus sector keys.
static const char *KEY_SHA256 =
    "19cc3c63cb8802668800c3b3bf3fee05b3c59bf59fc5fd256b68e868084ec304";

static bool key_hash_matches(const uint8_t *key, size_t len) {
    if (len != bambu::MASTER_KEY_SIZE)
        return false;
    uint8_t digest[32];
    Sha256 sha;
    sha.begin();
    sha.update(key, len);
    sha.finish(digest);
    char hex[65];
    for (int i = 0; i < 32; i++)
        snprintf(hex + i * 2, 3, "%02x", digest[i]);
    return strcmp(hex, KEY_SHA256) == 0;
}

BambuCodec::BambuCodec(const CodecOptions &options) {
    const std::string &key = options.get("bambu_key");
    if (!key.empty() &&
        key_hash_matches(reinterpret_cast<const uint8_t *>(key.data()),
                         key.size())) {
        memcpy(master_key_, key.data(), bambu::MASTER_KEY_SIZE);
        has_key_ = true;
    }
}

std::vector<std::string> BambuCodec::tag_types() const {
    return {TagTypes::MIFARE_CLASSIC_1K};
}

void BambuCodec::mifare_keys(const uint8_t *uid, size_t uid_len,
                             std::map<int, std::pair<std::vector<uint8_t>,
                             std::vector<uint8_t>>> &out) {
    if (!has_key_)
        return;
    uint8_t keys[bambu::SECTORS][bambu::KEY_SIZE];
    bambu::derive_keys(master_key_, uid, (uint8_t)uid_len, keys);
    for (int s = 0; s < bambu::SECTORS; s++) {
        out[s] = {std::vector<uint8_t>(keys[s], keys[s] + bambu::KEY_SIZE),
                  std::vector<uint8_t>()};
    }
}

bool BambuCodec::decode(const Tag &tag, DecodeResult &out) {
    if (tag.tag_type != TagType::MIFARE_CLASSIC_1K ||
        tag.key_source != "bambu" || tag.readable.empty())
        return false;
    for (int b : {1, 2, 4, 5, 6}) {
        if ((size_t)b >= tag.readable.size() || !tag.readable[b])
            return false;
    }

    // Parse the Bambu blocks with the firmware's own decoder.
    bambu::TagInfo info;
    memset(&info, 0, sizeof(info));
    static const uint8_t BLOCKS[] = {1, 2, 4, 5, 6, 8, 9, 10, 12, 13, 14, 16};
    for (uint8_t b : BLOCKS) {
        if ((size_t)b < tag.readable.size() && tag.readable[b]) {
            uint8_t data[bambu::BLOCK_SIZE];
            if (tag.get_block(b, data, sizeof(data)))
                bambu::parse_block(b, data, info);
        }
    }

    bambu_view::to_result(info, true, nullptr, out);
    return out.recognized;
}

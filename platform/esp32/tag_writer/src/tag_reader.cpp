// Copyright (C) 2026 JR Lomas <lomas.jr@gmail.com>
// This file may be distributed under the terms of the GNU GPLv3 license.

#include "tag_reader.h"

#include <memory>
#include <set>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "iso14443a_type2.h"
#include "iso15693.h"

namespace tagrd {

static std::vector<FormatCodec *> g_codecs;

// reader.py DEFAULT_MIFARE_KEYS
static const uint8_t DEFAULT_KEYS[][6] = {
    {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF},
    {0xA0, 0xA1, 0xA2, 0xA3, 0xA4, 0xA5},
    {0xD3, 0xF7, 0xD3, 0xF7, 0xD3, 0xF7},
};

void begin(const CodecOptions &options) {
    for (FormatCodec *c : g_codecs)
        delete c;
    g_codecs = create_codecs(options);
}

const std::vector<FormatCodec *> &codecs() { return g_codecs; }

void end() {
    for (FormatCodec *c : g_codecs)
        delete c;
    std::vector<FormatCodec *>().swap(g_codecs);
}

// ---- MIFARE Classic geometry (iso14443a.py helpers) --------------------
static int mifare_sector_first(int sector) {
    return sector < 32 ? sector * 4 : 128 + (sector - 32) * 16;
}
static int mifare_sector_size(int sector) { return sector < 32 ? 4 : 16; }

static bool same_uid(const CardUid &a, const CardUid &b) {
    return a.size == b.size && memcmp(a.bytes, b.bytes, a.size) == 0;
}

// ---- detection / classification ----------------------------------------
static void classify_14443a(Pn5180 &chip, Tag &tag) {
    uint8_t sak = tag.sak;
    if (sak == 0x09) {
        tag.tag_type = TagType::MIFARE_MINI;
        tag.alloc_memory(16, 20);
        tag.user_first_block = 1;
        tag.user_last_block = 19;
        return;
    }
    if (sak == 0x08 || sak == 0x88 || sak == 0x28) {
        tag.tag_type = TagType::MIFARE_CLASSIC_1K;
        tag.alloc_memory(16, 64);
        tag.user_first_block = 1;
        tag.user_last_block = 63;
        return;
    }
    if (sak == 0x18 || sak == 0x38) {
        tag.tag_type = TagType::MIFARE_CLASSIC_4K;
        tag.alloc_memory(16, 256);
        tag.user_first_block = 1;
        tag.user_last_block = 255;
        return;
    }
    if (sak == 0x00) {
        Iso14443aType2 t2(chip);
        t2.classify_type2(tag);  // GET_VERSION: NTAG / Ultralight / Type 2
        return;
    }
    if (sak & 0x20) {
        tag.tag_type = TagType::ISO14443_4;
        return;
    }
    tag.tag_type = TagType::TYPE2;
    Iso14443aType2(chip).geom_type2(tag, 16);
}

bool detect(Pn5180 &chip, Tag &out, CardUid &out_uid) {
    // Fresh session: power the field down so tags left selected/READY by an
    // earlier command answer the wake-up.
    chip.rf_off();

    // ISO15693 (NFC-V) first, matching reader.py protocols order.
    {
        Iso15693 v(chip);
        v.field_on();
        std::unique_ptr<Tag> t;
        try {
            t = v.inventory();
        } catch (const TagError &) {
            t.reset();
        }
        if (t) {
            try {
                v.get_system_info(*t);
            } catch (const TagError &) {
            }
            out = *t;
            memset(&out_uid, 0, sizeof(out_uid));
            out_uid.size = out.uid.size() > 10 ? 10 : (uint8_t)out.uid.size();
            for (int i = 0; i < out_uid.size; i++)
                out_uid.bytes[i] = out.uid[i];
            return true;
        }
    }

    // ISO14443A: antenna(true) reloads the 14443A RF config and powers up.
    chip.antenna(true);
    CardUid uid;
    memset(&uid, 0, sizeof(uid));
    if (chip.select(uid) != Pn5180::OK)
        return false;
    out = Tag(TagTechnology::ISO14443A, uid.bytes, uid.size, TagType::TYPE2);
    out.has_atqa = true;
    out.atqa[0] = uid.atqa[0];
    out.atqa[1] = uid.atqa[1];
    out.has_sak = true;
    out.sak = uid.sak;
    classify_14443a(chip, out);
    out_uid = uid;
    return true;
}

// ---- MIFARE Classic read (reader.py _read_mifare and helpers) ----------
static bool reselect(Pn5180 &chip, const CardUid &uid) {
    chip.stop_crypto();
    for (int i = 0; i < 4; i++) {
        CardUid again;
        memset(&again, 0, sizeof(again));
        if (chip.select(again) == Pn5180::OK && same_uid(again, uid))
            return true;
    }
    return false;
}

// Authenticate `sector` with `key` and read its blocks into tag.memory.
// Returns false if the sector could not be authenticated.
static bool read_sector(Pn5180 &chip, Tag &tag, const CardUid &uid, int sector,
                        const uint8_t key[6], bool key_b) {
    int first = mifare_sector_first(sector);
    if (!reselect(chip, uid))
        return false;
    if (chip.authenticate((uint8_t)first, key, uid, key_b) != Pn5180::OK)
        return false;
    int n = mifare_sector_size(sector);
    for (int b = first; b < first + n; b++) {
        if ((size_t)b >= tag.block_count)
            break;
        uint8_t data[16];
        if (chip.read_block((uint8_t)b, data) != Pn5180::OK)
            break;
        tag.set_block(b, data, 16);
    }
    SectorKey sk;
    memcpy(sk.key, key, 6);
    sk.key_b = key_b;
    tag.sector_keys[sector] = sk;
    return true;
}

// Try one codec's key map (its lowest sector decides the match).
static bool try_source(Pn5180 &chip, Tag &tag, const CardUid &uid,
                       const std::string &name,
                       const std::map<int, std::pair<std::vector<uint8_t>,
                                                      std::vector<uint8_t>>> &km,
                       std::set<int> &used) {
    if (km.empty())
        return false;
    auto first_it = km.begin();
    int first_s = first_it->first;
    const auto &keys = first_it->second;
    bool matched = false;
    if (keys.first.size() == 6 &&
        read_sector(chip, tag, uid, first_s, keys.first.data(), false))
        matched = true;
    else if (keys.second.size() == 6 &&
             read_sector(chip, tag, uid, first_s, keys.second.data(), true))
        matched = true;
    if (!matched)
        return false;
    tag.key_source = name;
    used.insert(first_s);
    for (const auto &kv : km) {
        int s = kv.first;
        if (s == first_s || used.count(s))
            continue;
        if (kv.second.first.size() == 6 &&
            read_sector(chip, tag, uid, s, kv.second.first.data(), false))
            used.insert(s);
        else if (kv.second.second.size() == 6 &&
                 read_sector(chip, tag, uid, s, kv.second.second.data(), true))
            used.insert(s);
    }
    return true;
}

static bool try_default_sector(Pn5180 &chip, Tag &tag, const CardUid &uid,
                               int sector) {
    for (const auto &key : DEFAULT_KEYS) {
        if (read_sector(chip, tag, uid, sector, key, false))
            return true;
    }
    return false;
}

static void read_mifare(Pn5180 &chip, Tag &tag, const CardUid &uid) {
    int nsec = 16;
    if (tag.tag_type == TagType::MIFARE_MINI)
        nsec = 5;
    else if (tag.tag_type == TagType::MIFARE_CLASSIC_4K)
        nsec = 40;

    std::set<int> used;
    for (FormatCodec *c : g_codecs) {
        std::map<int, std::pair<std::vector<uint8_t>, std::vector<uint8_t>>> km;
        c->mifare_keys(uid.bytes, uid.size, km);
        if (km.empty())
            continue;
        if (try_source(chip, tag, uid, c->name(), km, used))
            break;
    }
    int fails = 0;
    for (int s = 0; s < nsec; s++) {
        if (used.count(s)) {
            fails = 0;
            continue;
        }
        if (try_default_sector(chip, tag, uid, s)) {
            used.insert(s);
            fails = 0;
        } else if (++fails >= 2) {
            break;
        }
    }
    chip.stop_crypto();
    chip.halt();
}

bool read_memory(Pn5180 &chip, Tag &tag, const CardUid &uid) {
    if (tag.tag_type == TagType::ISO14443_4)
        return false;
    if (is_type2_tag(tag.tag_type)) {
        try {
            Iso14443aType2(chip).read_tag(tag);
        } catch (const TagError &) {
        }
        return true;
    }
    if (is_type5_tag(tag.tag_type)) {
        try {
            Iso15693(chip).read_tag(tag);
        } catch (const TagError &) {
        }
        return true;
    }
    if (is_mifare_classic_tag(tag.tag_type)) {
        read_mifare(chip, tag, uid);
        return true;
    }
    return false;
}

DecodeResult decode(const Tag &tag) { return decode_tag(tag, g_codecs); }

// MIFARE Classic sector of a block (1K: 4-block sectors; 4K: 16 above 128).
static int mifare_sector_of(size_t block) {
    return block < 128 ? static_cast<int>(block / 4)
                       : static_cast<int>(32 + (block - 128) / 16);
}
static bool mifare_is_trailer(size_t block) {
    return block < 128 ? (block % 4 == 3) : ((block - 128) % 16 == 15);
}

// Re-select the same tag for a clean (non-nested) authentication.
static bool mifare_reselect(Pn5180 &chip, const CardUid &uid) {
    chip.stop_crypto();
    for (int i = 0; i < 3; ++i) {
        CardUid again;
        if (chip.select(again) == Pn5180::OK && same_uid(again, uid))
            return true;
    }
    return false;
}

// Flush changed blocks to a MIFARE Classic tag: re-authenticate each sector
// with the key that read it (tag.sector_keys, filled by read_memory), write the
// data blocks, then verify. Mirrors reader.py _write_mifare/_verify_mifare with
// allow_trailers=False (never rewrites keys). A nested auth at a sector boundary
// can be flaky, so reselect and retry the sector up to 3 times.
static bool mifare_write_blocks(Pn5180 &chip, Tag &tag, const CardUid &uid,
                                const std::vector<BlockWrite> &writes,
                                std::string &err) {
    size_t i = 0;
    while (i < writes.size()) {
        int s = mifare_sector_of(writes[i].block);
        auto it = tag.sector_keys.find(s);
        if (it == tag.sector_keys.end()) {
            err = "no key for sector " + std::to_string(s);
            return false;
        }
        size_t j = i;
        while (j < writes.size() && mifare_sector_of(writes[j].block) == s) ++j;

        bool ok = false;
        for (int attempt = 1; attempt <= 3 && !ok; ++attempt) {
            if (chip.authenticate(static_cast<uint8_t>(writes[i].block),
                                  it->second.key, uid, it->second.key_b) != Pn5180::OK) {
                if (attempt < 3 && mifare_reselect(chip, uid)) continue;
                err = "auth failed sector " + std::to_string(s);
                return false;
            }
            bool wrote = true;
            for (size_t k = i; k < j; ++k) {
                if (chip.write_block(static_cast<uint8_t>(writes[k].block),
                                     writes[k].data.data()) != Pn5180::OK) {
                    wrote = false;
                    break;
                }
            }
            if (wrote) ok = true;
            else if (attempt < 3 && !mifare_reselect(chip, uid)) {
                err = "tag removed during write";
                return false;
            }
        }
        if (!ok) {
            err = "write failed sector " + std::to_string(s);
            return false;
        }
        i = j;
    }

    // Verify: re-auth each sector from a clean session and read back.
    if (!mifare_reselect(chip, uid)) { err = "tag removed before verify"; return false; }
    int last_s = -1;
    for (const auto &w : writes) {
        int s = mifare_sector_of(w.block);
        auto it = tag.sector_keys.find(s);
        if (last_s != s) {
            if (chip.authenticate(static_cast<uint8_t>(w.block), it->second.key,
                                  uid, it->second.key_b) != Pn5180::OK) {
                err = "verify auth failed sector " + std::to_string(s);
                return false;
            }
            last_s = s;
        }
        uint8_t got[16];
        if (chip.read_block(static_cast<uint8_t>(w.block), got) != Pn5180::OK ||
            memcmp(got, w.data.data(), tag.block_size) != 0) {
            err = "verify mismatch block " + std::to_string(w.block);
            return false;
        }
    }
    chip.stop_crypto();
    return true;
}

bool encode_and_write(Pn5180 &chip, Tag &tag, const CardUid &uid,
                      const std::string &format, const Filament &fields,
                      std::string &err, int *written) {
    if (written) *written = 0;

    FormatCodec *codec = find_codec(g_codecs, format);
    if (!codec) {
        err = "unknown format: " + format;
        return false;
    }
    if (!codec->can_encode(tag)) {
        err = "format '" + format + "' cannot encode a " +
              tag_type_to_string(tag.tag_type) + " tag";
        return false;
    }

    // Snapshot the memory before the codec rewrites the user area so we can
    // write back only the blocks that actually changed.
    std::vector<uint8_t> before = tag.memory;
    err.clear();
    if (!codec->encode(tag, fields, err)) {
        if (err.empty()) err = "encode failed";
        return false;
    }

    bool mc = is_mifare_classic_tag(tag.tag_type);
    std::vector<BlockWrite> writes;
    int first = tag.user_first_block >= 0 ? tag.user_first_block : 0;
    int last = tag.user_last_block;
    for (int b = first; b <= last && static_cast<size_t>(b) < tag.block_count; ++b) {
        if (mc && mifare_is_trailer(static_cast<size_t>(b)))
            continue;  // never rewrite sector trailers / keys here
        size_t off = static_cast<size_t>(b) * tag.block_size;
        if (off + tag.block_size > tag.memory.size()) break;
        bool changed = false;
        for (size_t i = 0; i < tag.block_size; ++i) {
            uint8_t o = (off + i) < before.size() ? before[off + i] : 0;
            if (tag.memory[off + i] != o) { changed = true; break; }
        }
        if (changed) writes.push_back(BlockWrite{static_cast<size_t>(b), tag.get_block(b)});
    }

    if (writes.empty()) {
        // Nothing differs: the tag already holds this content. Not an error.
        return true;
    }

    if (mc) {
        // MIFARE Classic: re-auth each sector with the read key and flush the
        // changed data blocks (reader.py write_blocks -> _write_mifare).
        if (!mifare_write_blocks(chip, tag, uid, writes, err))
            return false;
        if (written) *written = static_cast<int>(writes.size());
        return true;
    }

    if (!is_type2_tag(tag.tag_type) && !is_type5_tag(tag.tag_type)) {
        err = "writing not supported for this tag type";
        return false;
    }

    // write_blocks writes every page then reads each back to verify. A single
    // transceive can report a transient RX collision (notably on the 4-bit
    // WRITE ACK or the verify read of a just-written page) even though the
    // page data is correct. Writing a page is idempotent, so retry the whole
    // batch a few times before giving up; a re-detect refreshes the RF session.
    const int MAX_TRIES = 4;
    for (int attempt = 1; ; ++attempt) {
        try {
            if (is_type2_tag(tag.tag_type)) {
                Iso14443aType2(chip).write_blocks(tag, writes);
            } else {
                Iso15693(chip).write_blocks(tag, writes);
            }
            break;  // success
        } catch (const CollisionError &e) {
            if (attempt >= MAX_TRIES) {
                err = std::string("write failed: ") + e.what();
                return false;
            }
            // Re-establish the RF session before retrying.
            chip.antenna(false);
            vTaskDelay(pdMS_TO_TICKS(10));
            CardUid retry_uid;
            Tag retry_tag;
            if (!detect(chip, retry_tag, retry_uid) || !same_uid(retry_uid, uid)) {
                err = "write failed: tag removed during write";
                return false;
            }
        } catch (const std::exception &e) {
            err = std::string("write failed: ") + e.what();
            return false;
        }
    }

    if (written) *written = static_cast<int>(writes.size());
    return true;
}

}  // namespace tagrd

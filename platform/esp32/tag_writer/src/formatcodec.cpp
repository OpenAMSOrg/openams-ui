// Copyright (C) 2026 JR Lomas <lomas.jr@gmail.com>
// This file may be distributed under the terms of the GNU GPLv3 license.

#include "formatcodec.h"
#include "openprinttag.h"
#include "openspool.h"
#include "spoolman.h"
#include "spoolease.h"
#include "opentag3d.h"
#include "tigertag.h"
#include "anycubic.h"
#include "elegoo.h"
#include "bambu_codec.h"
#include "creality.h"
#include "snapmaker.h"
#include "qidi.h"
#include "ndef.h"
#include <algorithm>
#include <cctype>
#include <cstring>

static const std::string EMPTY_STRING = "";

const std::string &CodecOptions::get(const std::string &k) const {
    auto it = values.find(k);
    if (it != values.end()) {
        return it->second;
    }
    return EMPTY_STRING;
}

std::string json_safe_string(const std::string &s) {
    std::string out = s;
    for (char &c : out) {
        if (static_cast<unsigned char>(c) < 0x20 || static_cast<unsigned char>(c) == 0x7F) {
            c = ' ';
        }
    }
    return out;
}

static bool user_area_empty(const Tag &tag) {
    if (tag.memory.empty() || tag.readable.empty()) {
        return false;
    }
    bool mifare = is_mifare_classic_tag(tag.tag_type);
    bool seen = false;
    int first = tag.user_first_block >= 0 ? tag.user_first_block : 0;
    int last = tag.user_last_block >= 0 ? tag.user_last_block : -1;
    for (int block = first; block <= last; ++block) {
        if (block < 0 || static_cast<size_t>(block) >= tag.block_count) {
            continue;
        }
        if (mifare) {
            if (block < 128) {
                if (block % 4 == 3) continue;
            } else {
                if ((block - 128) % 16 == 15) continue;
            }
        }
        if (!tag.readable[block]) {
            continue;
        }
        seen = true;
        auto blk = tag.get_block(block);
        for (uint8_t b : blk) {
            if (b != 0) return false;
        }
    }
    return seen;
}

static ndef::Tag to_ndef_tag(const Tag &tag) {
    ndef::Tag ntag;
    if (tag.technology == TagTechnology::ISO14443A) {
        ntag.technology = ndef::Technology::ISO14443A;
    } else if (tag.technology == TagTechnology::ISO15693) {
        ntag.technology = ndef::Technology::ISO15693;
    } else {
        ntag.technology = ndef::Technology::Unknown;
    }
    switch (tag.tag_type) {
        case TagType::NTAG213: ntag.tag_type = ndef::TagType::NTAG213; break;
        case TagType::NTAG215: ntag.tag_type = ndef::TagType::NTAG215; break;
        case TagType::NTAG216: ntag.tag_type = ndef::TagType::NTAG216; break;
        case TagType::ICODE_SLIX2: ntag.tag_type = ndef::TagType::ICODE_SLIX2; break;
        case TagType::TYPE5: ntag.tag_type = ndef::TagType::Type5; break;
        default:
            if (is_type2_tag(tag.tag_type)) ntag.tag_type = ndef::TagType::Type2;
            else if (is_type5_tag(tag.tag_type)) ntag.tag_type = ndef::TagType::Type5;
            else if (is_mifare_classic_tag(tag.tag_type)) ntag.tag_type = ndef::TagType::MifareClassic;
            else ntag.tag_type = ndef::TagType::Unknown;
            break;
    }
    ntag.memory = tag.memory;
    ntag.block_size = tag.block_size;
    ntag.user_first_block = tag.user_first_block >= 0 ? tag.user_first_block : 0;
    ntag.user_last_block = tag.user_last_block >= 0 ? tag.user_last_block : 0;
    return ntag;
}

std::vector<FormatCodec *> create_codecs(const CodecOptions &options) {
    // Registry in detection order matching klipper_rf formats/__init__.py
    // codec_classes().
    return {
        // NDEF formats
        new OpenPrintTagCodec(options),
        new OpenSpoolCodec(options),
        new SpoolmanCodec(options),
        new SpoolEaseCodec(options),
        new OpenTag3DCodec(options),
        // Fixed layouts on NFC Forum Type 2 tags
        new TigerTagCodec(options),
        new AnycubicCodec(options),
        new ElegooCodec(options),
        // MIFARE Classic (keyed formats first, default-key formats last)
        new BambuCodec(options),
        new CrealityCodec(options),
        new SnapmakerCodec(options),
        new QidiCodec(options),
    };
}

FormatCodec *find_codec(const std::vector<FormatCodec *> &codecs,
                        const std::string &name) {
    for (FormatCodec *c : codecs) {
        if (c && name == c->name()) {
            return c;
        }
    }
    return nullptr;
}

DecodeResult decode_tag(const Tag &tag,
                        const std::vector<FormatCodec *> &codecs) {
    std::string tag_type_str = tag_type_to_string(tag.tag_type);
    for (FormatCodec *codec : codecs) {
        if (!codec) continue;
        auto types = codec->tag_types();
        if (!types.empty()) {
            bool matched = false;
            for (const auto &t : types) {
                if (t == tag_type_str) {
                    matched = true;
                    break;
                }
            }
            if (!matched) {
                continue;
            }
        }

        DecodeResult res;
        try {
            if (codec->decode(tag, res)) {
                // Ensure safe strings in filament output
                res.filament.brand = json_safe_string(res.filament.brand);
                res.filament.material = json_safe_string(res.filament.material);
                res.filament.name = json_safe_string(res.filament.name);
                res.filament.sku = json_safe_string(res.filament.sku);
                res.filament.spool_id = json_safe_string(res.filament.spool_id);
                for (auto &kv : res.filament.fields) {
                    kv.second = json_safe_string(kv.second);
                }
                for (auto &kv : res.info) {
                    kv.second = json_safe_string(kv.second);
                }
                return res;
            }
        } catch (const std::exception &e) {
            DecodeResult err_res;
            err_res.recognized = false;
            err_res.format = codec->name();
            err_res.info["error"] = json_safe_string(std::string("decoder failure: ") + e.what());
            return err_res;
        }
    }

    ndef::Tag ntag = to_ndef_tag(tag);
    ndef::NdefMessage msg;
    bool has_msg = ndef::read_message(ntag, msg);
    bool blank_ndef = has_msg && !msg.records.empty();
    if (blank_ndef) {
        for (const auto &r : msg.records) {
            if (r.tnf != ndef::TNF_EMPTY) {
                blank_ndef = false;
                break;
            }
        }
    }

    bool blank = blank_ndef || user_area_empty(tag);

    DecodeResult out;
    out.recognized = false;
    out.format = blank ? "blank" : "unknown";
    return out;
}

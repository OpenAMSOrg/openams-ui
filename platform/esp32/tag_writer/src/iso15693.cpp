// Copyright (C) 2026 JR Lomas <lomas.jr@gmail.com>
// This file may be distributed under the terms of the GNU GPLv3 license.

// ISO/IEC 15693 implementation matching iso15693.py and reader.py.
#include "iso15693.h"
#include <stdio.h>
#include <string.h>
#include <algorithm>

TagType classify_icode(const uint8_t *uid8) {
    if (!uid8 || uid8[0] != 0xE0 || uid8[1] != 0x04) {
        return TagType::TYPE5;
    }
    if (uid8[2] == 0x01) {
        uint8_t t = (uid8[3] >> 3) & 3;
        if (t == 2) return TagType::ICODE_SLIX;
        if (t == 1) return TagType::ICODE_SLIX2;
        if (t == 3) return TagType::ICODE_DNA;
        return TagType::TYPE5;
    }
    if (uid8[2] == 0x02) {
        return TagType::ICODE_SLIX_S;
    }
    if (uid8[2] == 0x03) {
        return TagType::ICODE_SLIX_L;
    }
    return TagType::TYPE5;
}

TagType classify_icode(const std::vector<uint8_t> &uid) {
    if (uid.size() != 8) {
        return TagType::TYPE5;
    }
    return classify_icode(uid.data());
}

void known_size(TagType tag_type, size_t &nblocks, size_t &bsize, int &user_last) {
    bsize = 4;
    switch (tag_type) {
        case TagType::ICODE_SLIX:
            nblocks = 28;
            user_last = 27;
            break;
        case TagType::ICODE_SLIX2:
            nblocks = 80;
            user_last = 78; // Block 79 is reserved counter block
            break;
        case TagType::ICODE_SLIX_S:
            nblocks = 40;
            user_last = 39;
            break;
        case TagType::ICODE_SLIX_L:
            nblocks = 8;
            user_last = 7;
            break;
        case TagType::ICODE_DNA:
            nblocks = 63;
            user_last = 62;
            break;
        case TagType::TYPE5:
        default:
            nblocks = 8;
            user_last = 7;
            break;
    }
}

void Iso15693::field_on() {
    chip_.load_rf_config(RF_ISO15693_TX, RF_ISO15693_RX);
    chip_.rf_on();
    chip_.set_crc(true, true);
}

void Iso15693::field_off() {
    chip_.rf_off();
}

std::unique_ptr<Tag> Iso15693::inventory() {
    uint8_t cmd[3] = {0x26, 0x01, 0x00};
    uint8_t rx_buf[PN5180_MAX_FRAME];
    size_t rx_len = 0;
    uint8_t last_bits = 0;

    Pn5180::Status st = chip_.transceive(cmd, 3, 0, rx_buf, &rx_len, &last_bits, 20);
    if (st == Pn5180::NO_CARD) {
        return nullptr;
    }
    if (st == Pn5180::COLLISION) {
        throw CollisionError("collision");
    }
    if (st != Pn5180::OK) {
        char err[64];
        snprintf(err, sizeof(err), "inventory failed: %s", Pn5180::status_name(st));
        throw TagError(err);
    }
    if (rx_len < 10) {
        throw TagError("short inventory");
    }
    uint8_t flags = rx_buf[0];
    if (flags & 1) {
        char err[64];
        snprintf(err, sizeof(err), "inventory error 0x%02x", rx_buf[1]);
        throw Iso15693Error(err, rx_buf[1]);
    }
    uint8_t dsfid = rx_buf[1];
    // UID on air is LSB first (rx_buf[2..9]); canonical UID is MSB first
    std::vector<uint8_t> uid(8);
    for (int i = 0; i < 8; ++i) {
        uid[i] = rx_buf[9 - i];
    }
    TagType type = classify_icode(uid);
    auto tag = std::unique_ptr<Tag>(new Tag(TagTechnology::ISO15693, uid, type));
    tag->has_dsfid = true;
    tag->dsfid = dsfid;
    return tag;
}

void Iso15693::get_system_info(Tag &tag) {
    uint8_t uid_lsb[8];
    for (int i = 0; i < 8; ++i) {
        uid_lsb[i] = tag.uid[7 - i];
    }
    size_t nblocks = 8;
    size_t bsize = 4;
    int user_last = 7;
    known_size(tag.tag_type, nblocks, bsize, user_last);
    bool reported = false;

    uint8_t frame[10];
    frame[0] = 0x22;
    frame[1] = 0x2B;
    memcpy(&frame[2], uid_lsb, 8);

    uint8_t rx_buf[PN5180_MAX_FRAME];
    size_t rx_len = 0;
    uint8_t last_bits = 0;

    Pn5180::Status st = chip_.transceive(frame, 10, 0, rx_buf, &rx_len, &last_bits, 20);
    if (st == Pn5180::OK && rx_len >= 2 && !(rx_buf[0] & 1)) {
        uint8_t info = rx_buf[1];
        size_t pos = 10;
        if (rx_len >= pos) {
            if ((info & 1) && pos < rx_len) {
                tag.has_dsfid = true;
                tag.dsfid = rx_buf[pos++];
            }
            if ((info & 2) && pos < rx_len) {
                tag.has_afi = true;
                tag.afi = rx_buf[pos++];
            }
            if ((info & 4) && pos + 1 < rx_len) {
                nblocks = std::min(static_cast<size_t>(rx_buf[pos]) + 1, static_cast<size_t>(256));
                bsize = (rx_buf[pos + 1] & 0x1F) + 1;
                reported = true;
                pos += 2;
            }
            if ((info & 8) && pos < rx_len) {
                tag.has_ic_ref = true;
                tag.ic_ref = rx_buf[pos++];
            }
        }
    }

    tag.alloc_memory(bsize, nblocks);
    tag.user_first_block = 0;
    if (reported && tag.tag_type == TagType::TYPE5) {
        user_last = static_cast<int>(nblocks) - 1;
    }
    tag.user_last_block = std::min(user_last, static_cast<int>(nblocks) - 1);
}

std::vector<uint8_t> Iso15693::read_blocks(Tag &tag, size_t first, size_t count) {
    uint8_t uid_lsb[8];
    for (int i = 0; i < 8; ++i) {
        uid_lsb[i] = tag.uid[7 - i];
    }
    size_t bs = tag.block_size;
    size_t max_n = std::max(static_cast<size_t>(1), static_cast<size_t>(PN5180_MAX_FRAME - 1) / bs);

    std::vector<uint8_t> out;
    size_t i = 0;
    while (i < count) {
        size_t block = first + i;
        if (!tag.no_read_multiple) {
            size_t n = std::min(count - i, max_n);
            auto res = read_multiple(tag, uid_lsb, block, n, bs);
            if (res.second) {
                out.insert(out.end(), res.first.begin(), res.first.end());
                i += n;
                continue;
            }
        }
        auto data = read_single(uid_lsb, block, bs);
        out.insert(out.end(), data.begin(), data.end());
        i += 1;
    }
    return out;
}

std::pair<std::vector<uint8_t>, bool> Iso15693::read_multiple(
    Tag &tag, const uint8_t uid_lsb[8], size_t first, size_t n, size_t bs) {
    uint8_t frame[12];
    frame[0] = 0x22;
    frame[1] = 0x23;
    memcpy(&frame[2], uid_lsb, 8);
    frame[10] = static_cast<uint8_t>(first);
    frame[11] = static_cast<uint8_t>(n - 1);

    uint8_t rx_buf[PN5180_MAX_FRAME];
    size_t rx_len = 0;
    uint8_t last_bits = 0;

    Pn5180::Status st = chip_.transceive(frame, 12, 0, rx_buf, &rx_len, &last_bits, 50);
    if (st == Pn5180::COLLISION) {
        throw CollisionError("collision");
    }
    if (st != Pn5180::OK) {
        char err[64];
        snprintf(err, sizeof(err), "read multiple failed: %s", Pn5180::status_name(st));
        throw TagError(err);
    }
    if (rx_len > 0 && (rx_buf[0] & 1)) {
        uint8_t code = rx_len > 1 ? rx_buf[1] : 0;
        if (code == 0x01 || code == 0x02) {
            tag.no_read_multiple = true;
            return {std::vector<uint8_t>(), false};
        }
        char err[64];
        snprintf(err, sizeof(err), "read multiple error 0x%02x", code);
        throw Iso15693Error(err, code);
    }
    if (rx_len < 1 + n * bs) {
        throw TagError("short read multiple");
    }
    return {std::vector<uint8_t>(&rx_buf[1], &rx_buf[1 + n * bs]), true};
}

std::vector<uint8_t> Iso15693::read_single(const uint8_t uid_lsb[8], size_t block, size_t bs) {
    uint8_t frame[11];
    frame[0] = 0x22;
    frame[1] = 0x20;
    memcpy(&frame[2], uid_lsb, 8);
    frame[10] = static_cast<uint8_t>(block);

    uint8_t rx_buf[PN5180_MAX_FRAME];
    size_t rx_len = 0;
    uint8_t last_bits = 0;

    Pn5180::Status st = chip_.transceive(frame, 11, 0, rx_buf, &rx_len, &last_bits, 50);
    if (st == Pn5180::COLLISION) {
        throw CollisionError("collision");
    }
    if (st != Pn5180::OK) {
        char err[64];
        snprintf(err, sizeof(err), "read single failed: %s", Pn5180::status_name(st));
        throw TagError(err);
    }
    if (rx_len > 0 && (rx_buf[0] & 1)) {
        uint8_t code = rx_len > 1 ? rx_buf[1] : 0;
        char err[64];
        snprintf(err, sizeof(err), "read single error 0x%02x", code);
        throw Iso15693Error(err, code);
    }
    if (rx_len < 1 + bs) {
        throw TagError("short read single");
    }
    return std::vector<uint8_t>(&rx_buf[1], &rx_buf[1 + bs]);
}

void Iso15693::write_block(Tag &tag, size_t block, const uint8_t *data, size_t len) {
    uint8_t uid_lsb[8];
    for (int i = 0; i < 8; ++i) {
        uid_lsb[i] = tag.uid[7 - i];
    }
    std::vector<uint8_t> frame;
    frame.reserve(11 + len);
    frame.push_back(0x22);
    frame.push_back(0x21);
    for (int i = 0; i < 8; ++i) {
        frame.push_back(uid_lsb[i]);
    }
    frame.push_back(static_cast<uint8_t>(block));
    for (size_t i = 0; i < len; ++i) {
        frame.push_back(data[i]);
    }

    uint8_t rx_buf[PN5180_MAX_FRAME];
    size_t rx_len = 0;
    uint8_t last_bits = 0;

    Pn5180::Status st = chip_.transceive(frame.data(), frame.size(), 0, rx_buf, &rx_len, &last_bits, 60);
    if (st == Pn5180::COLLISION) {
        throw CollisionError("collision");
    }
    if (st == Pn5180::NO_CARD) {
        throw NoTagError("no card");
    }
    if (rx_len > 0 && (rx_buf[0] & 1)) {
        uint8_t code = rx_len > 1 ? rx_buf[1] : 0;
        char err[64];
        snprintf(err, sizeof(err), "ISO15693 error 0x%02x on block %zu", code, block);
        throw TagWriteError(err);
    }
    if (st != Pn5180::OK) {
        char err[64];
        snprintf(err, sizeof(err), "write block %zu failed: %s", block, Pn5180::status_name(st));
        throw TagWriteError(err);
    }
}

void Iso15693::write_block(Tag &tag, size_t block, const std::vector<uint8_t> &data) {
    write_block(tag, block, data.data(), data.size());
}

void Iso15693::read_tag(Tag &tag) {
    auto data = read_blocks(tag, 0, tag.block_count);
    size_t bs = tag.block_size;
    for (size_t i = 0; i < tag.block_count; ++i) {
        tag.set_block(i, &data[i * bs], bs);
    }

    if (tag.tag_type != TagType::TYPE5 || tag.block_count < 1) {
        return;
    }

    auto cc = tag.get_block(0);
    // An NDEF capability container is 4 bytes; a tag that reports a 1-2 byte
    // block gives a shorter block 0, and cc[2] would read out of bounds.
    if (cc.size() < 4) {
        return;
    }
    if (cc[0] != 0xE1 && cc[0] != 0xE2) {
        return;
    }
    size_t mlen = 0;
    if (cc[0] == 0xE2 && tag.memory.size() >= 8) {
        mlen = (static_cast<size_t>(tag.memory[6]) << 8) | tag.memory[7];
    } else {
        mlen = cc[2];
    }
    size_t needed = std::min((mlen * 8) / bs, static_cast<size_t>(256));
    size_t base = tag.block_count;
    if (needed <= base) {
        return;
    }

    std::vector<uint8_t> rest;
    try {
        rest = read_blocks(tag, base, needed - base);
    } catch (const CollisionError &) {
        throw;
    } catch (const TagError &) {
        return;
    }

    std::vector<uint8_t> old = tag.memory;
    tag.alloc_memory(bs, needed);
    tag.user_first_block = 0;
    tag.user_last_block = static_cast<int>(needed) - 1;

    for (size_t i = 0; i < base; ++i) {
        tag.set_block(i, &old[i * bs], bs);
    }
    for (size_t i = 0; i < needed - base; ++i) {
        tag.set_block(base + i, &rest[i * bs], bs);
    }
}

void Iso15693::write_blocks(Tag &tag, const std::vector<BlockWrite> &writes) {
    for (const auto &w : writes) {
        if (static_cast<int>(w.block) < tag.user_first_block ||
            static_cast<int>(w.block) > tag.user_last_block) {
            char err[64];
            snprintf(err, sizeof(err), "block %zu is outside the user area", w.block);
            throw TagWriteError(err);
        }
        if (tag.tag_type == TagType::ICODE_SLIX2 && w.block == 79) {
            throw TagWriteError("refusing SLIX2 counter block 79");
        }
        if (w.data.size() != tag.block_size) {
            char err[64];
            snprintf(err, sizeof(err), "block %zu length %zu", w.block, w.data.size());
            throw TagWriteError(err);
        }
    }

    for (const auto &w : writes) {
        write_block(tag, w.block, w.data);
    }
    verify_blocks(tag, writes);
}

void Iso15693::verify_blocks(Tag &tag, const std::vector<BlockWrite> &writes) {
    for (const auto &w : writes) {
        auto got = read_blocks(tag, w.block, 1);
        if (got != w.data) {
            char err[64];
            snprintf(err, sizeof(err), "verify failed at block %zu", w.block);
            throw TagWriteError(err);
        }
        tag.set_block(w.block, got);
    }
}

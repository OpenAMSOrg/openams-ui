// Copyright (C) 2026 JR Lomas <lomas.jr@gmail.com>
// This file may be distributed under the terms of the GNU GPLv3 license.

// ISO/IEC 14443A Type 2 implementation matching iso14443a.py.
#include "iso14443a_type2.h"
#include <stdio.h>
#include <string.h>
#include <algorithm>

namespace {
    struct CrcRestore {
        Pn5180 &c;
        // set_crc can throw (a BUSY timeout); a throw from a destructor during
        // the unwinding of another exception would terminate the firmware.
        ~CrcRestore() {
            try {
                c.set_crc(true, true);
            } catch (...) {
            }
        }
    };
}

void Iso14443aType2::field_on() {
    chip_.load_rf_config(0x00, 0x80);
    chip_.rf_on();
    chip_.set_crc(true, true);
}

void Iso14443aType2::field_off() {
    chip_.rf_off();
}

void Iso14443aType2::geom_type2(Tag &tag, size_t pages, int user_last) {
    tag.alloc_memory(4, pages);
    tag.user_first_block = 4;
    if (user_last < 0) {
        user_last = static_cast<int>(pages) - 1;
    }
    tag.user_last_block = user_last;
}

bool Iso14443aType2::reactivate(Tag &tag) {
    CardUid cuid;
    memset(&cuid, 0, sizeof(cuid));
    cuid.size = static_cast<uint8_t>(std::min(tag.uid.size(), sizeof(cuid.bytes)));
    memcpy(cuid.bytes, tag.uid.data(), cuid.size);
    cuid.sak = tag.has_sak ? tag.sak : 0x00;
    if (tag.has_atqa) {
        cuid.atqa[0] = tag.atqa[0];
        cuid.atqa[1] = tag.atqa[1];
    }
    bool ok = chip_.reselect(cuid);
    if (ok) {
        tag.has_sak = true;
        tag.sak = cuid.sak;
        tag.has_atqa = true;
        tag.atqa[0] = cuid.atqa[0];
        tag.atqa[1] = cuid.atqa[1];
    }
    return ok;
}

void Iso14443aType2::classify_type2(Tag &tag) {
    chip_.set_crc(true, true);
    uint8_t cmd = 0x60;
    uint8_t rx_buf[PN5180_MAX_FRAME];
    size_t rx_len = 0;
    uint8_t last_bits = 0;

    Pn5180::Status st = chip_.transceive(&cmd, 1, 0, rx_buf, &rx_len, &last_bits, 50);
    if (st == Pn5180::COLLISION) {
        throw CollisionError("collision");
    }
    if (st != Pn5180::OK || rx_len != 8) {
        // No answer, or a NAK: a Type 2 tag without GET_VERSION (plain
        // Ultralight, NTAG203, clones). It is now halted: wake it.
        reactivate(tag);
        tag.tag_type = TagType::TYPE2;
        geom_type2(tag, 16);
        return;
    }

    tag.version.assign(rx_buf, rx_buf + 8);
    uint8_t tt = rx_buf[2];
    uint8_t sz = rx_buf[6];

    if (tt == 0x04) {
        if (sz == 0x0F) {
            tag.tag_type = TagType::NTAG213;
            geom_type2(tag, 45, 39);
            return;
        }
        if (sz == 0x11) {
            tag.tag_type = TagType::NTAG215;
            geom_type2(tag, 135, 129);
            return;
        }
        if (sz == 0x13) {
            tag.tag_type = TagType::NTAG216;
            geom_type2(tag, 231, 225);
            return;
        }
    }
    if (tt == 0x03) {
        tag.tag_type = TagType::ULTRALIGHT_EV1;
        if (sz == 0x0B) {
            geom_type2(tag, 20, 15);
            return;
        }
        if (sz == 0x0E) {
            geom_type2(tag, 41, 35);
            return;
        }
    }

    tag.tag_type = TagType::TYPE2;
    geom_type2(tag, 16);
}

std::vector<uint8_t> Iso14443aType2::read_pages(uint8_t page) {
    uint8_t out[16];
    Pn5180::Status st = read_pages(page, out);
    if (st == Pn5180::COLLISION) {
        throw CollisionError("collision");
    }
    if (st == Pn5180::NO_CARD) {
        throw NoTagError("no card");
    }
    if (st != Pn5180::OK) {
        char err[64];
        snprintf(err, sizeof(err), "read_pages %u failed: %s", page, Pn5180::status_name(st));
        throw TagError(err);
    }
    return std::vector<uint8_t>(out, out + 16);
}

Pn5180::Status Iso14443aType2::read_pages(uint8_t page, uint8_t out[16]) {
    chip_.set_crc(true, true);
    uint8_t cmd[2] = {0x30, page};
    uint8_t rx_buf[PN5180_MAX_FRAME];
    size_t rx_len = 0;
    uint8_t last_bits = 0;

    Pn5180::Status st = chip_.transceive(cmd, 2, 0, rx_buf, &rx_len, &last_bits, 50);
    if (st != Pn5180::OK) {
        return st;
    }
    if (rx_len != 16) {
        return Pn5180::ERROR;
    }
    memcpy(out, rx_buf, 16);
    return Pn5180::OK;
}

std::vector<uint8_t> Iso14443aType2::fast_read(uint8_t start, uint8_t end) {
    int n = static_cast<int>(end) - static_cast<int>(start) + 1;
    if (n < 1 || n > 12) {
        char err[64];
        snprintf(err, sizeof(err), "FAST_READ window %d pages", n);
        throw TagError(err);
    }
    uint8_t out[PN5180_MAX_FRAME];
    size_t out_len = 0;
    Pn5180::Status st = fast_read(start, end, out, &out_len);
    if (st == Pn5180::COLLISION) {
        throw CollisionError("collision");
    }
    if (st == Pn5180::NO_CARD) {
        throw NoTagError("no card");
    }
    if (st != Pn5180::OK) {
        char err[64];
        snprintf(err, sizeof(err), "fast_read %u..%u failed: %s", start, end, Pn5180::status_name(st));
        throw TagError(err);
    }
    return std::vector<uint8_t>(out, out + out_len);
}

Pn5180::Status Iso14443aType2::fast_read(uint8_t start, uint8_t end, uint8_t *out, size_t *out_len) {
    int n = static_cast<int>(end) - static_cast<int>(start) + 1;
    if (n < 1 || n > 12) {
        return Pn5180::ERROR;
    }
    chip_.set_crc(true, true);
    uint8_t cmd[3] = {0x3A, start, end};
    uint8_t rx_buf[PN5180_MAX_FRAME];
    size_t rx_len = 0;
    uint8_t last_bits = 0;

    Pn5180::Status st = chip_.transceive(cmd, 3, 0, rx_buf, &rx_len, &last_bits, 50);
    if (st != Pn5180::OK) {
        return st;
    }
    if (rx_len != static_cast<size_t>(4 * n)) {
        return Pn5180::ERROR;
    }
    if (out) {
        memcpy(out, rx_buf, rx_len);
    }
    if (out_len) {
        *out_len = rx_len;
    }
    return Pn5180::OK;
}

void Iso14443aType2::write_page(uint8_t page, const uint8_t data[4]) {
    chip_.set_crc(true, false);
    CrcRestore restore{chip_};

    uint8_t frame[6];
    frame[0] = 0xA2;
    frame[1] = page;
    memcpy(&frame[2], data, 4);

    uint8_t rx_buf[PN5180_MAX_FRAME];
    size_t rx_len = 0;
    uint8_t last_bits = 0;

    Pn5180::Status st = chip_.transceive(frame, 6, 0, rx_buf, &rx_len, &last_bits, 20);
    if (st == Pn5180::COLLISION) {
        throw CollisionError("collision");
    }
    if (st == Pn5180::NO_CARD) {
        throw NoTagError("no card");
    }
    if (st == Pn5180::NAK) {
        char err[64];
        snprintf(err, sizeof(err), "NAK at page %u", page);
        throw TagWriteError(err);
    }
    if (st != Pn5180::OK && st != Pn5180::CRC_ERROR) {
        char err[64];
        snprintf(err, sizeof(err), "write failed at page %u: %s", page, Pn5180::status_name(st));
        throw TagWriteError(err);
    }
    if (rx_len > 0) {
        if ((rx_buf[0] & 0x0F) != 0x0A) {
            char err[64];
            snprintf(err, sizeof(err), "NAK 0x%x last_bits=%u at page %u",
                     rx_buf[0] & 0x0F, last_bits, page);
            throw TagWriteError(err);
        }
    }
}

void Iso14443aType2::write_page(uint8_t page, const std::vector<uint8_t> &data) {
    if (data.size() != 4) {
        throw TagError("WRITE needs 4 bytes");
    }
    write_page(page, data.data());
}

void Iso14443aType2::read_tag(Tag &tag) {
    if (tag.tag_type == TagType::TYPE2) {
        std::vector<uint8_t> hdr;
        try {
            hdr = read_pages(0);
        } catch (const TagError &) {
            // failed to read page 0 header
        }
        if (hdr.size() >= 16 && hdr[12] == 0xE1) {
            size_t pages = std::min(4 + (static_cast<size_t>(hdr[14]) * 8) / 4, MAX_TYPE2_PAGES);
            if (pages > tag.block_count) {
                tag.alloc_memory(4, pages);
                tag.user_first_block = 4;
                tag.user_last_block = static_cast<int>(pages) - 1;
            }
        }
    }

    size_t n = tag.block_count;
    size_t step = (!tag.version.empty()) ? 12 : 4;
    int failures = 0;
    size_t start = 0;

    while (start < n) {
        size_t end = std::min(start + step - 1, n - 1);
        try {
            std::vector<uint8_t> data;
            if (!tag.version.empty()) {
                data = fast_read(static_cast<uint8_t>(start), static_cast<uint8_t>(end));
            } else {
                data = read_pages(static_cast<uint8_t>(start));
            }
            size_t count = end - start + 1;
            for (size_t i = 0; i < count; ++i) {
                tag.set_block(start + i, &data[i * 4], 4);
            }
            failures = 0;
        } catch (const CollisionError &) {
            throw;
        } catch (const TagError &) {
            failures++;
            if (failures >= 2 || !reactivate(tag)) {
                break;
            }
        }
        start = end + 1;
    }
}

void Iso14443aType2::write_blocks(Tag &tag, const std::vector<BlockWrite> &writes) {
    for (const auto &w : writes) {
        if (static_cast<int>(w.block) < tag.user_first_block ||
            static_cast<int>(w.block) > tag.user_last_block) {
            char err[64];
            snprintf(err, sizeof(err), "block %zu is outside the user area", w.block);
            throw TagWriteError(err);
        }
        if (w.block <= 3) {
            char err[64];
            snprintf(err, sizeof(err), "refusing Type 2 page %zu", w.block);
            throw TagWriteError(err);
        }
        if (tag.tag_type == TagType::TYPE2) {
            bool all_readable = true;
            for (int b = tag.user_first_block; b <= tag.user_last_block; ++b) {
                if (b >= 0 && static_cast<size_t>(b) < tag.readable.size() && !tag.readable[b]) {
                    all_readable = false;
                    break;
                }
            }
            if (!all_readable) {
                throw TagWriteError(
                    "the capability container does not match the tag memory; refusing to write");
            }
        }
        if (w.data.size() != tag.block_size) {
            char err[64];
            snprintf(err, sizeof(err), "block %zu length %zu", w.block, w.data.size());
            throw TagWriteError(err);
        }
    }

    for (const auto &w : writes) {
        write_page(static_cast<uint8_t>(w.block), w.data.data());
    }
    verify_blocks(tag, writes);
}

void Iso14443aType2::verify_blocks(Tag &tag, const std::vector<BlockWrite> &writes) {
    for (const auto &w : writes) {
        auto r = read_pages(static_cast<uint8_t>(w.block));
        std::vector<uint8_t> got(r.begin(), r.begin() + tag.block_size);
        if (got != w.data) {
            char err[64];
            snprintf(err, sizeof(err), "verify failed at block %zu", w.block);
            throw TagWriteError(err);
        }
        tag.set_block(w.block, got);
    }
}

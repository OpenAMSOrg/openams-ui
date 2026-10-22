// Copyright (C) 2026 JR Lomas <lomas.jr@gmail.com>
// This file may be distributed under the terms of the GNU GPLv3 license.

#include "tag_ops.h"

#include <stdio.h>
#include <string.h>

#include "esp_random.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sha256.h"

namespace tagops {

typedef Reader::Status Status;

enum KeyChoice : uint8_t { KEY_DERIVED, KEY_DEFAULT };

static Reader *reader;
static uint8_t master_key[bambu::MASTER_KEY_SIZE];
static bool have_master_key;

static const uint8_t READ_BLOCKS[] = {1, 2, 4, 5, 6, 8, 9, 10, 12, 13, 14, 16};
static const uint8_t SIGNATURE_BLOCK = 40;

void begin(Reader &r) { reader = &r; }

void set_master_key(const uint8_t key[bambu::MASTER_KEY_SIZE]) {
    memcpy(master_key, key, sizeof(master_key));
    have_master_key = false;
    for (uint8_t i = 0; i < sizeof(master_key); i++)
        have_master_key |= master_key[i] != 0;
}

bool has_master_key() { return have_master_key; }

void key_fingerprint(char out[9]) {
    uint8_t digest[32];
    Sha256 sha;
    sha.begin();
    sha.update(master_key, sizeof(master_key));
    sha.finish(digest);
    for (uint8_t i = 0; i < 4; i++)
        sprintf(out + i * 2, "%02X", digest[i]);
}

void field(bool on) {
    reader->antenna(on);
    if (on)
        vTaskDelay(pdMS_TO_TICKS(5));  // let a tag power up before talking to it
}

static Result result(bool ok, Status status, uint8_t sector, const char *message) {
    Result r = {ok, status, sector, message};
    return r;
}

bool same_uid(const CardUid &a, const CardUid &b) {
    return a.size == b.size && !memcmp(a.bytes, b.bytes, a.size);
}

// After a failed authentication the tag drops out of the session; bring the
// same tag back.
static bool reselect(const Card &card) {
    reader->stop_crypto();
    CardUid again;
    for (uint8_t attempt = 0; attempt < 3; attempt++) {
        if (reader->select(again) == Reader::OK && same_uid(again, card.uid))
            return true;
    }
    return false;
}

// Authenticates `sector` with key A, trying `preferred` first and then the
// other of {derived, factory default}. Updates `preferred`.
static Status open_sector(Card &card, uint8_t sector, KeyChoice &preferred,
                          bool allow_fallback = true) {
    KeyChoice order[2] = {preferred, preferred == KEY_DERIVED ? KEY_DEFAULT : KEY_DERIVED};
    for (uint8_t i = 0; i < (allow_fallback ? 2 : 1); i++) {
        if (order[i] == KEY_DERIVED && !have_master_key)
            continue;
        const uint8_t *key = order[i] == KEY_DERIVED ? card.keys[sector] : bambu::DEFAULT_KEY;
        Status status = reader->authenticate(sector * 4 + 3, key, card.uid);
        if (status == Reader::OK) {
            preferred = order[i];
            return status;
        }
        if (!reselect(card))
            return Reader::NO_CARD;
    }
    return Reader::AUTH_FAILED;
}

static void finish() {
    reader->halt();
    reader->stop_crypto();
}

static Kind classify(Card &card) {
    if (!(card.uid.sak & 0x08))
        return UNSUPPORTED;
    KeyChoice key = KEY_DERIVED;
    if (open_sector(card, 0, key) != Reader::OK)
        return UNKNOWN_KEYS;
    if (key == KEY_DEFAULT)
        return BLANK;

    uint8_t data[16];
    bool locked = reader->read_block(3, data) == Reader::OK &&
                  !memcmp(data + 6, bambu::ACCESS_BAMBU_LOCKED, 3);
    bool signed_tag = false;
    if (open_sector(card, SIGNATURE_BLOCK / 4, key, false) == Reader::OK &&
            reader->read_block(SIGNATURE_BLOCK, data) == Reader::OK) {
        for (uint8_t i = 0; i < 16; i++)
            signed_tag |= data[i] != 0;
    }
    if (signed_tag)
        return GENUINE;
    return locked ? LOCKED : CUSTOM;
}

bool detect(Card &card, const Card *known) {
    card.kind = NONE;
    if (reader->select(card.uid) != Reader::OK)
        return false;
    if (known && known->kind != NONE && same_uid(card.uid, known->uid)) {
        card = *known;
        finish();
        return true;
    }
    if (have_master_key)
        bambu::derive_keys(master_key, card.uid.bytes, card.uid.size, card.keys);
    card.kind = classify(card);
    finish();
    return true;
}

static bool begin_session(Card &card) {
    reader->stop_crypto();
    return reselect(card);
}

Result read(Card &card, bambu::TagInfo &info) {
    memset(&info, 0, sizeof(info));
    if (card.kind == UNSUPPORTED || card.kind == UNKNOWN_KEYS || card.kind == NONE)
        return result(false, Reader::OK, 0xFF, "not a readable MIFARE tag");
    if (!begin_session(card))
        return result(false, Reader::NO_CARD, 0xFF, "tag removed");
    KeyChoice key = card.kind == BLANK ? KEY_DEFAULT : KEY_DERIVED;
    uint8_t open = 0xFF;
    for (uint8_t i = 0; i < sizeof(READ_BLOCKS); i++) {
        uint8_t block = READ_BLOCKS[i];
        if (block / 4 != open) {
            Status status = open_sector(card, block / 4, key);
            if (status != Reader::OK) {
                finish();
                return result(false, status, block / 4, "cannot open sector");
            }
            open = block / 4;
        }
        uint8_t data[16];
        Status status = reader->read_block(block, data);
        if (status != Reader::OK) {
            finish();
            return result(false, status, block / 4, "read failed");
        }
        bambu::parse_block(block, data, info);
    }
    finish();
    return result(true, Reader::OK, 0xFF, "ok");
}

Result dump(Card &card, uint8_t *image, bool *ok_blocks) {
    memset(image, 0, 1024);
    memset(ok_blocks, 0, 64);
    if (!begin_session(card))
        return result(false, Reader::NO_CARD, 0xFF, "tag removed");
    KeyChoice key = card.kind == BLANK ? KEY_DEFAULT : KEY_DERIVED;
    bool all_ok = true;
    for (uint8_t sector = 0; sector < bambu::SECTORS; sector++) {
        bool open = open_sector(card, sector, key) == Reader::OK;
        for (uint8_t i = 0; i < 4; i++) {
            uint8_t block = sector * 4 + i;
            uint8_t *data = image + block * 16;
            bool ok = open && reader->read_block(block, data) == Reader::OK;
            if (ok && bambu::is_trailer(block))  // key A always reads back as zeros
                memcpy(data, key == KEY_DERIVED ? card.keys[sector] : bambu::DEFAULT_KEY, 6);
            if (!ok && open)
                open = reselect(card) && open_sector(card, sector, key) == Reader::OK;
            ok_blocks[block] = ok;
            all_ok &= ok;
        }
    }
    finish();
    return result(all_ok, Reader::OK, 0xFF, all_ok ? "ok" : "some blocks unreadable");
}

static void make_tray_uid(uint8_t out[16]) {
    // Any unique 16 bytes: OpenAMS matches it against Spoolman's tray_uid.
    esp_fill_random(out, 16);
}

Result write(Card &card, const bambu::Spool &spool, const char *date, Progress progress) {
    if (!have_master_key)
        return result(false, Reader::OK, 0xFF, "no Bambu master key set");
    switch (card.kind) {
    case BLANK:
    case CUSTOM:
        break;
    case GENUINE:
        return result(false, Reader::OK, 0xFF, "genuine Bambu tags are read-only");
    case LOCKED:
        return result(false, Reader::OK, 0xFF, "tag is locked read-only");
    case UNSUPPORTED:
        return result(false, Reader::OK, 0xFF, "needs a MIFARE Classic 1K tag");
    default:
        return result(false, Reader::OK, 0xFF, "tag uses unknown keys");
    }
    if (!begin_session(card))
        return result(false, Reader::NO_CARD, 0xFF, "tag removed");

    uint8_t tray_uid[16];
    make_tray_uid(tray_uid);
    uint8_t data[16];
    KeyChoice key = card.kind == CUSTOM ? KEY_DERIVED : KEY_DEFAULT;
    for (uint8_t sector = 0; sector < bambu::SECTORS; sector++) {
        // A data or trailer write can NAK transiently at a sector boundary
        // (seen right after the previous sector's key change: the nested
        // authentication of the next sector is flaky). The trailer is always
        // written last, so a failed data block leaves this sector's key
        // unchanged -- reselect for a clean, non-nested auth and retry the
        // whole sector. Writes are idempotent, so re-doing earlier blocks is
        // harmless.
        Status status = Reader::OK;
        const int SECTOR_TRIES = 3;
        for (int attempt = 1; attempt <= SECTOR_TRIES; attempt++) {
            status = open_sector(card, sector, key);
            if (status == Reader::OK) {
                for (uint8_t block = sector * 4; block < sector * 4 + 3; block++) {
                    if (block == 0)
                        continue;  // manufacturer block
                    bambu::build_block(spool, block, tray_uid, date, data);
                    status = reader->write_block(block, data);
                    if (status != Reader::OK)
                        break;
                }
            }
            if (status == Reader::OK) {
                // The trailer goes last: the new keys apply from the next auth.
                bambu::build_trailer(card.keys[sector], data);
                status = reader->write_block(sector * 4 + 3, data);
            }
            if (status == Reader::OK)
                break;
            if (attempt < SECTOR_TRIES && !reselect(card)) {
                finish();
                return result(false, Reader::NO_CARD, sector, "tag removed");
            }
        }
        if (status != Reader::OK) {
            finish();
            return result(false, status, sector, "write failed");
        }
        if (progress)
            progress(sector + 1, 2 * bambu::SECTORS);
    }
    card.kind = CUSTOM;

    // Verify the way the AMS reads: derived key A only.
    key = KEY_DERIVED;
    for (uint8_t sector = 0; sector < bambu::SECTORS; sector++) {
        Status status = open_sector(card, sector, key, false);
        if (status != Reader::OK) {
            finish();
            return result(false, status, sector, "verify: key rejected");
        }
        for (uint8_t block = sector * 4; block < sector * 4 + 3; block++) {
            if (block == 0)
                continue;
            uint8_t expect[16];
            bambu::build_block(spool, block, tray_uid, date, expect);
            status = reader->read_block(block, data);
            if (status != Reader::OK || memcmp(data, expect, 16)) {
                finish();
                return result(false, status, sector, "verify: data mismatch");
            }
        }
        if (progress)
            progress(bambu::SECTORS + sector + 1, 2 * bambu::SECTORS);
    }
    finish();
    return result(true, Reader::OK, 0xFF, "written and verified");
}

Result erase(Card &card, Progress progress) {
    if (card.kind != BLANK && card.kind != CUSTOM)
        return result(false, Reader::OK, 0xFF, "only blank or custom tags can be reset");
    if (!begin_session(card))
        return result(false, Reader::NO_CARD, 0xFF, "tag removed");
    static const uint8_t ZERO[16] = {0};
    uint8_t trailer[16];
    memcpy(trailer, bambu::DEFAULT_KEY, 6);
    memcpy(trailer + 6, bambu::ACCESS_WRITABLE, 4);
    memcpy(trailer + 10, bambu::DEFAULT_KEY, 6);
    KeyChoice key = card.kind == CUSTOM ? KEY_DERIVED : KEY_DEFAULT;
    for (uint8_t sector = 0; sector < bambu::SECTORS; sector++) {
        // Same transient sector-boundary NAK as write(): reselect for a clean
        // auth and retry the whole sector. The trailer (key reset to factory)
        // is written last, so a failed data block leaves this sector's key
        // unchanged; writing zeros is idempotent.
        Status status = Reader::OK;
        const int SECTOR_TRIES = 3;
        for (int attempt = 1; attempt <= SECTOR_TRIES; attempt++) {
            status = open_sector(card, sector, key);
            if (status == Reader::OK) {
                for (uint8_t block = sector * 4; block < sector * 4 + 3; block++) {
                    if (block == 0)
                        continue;  // manufacturer block
                    status = reader->write_block(block, ZERO);
                    if (status != Reader::OK)
                        break;
                }
            }
            if (status == Reader::OK)
                status = reader->write_block(sector * 4 + 3, trailer);
            if (status == Reader::OK)
                break;
            if (attempt < SECTOR_TRIES && !reselect(card)) {
                finish();
                return result(false, Reader::NO_CARD, sector, "tag removed");
            }
        }
        if (status != Reader::OK) {
            finish();
            return result(false, status, sector, "write failed");
        }
        if (progress)
            progress(sector + 1, bambu::SECTORS);
    }
    card.kind = BLANK;
    finish();
    return result(true, Reader::OK, 0xFF, "reset to blank");
}

// --------------------------------------------------------------- cloning

bool image_is_valid_bambu(const uint8_t image[1024], bambu::TagInfo &info,
                          const char **why) {
    // Block 0: 4-byte UID then its BCC (XOR). (7-byte UID tags would differ,
    // but every Bambu tag seen is a 4-byte-UID MIFARE 1K.)
    uint8_t bcc = image[0] ^ image[1] ^ image[2] ^ image[3];
    if (bcc != image[4]) {
        *why = "block 0 UID checksum is wrong";
        return false;
    }
    uint8_t keys[bambu::SECTORS][bambu::KEY_SIZE];
    bambu::derive_keys(master_key, image, 4, keys);
    if (have_master_key) {
        for (uint8_t s = 0; s < bambu::SECTORS; s++) {
            if (memcmp(image + (s * 4 + 3) * 16, keys[s], bambu::KEY_SIZE)) {
                *why = "sector keys are not this tag's derived keys";
                return false;
            }
        }
    }
    memset(&info, 0, sizeof(info));
    static const uint8_t blocks[] = {1, 2, 4, 5, 6, 8, 9, 10, 12, 13, 14, 16};
    for (uint8_t b : blocks)
        bambu::parse_block(b, image + b * 16, info);
    if (!info.type[0] && !info.detail[0]) {
        *why = "no filament data in the image";
        return false;
    }
    *why = "ok";
    return true;
}

static void log_probe(const char *name, Status status) {
    printf("magic: %s %s\n", name, Reader::status_name(status));
}

static void log_hex16(const char *name, const uint8_t data[16]) {
    printf("magic: %s", name);
    for (uint8_t i = 0; i < 16; i++)
        printf(" %02X", data[i]);
    printf("\n");
}

static bool recover_after_uid_write(Card &card, const uint8_t new_uid[4]) {
    reader->stop_crypto();
    reader->halt();
    reader->antenna(false);
    vTaskDelay(pdMS_TO_TICKS(20));
    reader->antenna(true);
    vTaskDelay(pdMS_TO_TICKS(10));
    memcpy(card.uid.bytes, new_uid, 4);
    card.uid.size = 4;
    return reselect(card);
}

Reader::Magic magic_detect(Card &card) {
    // Probe gen2/gen3 before gen1a: sending 0x40 to a CUID/gen3 tag can leave
    // it in a state where the later authenticated write NAKs. Prefer types the
    // AMS will actually accept (it rejects gen1a).
    uint8_t block0[16];
    memset(block0, 0, sizeof(block0));
    if (card.uid.size == 4)
        memcpy(block0, card.uid.bytes, 4);

    // Gen3: after a plain select, block 0 can be read with no Crypto1.
    reader->stop_crypto();
    if (reselect(card)) {
        Status st = reader->read_block(0, block0);
        log_probe("gen3 unauth read", st);
        if (st == Reader::OK) {
            finish();
            return Reader::MAGIC_GEN3;
        }
    }

    // Read block 0 under the factory key so the gen2/gen3 probes rewrite the
    // same bytes (UID unchanged either way).
    bool have_block0 = false;
    reader->stop_crypto();
    if (reselect(card)) {
        KeyChoice key = KEY_DEFAULT;
        have_block0 = open_sector(card, 0, key) == Reader::OK &&
                      reader->read_block(0, block0) == Reader::OK;
    }
    if (!have_block0)
        printf("magic: gen2 auth/read failed\n");
    else
        log_hex16("block0", block0);

    // Gen2 / CUID: authenticated rewrite of the bytes already in block 0.
    // A normal tag NAKs. Some clones need longer than the default 25 ms.
    if (have_block0) {
        vTaskDelay(pdMS_TO_TICKS(5));
        reader->set_rx_timeout_ms(100);
        Status st = reader->write_block(0, block0);
        reader->set_rx_timeout_ms(25);
        log_probe("gen2 block0 write", st);
        if (st == Reader::OK) {
            finish();
            return Reader::MAGIC_GEN2;
        }
    }

    // Gen3 fallback: some cards NAK the unauth read but accept 90 F0 / 90 FB.
    reader->stop_crypto();
    reader->halt();
    if (have_block0 && reselect(card)) {
        Status st = reader->magic_gen3_write_block0(block0, 300);
        log_probe("gen3 APDU write", st);
        if (st == Reader::OK) {
            finish();
            return Reader::MAGIC_GEN3;
        }
    }
    reader->stop_crypto();
    if (reselect(card) && card.uid.size == 4) {
        Status st = reader->magic_gen3_write_uid(card.uid.bytes, 4, 300);
        log_probe("gen3 UID write", st);
        if (st == Reader::OK) {
            finish();
            return Reader::MAGIC_GEN3;
        }
    }

    if (reader->magic_is_gen1a()) {
        printf("magic: gen1a backdoor ok\n");
        finish();
        return Reader::MAGIC_GEN1A;
    }
    printf("magic: gen1a backdoor no\n");

    // Gen4 GDM / USCUID. AliExpress "CUID / sector 0 writable" cards often
    // ship with CUID off, so the ordinary block-0 write NAKs. Two entries:
    // wakeup 20/23 (if enabled) and magic-auth 0x80 + zero key.
    Status st = reader->magic_gdm_open();
    log_probe("gdm wakeup 20/23", st);
    if (st == Reader::OK && have_block0) {
        reader->set_rx_timeout_ms(100);
        st = reader->write_block(0, block0);
        reader->set_rx_timeout_ms(25);
        log_probe("gdm wakeup block0 write", st);
        if (st == Reader::OK) {
            finish();
            return Reader::MAGIC_GEN4;
        }
    }

    reader->stop_crypto();
    reader->halt();
    reader->antenna(false);
    vTaskDelay(pdMS_TO_TICKS(20));
    reader->antenna(true);
    vTaskDelay(pdMS_TO_TICKS(10));
    if (reselect(card)) {
        uint8_t nonce[8];
        uint8_t nlen = 0, nbits = 0;
        reader->magic_gdm_probe(nonce, &nlen, &nbits);
    }
    reader->stop_crypto();
    if (reselect(card)) {
        st = reader->magic_gdm_auth(card.uid);
        log_probe("gdm magic auth", st);
        if (st == Reader::OK) {
            uint8_t cfg[16];
            st = reader->read_block_ex(0xE0, 0, cfg);
            log_probe("gdm cfg read", st);
            if (st == Reader::OK)
                log_hex16("gdm cfg", cfg);

            uint8_t hid[16];
            st = reader->read_block_ex(0x38, 0, hid);
            log_probe("gdm hidden read", st);
            bool have_hid = st == Reader::OK;
            if (have_hid)
                log_hex16("gdm hid0", hid);

            if (have_block0) {
                vTaskDelay(pdMS_TO_TICKS(5));
                reader->set_rx_timeout_ms(100);
                st = reader->write_block(0, block0);
                reader->set_rx_timeout_ms(25);
                log_probe("gdm auth A0 block0", st);
                if (st == Reader::OK) {
                    finish();
                    return Reader::MAGIC_GEN4;
                }
            }

            if (have_hid) {
                reader->set_rx_timeout_ms(100);
                st = reader->write_block_ex(0xA8, 0, hid);
                reader->set_rx_timeout_ms(25);
                log_probe("gdm auth A8 hid0 rewrite", st);
            }
        }
    }

    reader->stop_crypto();
    if (reselect(card)) {
        uint8_t g4[16];
        st = reader->magic_gen4_read(0, g4);
        log_probe("gen4 GTU read", st);
        if (st == Reader::OK) {
            log_hex16("gen4 blk0", g4);
            finish();
            return Reader::MAGIC_GEN4;
        }
    }

    // Some clones only accept block-0 writes under key B.
    if (have_block0) {
        reader->stop_crypto();
        if (reselect(card)) {
            st = reader->authenticate(3, bambu::DEFAULT_KEY, card.uid, true);
            log_probe("key B auth", st);
            if (st == Reader::OK) {
                reader->set_rx_timeout_ms(100);
                st = reader->write_block(0, block0);
                reader->set_rx_timeout_ms(25);
                log_probe("key B block0 write", st);
                if (st == Reader::OK) {
                    finish();
                    return Reader::MAGIC_GEN2;
                }
            }
        }
    }

    // Sanity: if block 1 rewrites, the silicon is not fully read-only.
    reader->stop_crypto();
    if (reselect(card)) {
        KeyChoice key = KEY_DEFAULT;
        if (open_sector(card, 0, key) == Reader::OK) {
            uint8_t b1[16];
            if (reader->read_block(1, b1) == Reader::OK) {
                reader->set_rx_timeout_ms(100);
                st = reader->write_block(1, b1);
                reader->set_rx_timeout_ms(25);
                log_probe("block1 rewrite", st);
            } else {
                printf("magic: block1 read failed\n");
            }
        } else {
            printf("magic: block1 auth failed\n");
        }
    }

    finish();
    return Reader::MAGIC_NONE;
}

Result capture(Card &card, uint8_t image[1024], bool ok[64]) {
    if (card.kind != CUSTOM && card.kind != GENUINE && card.kind != LOCKED)
        return result(false, Reader::OK, 0xFF, "not a Bambu-format tag");
    // dump() reads block 0 (UID + BCC + manufacturer bytes) under the derived
    // key, which is exactly the block a clone must reproduce.
    Result r = dump(card, image, ok);
    bool all = true;
    for (uint8_t b = 0; b < 64; b++)
        all &= ok[b];
    if (!all)
        return result(false, Reader::OK, 0xFF, "some blocks would not read");
    return r;
}

// Writes every block of `image` to the current gen1a tag through the backdoor.
static Result clone_gen1a(const uint8_t image[1024], Progress progress) {
    for (uint8_t block = 0; block < 64; block++) {
        if (reader->magic_backdoor_open() != Reader::OK)
            return result(false, Reader::NAK, block / 4, "backdoor closed mid-write");
        Status status = reader->write_block(block, image + block * 16);
        if (status != Reader::OK)
            return result(false, status, block / 4, "backdoor write failed");
        if (progress)
            progress(block + 1, 128);
    }
    return result(true, Reader::OK, 0xFF, "ok");
}

// After block 0 (the UID) is in place, write the rest of the image with the
// factory key. Block 0 is skipped: it was already written, and a second write
// would fail on FUID / fuse-on-first-write cards.
//
// Opens `sector` with the factory FF key and writes either its data blocks
// (trailer == false, skipping block 0) or just its trailer (trailer == true).
// Each attempt does a clean, non-nested auth (reselect first), which is the
// fix for the sector-boundary flakiness write() documents: a nested auth right
// after a key change can NAK or drop the card. Writes are idempotent, so a
// retried block is harmless.
static Status write_sector_part(Card &card, uint8_t sector, const uint8_t image[1024],
                                bool trailer) {
    const int SECTOR_TRIES = 4;
    Status status = Reader::NO_CARD;
    for (int attempt = 1; attempt <= SECTOR_TRIES; attempt++) {
        if (!reselect(card)) {  // halt + fresh select: never a nested auth
            status = Reader::NO_CARD;
            vTaskDelay(pdMS_TO_TICKS(5));
            continue;
        }
        KeyChoice key = KEY_DEFAULT;  // a blank magic tag ships with FF keys
        status = open_sector(card, sector, key);
        if (status == Reader::OK) {
            uint8_t lo = trailer ? 3 : 0;
            uint8_t hi = trailer ? 4 : 3;
            for (uint8_t i = lo; i < hi; i++) {
                uint8_t block = sector * 4 + i;
                if (block == 0)
                    continue;  // UID block, written by the gen2/gen3/etc. step
                status = reader->write_block(block, image + block * 16);
                if (status != Reader::OK)
                    break;
            }
        }
        if (status == Reader::OK)
            return Reader::OK;
        vTaskDelay(pdMS_TO_TICKS(5));
    }
    return status;
}

// Two passes so a dropout never half-locks the tag: every sector's DATA blocks
// first (all sectors stay on factory FF keys, freely re-openable on retry),
// then every TRAILER last. A failure during the data pass leaves the tag fully
// blank and reusable; only the short trailer pass actually locks it. This both
// stops a mid-clone glitch from bricking a (write-once) tag and avoids the
// mid-stream key changes that were destabilising the nested auth.
static Result clone_rest(Card &card, const uint8_t image[1024], Progress progress) {
    reader->set_rx_timeout_ms(100);
    for (uint8_t sector = 0; sector < bambu::SECTORS; sector++) {
        Status status = write_sector_part(card, sector, image, false);
        if (status != Reader::OK) {
            reader->set_rx_timeout_ms(25);
            return result(false, status, sector, "write failed");
        }
        if (progress)
            progress((sector + 1) * 2, 128);  // 2..32 across the data pass
    }
    for (uint8_t sector = 0; sector < bambu::SECTORS; sector++) {
        Status status = write_sector_part(card, sector, image, true);
        if (status != Reader::OK) {
            reader->set_rx_timeout_ms(25);
            return result(false, status, sector, "trailer write failed");
        }
        if (progress)
            progress(32 + (sector + 1) * 2, 128);  // 34..64 across the trailer pass
    }
    reader->set_rx_timeout_ms(25);
    return result(true, Reader::OK, 0xFF, "ok");
}

// Writes `image` to a gen2 tag: block 0 goes out with the ordinary write,
// which gen2 allows. The UID change kills the Crypto1 session, so we halt,
// field-reset, and re-select under the new UID before the rest of the image.
static Result clone_gen2(Card &card, const uint8_t image[1024], Progress progress) {
    if (!begin_session(card))
        return result(false, Reader::NO_CARD, 0xFF, "tag removed");
    KeyChoice key = KEY_DEFAULT;
    if (open_sector(card, 0, key) != Reader::OK)
        return result(false, Reader::AUTH_FAILED, 0, "cannot open magic sector");
    vTaskDelay(pdMS_TO_TICKS(5));
    reader->set_rx_timeout_ms(100);
    Status status = reader->write_block(0, image);
    reader->set_rx_timeout_ms(25);
    if (status != Reader::OK)
        return result(false, status, 0, "block 0 not writable (not gen2?)");
    if (progress)
        progress(1, 128);
    if (!recover_after_uid_write(card, image))
        return result(false, Reader::NO_CARD, 0, "tag lost after UID write");
    return clone_rest(card, image, progress);
}

// Gen3: manufacturer block via 90 F0 APDU, then ordinary writes for the rest.
static Result clone_gen3(Card &card, const uint8_t image[1024], Progress progress) {
    reader->stop_crypto();
    if (!reselect(card))
        return result(false, Reader::NO_CARD, 0xFF, "tag removed");
    Status status = reader->magic_gen3_write_block0(image);
    if (status != Reader::OK)
        return result(false, status, 0, "gen3 block 0 write failed");
    if (progress)
        progress(1, 128);
    if (!recover_after_uid_write(card, image))
        return result(false, Reader::NO_CARD, 0, "tag lost after UID write");
    return clone_rest(card, image, progress);
}

// Gen4 GDM: prefer magic-auth then A0 (works with wakeup disabled, which is
// how AliExpress "CUID" GDM cards often ship). Fall back to 20/23 wakeup, then
// to GTU CF backdoor writes.
static Result clone_gen4(Card &card, const uint8_t image[1024], Progress progress) {
    reader->stop_crypto();
    if (reselect(card) && reader->magic_gdm_auth(card.uid) == Reader::OK) {
        reader->set_rx_timeout_ms(100);
        Status status = reader->write_block(0, image);
        reader->set_rx_timeout_ms(25);
        if (status == Reader::OK) {
            if (progress)
                progress(1, 128);
            if (!recover_after_uid_write(card, image))
                return result(false, Reader::NO_CARD, 0, "tag lost after UID write");
            if (reader->magic_gdm_auth(card.uid) == Reader::OK) {
                reader->set_rx_timeout_ms(100);
                for (uint8_t block = 1; block < 64; block++) {
                    status = reader->write_block(block, image + block * 16);
                    if (status != Reader::OK) {
                        reader->set_rx_timeout_ms(25);
                        return result(false, status, block / 4, "gen4 write failed");
                    }
                    if (progress)
                        progress(block + 1, 128);
                }
                reader->set_rx_timeout_ms(25);
                return result(true, Reader::OK, 0xFF, "ok");
            }
            return clone_rest(card, image, progress);
        }
        reader->stop_crypto();
    }

    if (reader->magic_gdm_open() == Reader::OK) {
        for (uint8_t block = 0; block < 64; block++) {
            if (reader->magic_gdm_open() != Reader::OK)
                return result(false, Reader::NAK, block / 4, "gdm backdoor closed mid-write");
            Status status = reader->write_block(block, image + block * 16);
            if (status != Reader::OK)
                return result(false, status, block / 4, "gdm write failed");
            if (progress)
                progress(block + 1, 128);
        }
        return result(true, Reader::OK, 0xFF, "ok");
    }

    reader->stop_crypto();
    if (!reselect(card))
        return result(false, Reader::NO_CARD, 0xFF, "tag removed");
    for (uint8_t block = 0; block < 64; block++) {
        Status status = reader->magic_gen4_write(block, image + block * 16);
        if (status != Reader::OK)
            return result(false, status, block / 4, "gen4 GTU write failed");
        if (progress)
            progress(block + 1, 128);
        if (block == 0 && !recover_after_uid_write(card, image))
            return result(false, Reader::NO_CARD, 0, "tag lost after UID write");
    }
    return result(true, Reader::OK, 0xFF, "ok");
}

Result clone(Card &card, const uint8_t image[1024], Progress progress) {
    bambu::TagInfo info;
    const char *why;
    if (!image_is_valid_bambu(image, info, &why))
        return result(false, Reader::OK, 0xFF, why);

    Reader::Magic magic = magic_detect(card);
    Result r;
    if (magic == Reader::MAGIC_GEN1A)
        r = clone_gen1a(image, progress);
    else if (magic == Reader::MAGIC_GEN2)
        r = clone_gen2(card, image, progress);
    else if (magic == Reader::MAGIC_GEN3)
        r = clone_gen3(card, image, progress);
    else if (magic == Reader::MAGIC_GEN4)
        r = clone_gen4(card, image, progress);
    else
        return result(false, Reader::OK, 0xFF,
                      "UID is read-only — need a CUID/gen2/gen3 magic tag");
    if (!r.ok)
        return r;

    // Verify as the AMS will: the tag must now select by the genuine UID and
    // read back every block under the derived key A. A field reset lets gen2
    // / gen3 chips start advertising the UID we just wrote.
    reader->stop_crypto();
    reader->halt();
    reader->antenna(false);
    vTaskDelay(pdMS_TO_TICKS(20));
    reader->antenna(true);
    vTaskDelay(pdMS_TO_TICKS(10));
    CardUid uid;
    Card written;
    memset(&written, 0, sizeof(written));
    bool found = false;
    for (uint8_t attempt = 0; attempt < 5 && !found; attempt++)
        found = reader->select(uid) == Reader::OK;
    if (!found || uid.size != 4 || memcmp(uid.bytes, image, 4))
        return result(false, Reader::NO_CARD, 0xFF, "clone did not take (UID mismatch)");
    written.uid = uid;
    written.kind = CUSTOM;
    bambu::derive_keys(master_key, image, 4, written.keys);

    KeyChoice key = KEY_DERIVED;
    uint8_t data[16];
    for (uint8_t sector = 0; sector < bambu::SECTORS; sector++) {
        if (open_sector(written, sector, key, false) != Reader::OK) {
            finish();
            return result(false, Reader::AUTH_FAILED, sector, "verify: derived key rejected");
        }
        for (uint8_t i = 0; i < 3; i++) {
            uint8_t block = sector * 4 + i;
            if (reader->read_block(block, data) != Reader::OK ||
                    memcmp(data, image + block * 16, 16)) {
                finish();
                return result(false, Reader::OK, sector, "verify: data mismatch");
            }
        }
        if (progress)
            progress(64 + sector * 4 + 4, 128);
    }
    finish();
    return result(true, Reader::OK, 0xFF, "cloned and verified");
}

const char *kind_name(Kind kind) {
    switch (kind) {
    case NONE: return "no tag";
    case UNSUPPORTED: return "not MIFARE Classic";
    case BLANK: return "blank MIFARE 1K";
    case CUSTOM: return "custom spool tag";
    case GENUINE: return "genuine Bambu tag";
    case LOCKED: return "locked Bambu-format tag";
    case UNKNOWN_KEYS: return "unknown keys";
    }
    return "?";
}

bool is_writable(Kind kind) { return kind == BLANK || kind == CUSTOM; }

void format_uid(const CardUid &uid, char *out) {
    for (uint8_t i = 0; i < uid.size; i++)
        sprintf(out + i * 2, "%02X", uid.bytes[i]);
    out[uid.size * 2] = 0;
}

}  // namespace tagops

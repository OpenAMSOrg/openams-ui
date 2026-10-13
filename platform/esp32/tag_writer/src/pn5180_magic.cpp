// Copyright (C) 2026 JR Lomas <lomas.jr@gmail.com>
// Ported from the author's earlier MFRC522 tag-writer (GPLv3).
// This file may be distributed under the terms of the GNU GPLv3 license.

#include "pn5180.h"

#include <stdio.h>
#include <string.h>

static const uint8_t DEFAULT_KEY[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

static inline bool is_ack(const uint8_t *out, size_t out_len) {
    return out_len > 0 && (out[0] & 0x0F) == 0x0A;
}

const char *Pn5180::magic_name(Magic m) {
    switch (m) {
    case MAGIC_GEN1A: return "gen1a (UID backdoor)";
    case MAGIC_GEN2:  return "gen2 / CUID (direct UID write)";
    case MAGIC_GEN3:  return "gen3 (APDU UID write)";
    case MAGIC_GEN4:  return "gen4 / GDM (magic-auth UID write)";
    default:          return "normal (UID read-only)";
    }
}

Pn5180::Status Pn5180::magic_gen1a_open() {
    stop_crypto();
    halt();
    set_crc(false, false);

    uint8_t wupa = 0x52;
    uint8_t buf[16];
    size_t rlen = sizeof(buf);
    uint8_t last_bits = 0;

    Status st = transceive(&wupa, 1, 7, buf, &rlen, &last_bits, 10);
    if (st == NO_CARD) {
        return NO_CARD;
    }
    // Collision is ignored during WUPA, as in Python

    uint8_t cmd40 = 0x40;
    rlen = sizeof(buf);
    last_bits = 0;
    st = transceive(&cmd40, 1, 7, buf, &rlen, &last_bits, 20);
    if (st != OK || !is_ack(buf, rlen)) {
        return (st == NO_CARD) ? NO_CARD : NAK;
    }

    uint8_t cmd43 = 0x43;
    rlen = sizeof(buf);
    last_bits = 0;
    st = transceive(&cmd43, 1, 0, buf, &rlen, &last_bits, 20);
    if (st != OK || !is_ack(buf, rlen)) {
        return (st == NO_CARD) ? NO_CARD : NAK;
    }

    return OK;
}

Pn5180::Status Pn5180::magic_gen1a_write_block(uint8_t block, const uint8_t data[16]) {
    set_crc(true, false);
    Status result = OK;

    uint8_t cmd[2] = {0xA0, block};
    uint8_t buf[16];
    size_t rlen = sizeof(buf);
    uint8_t last_bits = 0;

    Status st = transceive(cmd, 2, 0, buf, &rlen, &last_bits, 20);
    if (st != OK && st != NO_CARD && rlen == 0) {
        result = st;
    } else if (st == NO_CARD && rlen == 0) {
        result = NO_CARD;
    } else if (rlen > 0 && !is_ack(buf, rlen)) {
        result = NAK;
    }

    if (result == OK) {
        rlen = sizeof(buf);
        last_bits = 0;
        st = transceive(data, 16, 0, buf, &rlen, &last_bits, 30);
        if (st != OK && st != NO_CARD && rlen == 0) {
            result = st;
        } else if (st == NO_CARD && rlen == 0) {
            result = NO_CARD;
        } else if (rlen > 0 && !is_ack(buf, rlen)) {
            result = NAK;
        }
    }

    set_crc(true, true);
    return result;
}

Pn5180::Status Pn5180::magic_gen3_write_block0(const uint8_t data[16],
                                               uint16_t timeout_ms) {
    (void)timeout_ms;   // the PN5180 transceive uses its own fixed timeout
    stop_crypto();
    set_crc(true, true);

    uint8_t frame[21];
    frame[0] = 0x90;
    frame[1] = 0xF0;
    frame[2] = 0xCC;
    frame[3] = 0xCC;
    frame[4] = 0x10;
    memcpy(&frame[5], data, 16);

    uint8_t buf[16];
    size_t rlen = sizeof(buf);
    uint8_t last_bits = 0;

    Status st = transceive(frame, sizeof(frame), 0, buf, &rlen, &last_bits, 100);
    if (st != OK && st != NO_CARD && rlen == 0) {
        return st;
    }
    if (st == NO_CARD && rlen == 0) {
        return NO_CARD;
    }
    if (is_ack(buf, rlen)) {
        return OK;
    }
    if (rlen >= 2 && buf[0] == 0x90 && buf[1] == 0x00) {
        return OK;
    }
    return NAK;
}

Pn5180::Magic Pn5180::magic_detect() {
    CardUid uid;
    stop_crypto();
    halt();
    if (select(uid) != OK) {
        if (magic_gen1a_open() == OK) {
            return MAGIC_GEN1A;
        }
        return MAGIC_NONE;
    }

    // 1. Gen3: unauthenticated block-0 read
    uint8_t block0[16];
    bool gen3_unauth = false;
    if (reselect(uid)) {
        stop_crypto();
        if (read_block(0, block0) == OK) {
            gen3_unauth = true;
        }
    }
    if (gen3_unauth) {
        return MAGIC_GEN3;
    }

    // 2. Read block 0 under factory key
    bool have_block0 = false;
    if (reselect(uid)) {
        if (authenticate(0, DEFAULT_KEY, uid, false) == OK) {
            if (read_block(0, block0) == OK) {
                have_block0 = true;
            }
        }
    }

    // 3. Gen2 / CUID: authenticated rewrite of block 0
    if (have_block0) {
        if (write_block(0, block0) == OK) {
            return MAGIC_GEN2;
        }
    }

    // 4. Gen3 fallback: APDU write of block 0
    if (have_block0 && reselect(uid)) {
        if (magic_gen3_write_block0(block0) == OK) {
            return MAGIC_GEN3;
        }
    }

    // 5. Gen1a backdoor last
    if (magic_gen1a_open() == OK) {
        return MAGIC_GEN1A;
    }

    return MAGIC_NONE;
}

Pn5180::Status Pn5180::clone_image(const uint8_t image[1024],
                                   void (*derive_key_a)(const uint8_t uid[4],
                                                        int sector,
                                                        uint8_t out[6])) {
    if (!image || !derive_key_a) {
        return ERROR;
    }

    Magic kind = magic_detect();
    if (kind == MAGIC_NONE) {
        printf("magic: UID is read-only — need a CUID/gen2 or gen3 magic tag\n");
        return NAK;
    }

    if (kind == MAGIC_GEN1A) {
        for (uint8_t block = 0; block < 64; block++) {
            Status st = magic_gen1a_open();
            if (st != OK) {
                printf("magic: gen1a backdoor closed at block %d\n", block);
                return (st == NO_CARD) ? NO_CARD : NAK;
            }
            st = magic_gen1a_write_block(block, &image[block * 16]);
            if (st != OK) {
                printf("magic: gen1a write failed at block %d\n", block);
                return st;
            }
        }
    } else if (kind == MAGIC_GEN2) {
        CardUid card_uid;
        stop_crypto();
        halt();
        if (select(card_uid) != OK) {
            printf("magic: tag lost before sector 0\n");
            return NO_CARD;
        }
        if (authenticate(0, DEFAULT_KEY, card_uid, false) != OK) {
            printf("magic: cannot open magic sector 0\n");
            return AUTH_FAILED;
        }
        Status st = write_block(0, &image[0]);
        if (st != OK) {
            printf("magic: gen2 block 0 write failed\n");
            return st;
        }

        CardUid new_card;
        if (!field_reset(&image[0], new_card)) {
            printf("magic: tag lost after UID write\n");
            return NO_CARD;
        }

        for (uint8_t sector = 0; sector < 16; sector++) {
            uint8_t first = sector * 4;
            if (authenticate(first, DEFAULT_KEY, new_card, false) != OK) {
                printf("magic: cannot open magic sector %d\n", sector);
                return AUTH_FAILED;
            }
            for (uint8_t b = first; b < first + 4; b++) {
                if (b == 0) {
                    continue;
                }
                st = write_block(b, &image[b * 16]);
                if (st != OK) {
                    printf("magic: write failed at block %d\n", b);
                    return st;
                }
            }
        }
    } else if (kind == MAGIC_GEN3) {
        CardUid card_uid;
        stop_crypto();
        halt();
        if (select(card_uid) != OK) {
            printf("magic: tag lost before block 0\n");
            return NO_CARD;
        }
        Status st = magic_gen3_write_block0(&image[0]);
        if (st != OK) {
            printf("magic: gen3 block 0 write failed\n");
            return st;
        }

        CardUid new_card;
        if (!field_reset(&image[0], new_card)) {
            printf("magic: tag lost after UID write\n");
            return NO_CARD;
        }

        for (uint8_t sector = 0; sector < 16; sector++) {
            uint8_t first = sector * 4;
            if (authenticate(first, DEFAULT_KEY, new_card, false) != OK) {
                printf("magic: cannot open magic sector %d\n", sector);
                return AUTH_FAILED;
            }
            for (uint8_t b = first; b < first + 4; b++) {
                if (b == 0) {
                    continue;
                }
                st = write_block(b, &image[b * 16]);
                if (st != OK) {
                    printf("magic: write failed at block %d\n", b);
                    return st;
                }
            }
        }
    } else {
        return NAK;
    }

    // Verify under derived keys
    CardUid verify_card;
    if (!field_reset(&image[0], verify_card)) {
        printf("magic: verify tag lost (UID mismatch)\n");
        return NO_CARD;
    }

    Status verify_status = OK;
    for (uint8_t sector = 0; sector < 16; sector++) {
        uint8_t key_a[6];
        derive_key_a(&image[0], sector, key_a);
        uint8_t first = sector * 4;
        if (authenticate(first, key_a, verify_card, false) != OK) {
            printf("magic: verify derived key rejected at sector %d\n", sector);
            verify_status = AUTH_FAILED;
            break;
        }
        for (uint8_t b = first; b < first + 3; b++) {
            uint8_t got[16];
            Status st = read_block(b, got);
            if (st != OK) {
                printf("magic: verify read failed at block %d\n", b);
                verify_status = st;
                break;
            }
            if (memcmp(got, &image[b * 16], 16) != 0) {
                printf("magic: verify data mismatch at block %d\n", b);
                verify_status = ERROR;
                break;
            }
        }
        if (verify_status != OK) {
            break;
        }
        uint8_t trailer = first + 3;
        uint8_t got[16];
        Status st = read_block(trailer, got);
        if (st != OK) {
            printf("magic: verify read failed at trailer block %d\n", trailer);
            verify_status = st;
            break;
        }
        if (memcmp(&got[6], &image[trailer * 16 + 6], 4) != 0) {
            printf("magic: verify trailer access bits mismatch at block %d\n", trailer);
            verify_status = ERROR;
            break;
        }
    }

    stop_crypto();
    return verify_status;
}

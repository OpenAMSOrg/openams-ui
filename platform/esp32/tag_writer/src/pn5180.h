// Copyright (C) 2026 JR Lomas <lomas.jr@gmail.com>
// This file may be distributed under the terms of the GNU GPLv3 license.

// PN5180 NFC front-end driver for the spool tag writer.
//
// A C++ port of the klipper_rf PN5180 driver (its pn5180_chip.py host
// interface, iso14443a.py for ISO 14443-A and MIFARE Classic, magic.py for
// UID-writable "magic" cards: same command and register sequences, bit masks,
// timing and error conditions), behind the same public interface as the
// MFRC522 driver (mfrc522.h), so tag_ops / tag_service / ui work with either
// reader (reader.h).
//
//   * The public method names, the Status codes and CardUid match mfrc522.h.
//     Where the PN5180 needs a signal the MFRC522 does not (BUSY), it is in
//     Pins.
//
// SPI is behind the Pn5180Spi interface so the protocol logic compiles and is
// unit-tested on the host (g++, no ESP-IDF) against a simulator, exactly as
// klipper_rf is. begin() wires the real ESP-IDF backend; tests inject a sim.
#pragma once

#include <stdint.h>
#include <stddef.h>

#ifdef ESP_PLATFORM
// For spi_host_device_t / SPI2_HOST / SPI3_HOST, as mfrc522.h provided. Kept
// behind ESP_PLATFORM so the driver still compiles on the host for tests.
#include "driver/spi_master.h"
#endif

// ---- SPI / BUSY / RST transport (mirror of klipper_rf's transport contract)
// One call = NSS-framed transfer. The PN5180 host protocol is: wait BUSY low,
// send `frame` in one NSS frame; if rlen, wait BUSY low again and clock out
// rlen 0xFF filler bytes in a SECOND NSS frame into readbuf. See
// pn5180_chip.py _exchange() / the KlipperTransport docstring.
struct Pn5180Spi {
    virtual ~Pn5180Spi() {}
    virtual void exchange(const uint8_t *frame, size_t flen,
                          uint8_t *readbuf, size_t rlen) = 0;
    virtual void wait_busy_low(uint32_t timeout_ms) = 0; // throw on timeout
    virtual void delay_us(uint32_t us) = 0;
    virtual uint32_t micros() = 0;
    virtual bool reset() = 0;            // pulse RST if wired; false if not
};

// Frame carried in one MCU/SPI message is bounded; multi-block reads split.
static const size_t PN5180_MAX_FRAME = 48;

// Global (as in mfrc522.h) so the app refers to it unqualified.
struct CardUid {
    uint8_t bytes[10];
    uint8_t size;
    uint8_t sak;
    uint8_t atqa[2];
};

class Pn5180 {
public:
    // MUST stay identical to Mfrc522::Status (mfrc522.h) - callers switch on it
    enum Status : uint8_t {
        OK, NO_CARD, ERROR, COLLISION, CRC_ERROR, NAK, AUTH_FAILED,
        CHIP_TIMEOUT,
    };
    // Mfrc522::Pins + `busy` (PN5180 BUSY line). `host` is the SPI host index
    // (keep the field so Pins stays source-compatible); clock_hz default 1 MHz.
    struct Pins {
        int host;
        int sck, mosi, miso, cs, busy, rst;
        int clock_hz;
    };
    enum Magic : uint8_t {
        MAGIC_NONE, MAGIC_GEN1A, MAGIC_GEN2, MAGIC_GEN3, MAGIC_GEN4,
    };

    // Inject SPI (tests pass a simulator; begin() builds the ESP-IDF one).
    explicit Pn5180(Pn5180Spi *spi = nullptr) : spi_(spi) {}

    // ---- core: implemented in pn5180_core.cpp (WP-CORE) --------------------
    // Build the ESP-IDF SPI backend from pins, hard-reset, read versions,
    // leave RF off. Returns the product version major byte (0x00/0xFF = no
    // chip), matching Mfrc522::begin()'s "VersionReg" convention. See
    // pn5180_chip.py initialize()/get_versions().
    uint8_t begin(const Pins &pins);
    uint8_t version();                  // product version major (first light)
    bool self_test();                   // bus/IC bring-up report; true if ok
    void antenna(bool on);              // RF field on/off (rf_on/rf_off)
    void power_down();
    // WUPA + anticollision/select. iso14443a.py activate()/_wupa_select().
    Status select(CardUid &uid);
    // Crypto1 auth via CMD_MIFARE_AUTHENTICATE. iso14443a.py mifare_auth().
    Status authenticate(uint8_t block, const uint8_t key[6],
                        const CardUid &uid, bool key_b = false);
    void stop_crypto();                 // chip.crypto_off()
    Status read_block(uint8_t block, uint8_t out[16]);   // mifare_read_block
    Status write_block(uint8_t block, const uint8_t data[16]); // mifare_write
    Status halt();
    void set_rx_timeout_ms(uint16_t ms);

    // ---- magic: implemented in pn5180_magic.cpp (WP-MAGIC) -----------------
    // Ported from magic.py (MagicWriter/Cloner). Gen1a/Gen2/Gen3 only; Gen4 is
    // deliberately unsupported on the PN5180 (see magic.py header comment).
    Magic magic_detect();               // classify without changing UID
    Status magic_gen1a_open();          // 0x40/0x43 backdoor; 4-bit ACK
    Status magic_gen1a_write_block(uint8_t block, const uint8_t data[16]);
    Status magic_gen3_write_block0(const uint8_t data[16],
                                   uint16_t timeout_ms = 1500);
    // Write the full 1024-byte image (block 0 included) onto a magic card and
    // verify under the derived keys. derive_key_a(uid, sector, out[6]) yields
    // the cloned UID's key A per sector (the firmware's taglib provides it).
    Status clone_image(const uint8_t image[1024],
                       void (*derive_key_a)(const uint8_t uid[4], int sector,
                                            uint8_t out[6]));

    static const char *status_name(Status s);
    static const char *magic_name(Magic m);

    // ---- MFRC522-compatible shims so the tag-writer app's existing clone
    // flow drives the PN5180 unchanged. gen4/GDM is unsupported on the PN5180
    // (its fixed MIFARE_AUTHENTICATE cannot drive magic-auth), so those
    // degrade to an error/none; the IRQ line is not used here.
    bool irq_level() const { return false; }
    Status magic_backdoor_open() { return magic_gen1a_open(); }
    bool magic_is_gen1a() { return magic_gen1a_open() == OK; }
    Status magic_gdm_open() { return ERROR; }
    Status magic_gdm_auth(const CardUid &uid) { (void)uid; return AUTH_FAILED; }
    Status magic_gdm_probe(uint8_t out[8], uint8_t *out_len, uint8_t *bits) {
        (void)out; if (out_len) *out_len = 0; if (bits) *bits = 0;
        return ERROR;
    }
    Status magic_gen3_write_uid(const uint8_t *uid, uint8_t uid_len,
                                uint16_t timeout_ms = 1500) {
        (void)uid; (void)uid_len; (void)timeout_ms; return ERROR;
    }
    Status magic_gen4_read(uint8_t block, uint8_t out[16]) {
        (void)block; (void)out; return ERROR;
    }
    Status magic_gen4_write(uint8_t block, const uint8_t data[16]) {
        (void)block; (void)data; return ERROR;
    }
    Status read_block_ex(uint8_t cmd, uint8_t block, uint8_t out[16]) {
        return (cmd == 0x30 || cmd == 0xA0) ? read_block(block, out) : ERROR;
    }
    Status write_block_ex(uint8_t cmd, uint8_t block, const uint8_t data[16]) {
        return (cmd == 0xA0) ? write_block(block, data) : ERROR;
    }

    // Low-level primitives shared by core and magic. WP-CORE implements them;
    // WP-MAGIC calls them and, for its standalone unit test, links a fake that
    // implements this surface (see BRIEF). Kept public so the two translation
    // units stay file-disjoint.
    Pn5180Spi *spi() { return spi_; }
    // CMD_* host-interface helpers (pn5180_chip.py): register and data ops.
    void write_register(uint8_t reg, uint32_t value);
    void write_register_or_mask(uint8_t reg, uint32_t mask);
    void write_register_and_mask(uint8_t reg, uint32_t mask);
    uint32_t read_register(uint8_t reg);
    void load_rf_config(uint8_t tx_config, uint8_t rx_config);
    void rf_on();
    void rf_off();
    void clear_irqs();
    void set_crc(bool tx, bool rx);
    // Low-level transceive (pn5180_chip.py transceive()): returns bytes in
    // out/out_len and the count of valid bits in the last byte via last_bits.
    Status transceive(const uint8_t *data, size_t len, uint8_t valid_bits,
                      uint8_t *out, size_t *out_len, uint8_t *last_bits,
                      uint32_t timeout_ms);
    // Re-select a halted tag by UID / power-cycle field and re-select
    // (iso14443a.py reactivate(); magic.py _field_reset()).
    bool reselect(const CardUid &uid);
    bool field_reset(const uint8_t expect_uid[4], CardUid &out);

private:
    Pn5180Spi *spi_;
};

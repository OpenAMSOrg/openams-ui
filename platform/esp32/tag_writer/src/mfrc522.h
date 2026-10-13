// Copyright (C) 2026 JR Lomas <lomas.jr@gmail.com>
// This file may be distributed under the terms of the GNU GPLv3 license.

// MFRC522 on an ESP-IDF SPI bus, plus the ISO 14443-A / MIFARE Classic
// commands the writer needs. Same register sequence as the Mega build
// (../src/mfrc522_soft.cpp), which follows the OpenAMS Klipper driver and the
// miguelbalboa library.
#pragma once

#include <stdint.h>

#include "driver/spi_master.h"

struct CardUid {
    uint8_t bytes[10];
    uint8_t size;
    uint8_t sak;
    uint8_t atqa[2];
};

class Mfrc522 {
public:
    enum Status : uint8_t {
        OK,
        NO_CARD,       // no answer before the chip's timer expired
        ERROR,         // protocol/parity/buffer error or a bad frame
        COLLISION,     // more than one card in the field
        CRC_ERROR,
        NAK,           // card refused (wrong access bits, bad block)
        AUTH_FAILED,
        CHIP_TIMEOUT,  // the MFRC522 itself never finished
    };

    struct Pins {
        spi_host_device_t host;
        int sck, mosi, miso, cs, rst, irq;
        int clock_hz;
    };

    // Bus + device setup, hard reset, register init. Returns VersionReg
    // (0x00/0xFF = no chip answering).
    uint8_t begin(const Pins &pins);
    uint8_t version() { return read_reg(VERSION_REG); }
    bool irq_level() const;
    void antenna(bool on);
    void power_down();

    // WUPA (wakes idle and halted cards) and full anticollision/select.
    Status select(CardUid &uid);
    Status authenticate(uint8_t block, const uint8_t key[6], const CardUid &uid,
                        bool key_b = false);
    void stop_crypto();
    Status read_block(uint8_t block, uint8_t out[16]);
    Status write_block(uint8_t block, const uint8_t data[16]);
    Status halt();

    // Receive timeout used by transceive/auth. The hardware timer is a 40 kHz
    // tick; 25 ms covers a normal MIFARE write, but block-0 / gen3 APDU
    // programming needs longer. Restores the 25 ms default in init_registers().
    void set_rx_timeout_ms(uint16_t ms);

    // "Magic" MIFARE tags, whose block 0 (the UID) can be rewritten.
    //  - Gen1a: a backdoor (0x40/0x43) opens unauthenticated access to every
    //    block, block 0 included. magic_backdoor_open() must precede each
    //    read_block/write_block in that mode.
    //  - Gen2 (CUID): block 0 is writable through the ordinary authenticated
    //    write, so a normal authenticate()+write_block(0,...) just works.
    //  - Gen3 (APDU): block 0 is written with a raw 90 F0 CC CC 10 <16 bytes>
    //    frame after select, no Crypto1. Other blocks use ordinary writes.
    //  - Gen4 GDM / USCUID: CUID often off at the factory. Enter via GDM
    //    wakeup 0x20/0x23, or select + magic auth 0x80 with a zero key, then
    //    ordinary A0 or hidden A8 writes.
    enum Magic : uint8_t { MAGIC_NONE, MAGIC_GEN1A, MAGIC_GEN2, MAGIC_GEN3, MAGIC_GEN4 };
    Status magic_backdoor_open();  // wakes the tag into gen1a backdoor mode
    bool magic_is_gen1a();         // non-destructive probe
    Status magic_gdm_open();       // gen4 GDM wakeup 0x20/0x23
    // After ISO14443-3 select (no auth). Writes manufacturer block 0.
    Status magic_gen3_write_block0(const uint8_t data[16], uint16_t timeout_ms = 1500);
    // Gen3 UID-only: 90 FB CC CC <len> <uid>. Some cards ignore 90 F0.
    Status magic_gen3_write_uid(const uint8_t *uid, uint8_t uid_len, uint16_t timeout_ms = 1500);
    // Gen4 GTU backdoor, default password 00000000. Tag must be selected.
    Status magic_gen4_read(uint8_t block, uint8_t out[16]);
    Status magic_gen4_write(uint8_t block, const uint8_t data[16]);
    // USCUID / GDM "magic auth": command 0x80 then Crypto1 with a zero key.
    Status magic_gdm_auth(const CardUid &uid);
    // After select: send 80 00 + CRC and return whatever the tag answered
    // (nonce or NAK), even if the frame has a parity/protocol error.
    Status magic_gdm_probe(uint8_t out[8], uint8_t *out_len, uint8_t *bits);
    // Encrypted MIFARE read/write with a caller-chosen command byte. After
    // magic_gdm_auth() use 0xA0 (public), 0xA8 (hidden), 0x38 (hidden read),
    // 0xE0 (config read), 0xE1 (config write).
    Status read_block_ex(uint8_t cmd, uint8_t block, uint8_t out[16]);
    Status write_block_ex(uint8_t cmd, uint8_t block, const uint8_t data[16]);

    static const char *status_name(Status status);
    static const char *magic_name(Magic magic);

    // Bus/IC bring-up checks (FIFO loopback, register readback, SPI clock
    // sweep, RST and IRQ lines, antenna driver); prints a report and leaves
    // the chip re-initialised. True when every check passed.
    bool self_test();

private:
    enum : uint8_t {
        COMMAND_REG = 0x01, COM_IEN_REG = 0x02, DIV_IEN_REG = 0x03, COM_IRQ_REG = 0x04, ERROR_REG = 0x06, STATUS2_REG = 0x08,
        FIFO_DATA_REG = 0x09, FIFO_LEVEL_REG = 0x0A, CONTROL_REG = 0x0C,
        BIT_FRAMING_REG = 0x0D, COLL_REG = 0x0E, MODE_REG = 0x11, TX_MODE_REG = 0x12,
        RX_MODE_REG = 0x13, TX_CONTROL_REG = 0x14, TX_ASK_REG = 0x15,
        MOD_WIDTH_REG = 0x24, RF_CFG_REG = 0x26, T_MODE_REG = 0x2A,
        T_PRESCALER_REG = 0x2B, T_RELOAD_H_REG = 0x2C, T_RELOAD_L_REG = 0x2D,
        VERSION_REG = 0x37,
    };
    enum : uint8_t {
        CMD_IDLE = 0x00, CMD_TRANSCEIVE = 0x0C, CMD_MF_AUTHENT = 0x0E, CMD_SOFT_RESET = 0x0F,
    };

    uint8_t read_reg(uint8_t reg);
    void write_reg(uint8_t reg, uint8_t value);
    void write_fifo(const uint8_t *data, uint8_t length);
    void read_fifo(uint8_t *data, uint8_t length);
    void set_bits(uint8_t reg, uint8_t mask) { write_reg(reg, read_reg(reg) | mask); }
    void clear_bits(uint8_t reg, uint8_t mask) { write_reg(reg, read_reg(reg) & ~mask); }

    Status communicate(uint8_t command, const uint8_t *send, uint8_t send_length,
                       uint8_t *back, uint8_t *back_length, uint8_t *valid_bits,
                       bool check_crc);
    Status transceive(const uint8_t *send, uint8_t send_length, uint8_t *back,
                      uint8_t *back_length, uint8_t *valid_bits = nullptr,
                      bool check_crc = false) {
        return communicate(CMD_TRANSCEIVE, send, send_length, back, back_length, valid_bits,
                           check_crc);
    }
    Status mifare_command(const uint8_t *data, uint8_t length);
    static void crc_a(const uint8_t *data, uint8_t length, uint8_t out[2]);

    bool set_clock(int hz);
    void init_registers();
    bool fifo_loopback(uint32_t seed);

    spi_device_handle_t dev_ = nullptr;
    spi_host_device_t host_ = SPI3_HOST;
    int cs_pin_ = -1;
    int clock_hz_ = 0;
    int rst_pin_ = -1;
    int irq_pin_ = -1;
    int timeout_ms_ = 25;
};

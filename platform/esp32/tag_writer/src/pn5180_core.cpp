// Copyright (C) 2026 JR Lomas <lomas.jr@gmail.com>
// This file may be distributed under the terms of the GNU GPLv3 license.

#include "pn5180.h"
#include <string.h>

namespace {

// Host interface instructions
static const uint8_t CMD_WRITE_REGISTER          = 0x00;
static const uint8_t CMD_WRITE_REGISTER_OR_MASK  = 0x01;
static const uint8_t CMD_WRITE_REGISTER_AND_MASK = 0x02;
static const uint8_t CMD_WRITE_REGISTER_MULTIPLE = 0x03;
static const uint8_t CMD_READ_REGISTER           = 0x04;
static const uint8_t CMD_READ_REGISTER_MULTIPLE  = 0x05;
static const uint8_t CMD_READ_EEPROM             = 0x07;
static const uint8_t CMD_SEND_DATA               = 0x09;
static const uint8_t CMD_READ_DATA               = 0x0A;
static const uint8_t CMD_MIFARE_AUTHENTICATE     = 0x0C;
static const uint8_t CMD_LOAD_RF_CONFIG          = 0x11;
static const uint8_t CMD_RF_ON                   = 0x16;
static const uint8_t CMD_RF_OFF                  = 0x17;

// Registers
static const uint8_t SYSTEM_CONFIG       = 0x00;
static const uint8_t IRQ_STATUS          = 0x02;
static const uint8_t IRQ_CLEAR           = 0x03;
static const uint8_t CRC_RX_CONFIG       = 0x12;
static const uint8_t RX_STATUS           = 0x13;
static const uint8_t CRC_TX_CONFIG       = 0x19;
static const uint8_t RF_STATUS           = 0x1D;

// WRITE_REGISTER_MULTIPLE actions
static const uint8_t ACTION_WRITE = 0x01;
static const uint8_t ACTION_OR    = 0x02;
static const uint8_t ACTION_AND   = 0x03;

// IRQ bits
static const uint32_t IRQ_RX              = 1u << 0;
static const uint32_t IRQ_TX              = 1u << 1;
static const uint32_t IRQ_RF_ACTIVE_ERROR = 1u << 10;
static const uint32_t IRQ_GENERAL_ERROR   = 1u << 17;

// RX_STATUS bits
static const uint32_t RX_BYTES_MASK           = 0x1FF;
static const uint32_t RX_LAST_BITS_SHIFT      = 13;
static const uint32_t RX_LAST_BITS_MASK       = 7;
static const uint32_t RX_DATA_INTEGRITY_ERROR = 1u << 16;
static const uint32_t RX_PROTOCOL_ERROR       = 1u << 17;
static const uint32_t RX_COLLISION            = 1u << 18;

// RF_STATUS
static const uint32_t RF_TX_RF_STATUS = 1u << 17;

// SYSTEM_CONFIG
static const uint32_t SYS_COMMAND_MASK   = 0x7;
static const uint32_t SYS_IDLE_AND       = 0xFFFFFFF8;
static const uint32_t SYS_TRANSCEIVE_OR  = 0x00000003;
static const uint32_t SYS_MFC_CRYPTO_ON  = 1u << 6;
static const uint32_t SYS_CRYPTO_OFF_AND = 0xFFFFFFBF;

// EEPROM
static const uint8_t EEPROM_PRODUCT = 0x10;

// RF configurations (tx, rx)
static const uint8_t RF_TX_ISO14443A_106 = 0x00;
static const uint8_t RF_RX_ISO14443A_106 = 0x80;

static uint32_t unpack_le32(const uint8_t *b) {
    return (uint32_t)b[0] | ((uint32_t)b[1] << 8) |
           ((uint32_t)b[2] << 16) | ((uint32_t)b[3] << 24);
}

static void pack_le32(uint8_t *b, uint32_t v) {
    b[0] = (uint8_t)(v & 0xFF);
    b[1] = (uint8_t)((v >> 8) & 0xFF);
    b[2] = (uint8_t)((v >> 16) & 0xFF);
    b[3] = (uint8_t)((v >> 24) & 0xFF);
}

struct DriverConfig {
    uint16_t rx_timeout_ms = 50;
    uint32_t last_rf_off_us = 0;
    bool has_rf_off = false;
};

static DriverConfig s_cfg;

static void wait_rf_off_time(Pn5180Spi *spi) {
    if (!s_cfg.has_rf_off || !spi) return;
    uint32_t now = spi->micros();
    uint32_t elapsed = now - s_cfg.last_rf_off_us;
    if (elapsed < 5100) {
        spi->delay_us(5100 - elapsed);
    }
}

static void send_data_cmd(Pn5180Spi *spi, const uint8_t *data, size_t len, uint8_t valid_bits) {
    uint8_t wrm[19];
    wrm[0] = CMD_WRITE_REGISTER_MULTIPLE;
    wrm[1] = SYSTEM_CONFIG;
    wrm[2] = ACTION_AND;
    pack_le32(wrm + 3, SYS_IDLE_AND);
    wrm[7] = SYSTEM_CONFIG;
    wrm[8] = ACTION_OR;
    pack_le32(wrm + 9, SYS_TRANSCEIVE_OR);
    wrm[13] = IRQ_CLEAR;
    wrm[14] = ACTION_WRITE;
    pack_le32(wrm + 15, 0xFFFFFFFF);
    spi->exchange(wrm, 19, nullptr, 0);

    uint8_t frame[PN5180_MAX_FRAME];
    frame[0] = CMD_SEND_DATA;
    frame[1] = valid_bits & 0x07;
    memcpy(frame + 2, data, len);
    spi->exchange(frame, len + 2, nullptr, 0);
}

static void read_data_cmd(Pn5180Spi *spi, uint8_t *out, size_t len) {
    if (len == 0 || !out) return;
    uint8_t frame[2] = {CMD_READ_DATA, 0x00};
    spi->exchange(frame, 2, out, len);
}

static Pn5180::Status transceive_internal(Pn5180 *chip,
                                          const uint8_t *data, size_t len,
                                          uint8_t valid_bits, uint8_t *out,
                                          size_t *out_len, uint8_t *last_bits,
                                          uint32_t timeout_ms, bool ack = false,
                                          bool allow_integrity_error = false,
                                          uint32_t expect_us = 0) {
    Pn5180Spi *spi = chip->spi();
    if (!spi) return Pn5180::ERROR;
    if (len + 2 > PN5180_MAX_FRAME) return Pn5180::ERROR;

    send_data_cmd(spi, data, len, valid_bits);

    uint32_t start = spi->micros();
    uint32_t timeout_us = timeout_ms * 1000;
    if (expect_us > 0) {
        uint32_t initial_pause = expect_us < timeout_us ? expect_us : timeout_us;
        spi->delay_us(initial_pause);
    }

    uint32_t irq = 0;
    uint32_t rx_stat = 0;

    while (true) {
        uint8_t q[3] = {CMD_READ_REGISTER_MULTIPLE, IRQ_STATUS, RX_STATUS};
        uint8_t rx[8] = {0};
        spi->exchange(q, 3, rx, 8);
        irq = unpack_le32(rx);
        rx_stat = unpack_le32(rx + 4);

        if (irq & IRQ_GENERAL_ERROR) {
            return Pn5180::ERROR;
        }
        if (irq & IRQ_RX) {
            break;
        }

        uint32_t elapsed = spi->micros() - start;
        if (elapsed >= timeout_us) {
            chip->write_register_and_mask(SYSTEM_CONFIG, SYS_IDLE_AND);
            chip->clear_irqs();
            return Pn5180::NO_CARD;
        }

        uint32_t rem = timeout_us - elapsed;
        uint32_t step = (expect_us > 0) ? expect_us : rem / 2;
        if (step < 2000) step = 2000;
        if (step > rem) step = rem;
        spi->delay_us(step);
    }

    size_t n = rx_stat & RX_BYTES_MASK;
    uint8_t lb = (rx_stat >> RX_LAST_BITS_SHIFT) & RX_LAST_BITS_MASK;
    if (last_bits) *last_bits = lb;

    if (n > PN5180_MAX_FRAME) {
        return Pn5180::ERROR;
    }

    if (n > 0 && out) {
        read_data_cmd(spi, out, n);
    }
    if (out_len) *out_len = n;

    if (rx_stat & RX_COLLISION) {
        // Reset the command FSM to idle so a collision does not leave the
        // chip mid-reception with BUSY stuck high for the next frame. The
        // bits received up to the collision are left in out/out_len/last_bits
        // for the caller to walk the anticollision tree.
        chip->write_register_and_mask(SYSTEM_CONFIG, SYS_IDLE_AND);
        chip->clear_irqs();
        return Pn5180::COLLISION;
    }

    if (ack) {
        if (n == 0) {
            return Pn5180::OK;
        }
    } else {
        if (rx_stat & RX_PROTOCOL_ERROR) {
            return Pn5180::ERROR;
        }
        if (rx_stat & RX_DATA_INTEGRITY_ERROR) {
            if (allow_integrity_error) {
                // Accepted
            } else if (n == 1 && out && ((out[0] & 0x0F) != 0x0A)) {
                return Pn5180::NAK;
            } else {
                return Pn5180::CRC_ERROR;
            }
        }
        if (n == 0) {
            return Pn5180::ERROR;
        }
    }

    return Pn5180::OK;
}

static Pn5180::Status transmit_frame(Pn5180 *chip, const uint8_t *data, size_t len, uint8_t valid_bits) {
    Pn5180Spi *spi = chip->spi();
    if (!spi) return Pn5180::ERROR;
    send_data_cmd(spi, data, len, valid_bits);
    uint32_t start = spi->micros();
    while (true) {
        uint32_t irq = chip->read_register(IRQ_STATUS);
        if (irq & IRQ_TX) {
            break;
        }
        if (spi->micros() - start >= 10000) {
            chip->write_register_and_mask(SYSTEM_CONFIG, SYS_IDLE_AND);
            return Pn5180::CHIP_TIMEOUT;
        }
        spi->delay_us(1000);
    }
    chip->write_register_and_mask(SYSTEM_CONFIG, SYS_IDLE_AND);
    return Pn5180::OK;
}

// ISO14443-3 anticollision for one cascade level, walking the bit tree so a
// single tag is isolated and SELECTed even when several answer (e.g. a Bambu
// spool's two tags). Returns the 4 UID bytes + BCC in out5 and the SAK.
// Mirrors iso14443a.py _resolve_level: each collision fixes the known UID
// bits up to it (collision position = received bits = n*8+last_bits) and
// takes the 0-branch; with one tag it resolves in a single pass.
static Pn5180::Status resolve_level(Pn5180 *chip, uint8_t sel,
                                    uint8_t out5[5], uint8_t *sak_out) {
    uint8_t known[33];
    int nbits = 0;
    for (int iter = 0; iter < 40; iter++) {
        int nbytes = nbits / 8, rem = nbits % 8, fb = (nbits + 7) / 8;
        uint8_t frame[7];
        frame[0] = sel;
        frame[1] = (uint8_t)(((2 + nbytes) << 4) | rem);
        for (int i = 0; i < fb; i++) frame[2 + i] = 0;
        for (int i = 0; i < nbits; i++)
            if (known[i]) frame[2 + (i >> 3)] |= (uint8_t)(1u << (i & 7));
        chip->set_crc(false, false);
        uint8_t resp[PN5180_MAX_FRAME];
        size_t n = 0;
        uint8_t lb = 0;
        Pn5180::Status s = chip->transceive(frame, (size_t)(2 + fb),
                                            (uint8_t)rem, resp, &n, &lb, 50);
        if (s != Pn5180::OK && s != Pn5180::COLLISION) return s;
        int got = (int)n * 8 + (int)lb;
        if (s == Pn5180::COLLISION) {
            for (int i = 0; i < got && nbits < 32; i++)
                known[nbits++] = (uint8_t)((resp[i >> 3] >> (i & 7)) & 1);
            if (nbits >= 32) return Pn5180::ERROR;   // did not isolate a tag
            known[nbits++] = 0;                       // descend the 0-branch
            continue;
        }
        if (nbits + got < 40) return Pn5180::ERROR;   // short anticollision
        uint8_t full[5] = {0, 0, 0, 0, 0};
        for (int i = 0; i < nbits; i++)
            if (known[i]) full[i >> 3] |= (uint8_t)(1u << (i & 7));
        for (int i = 0; i < got && (nbits + i) < 40; i++)
            if ((resp[i >> 3] >> (i & 7)) & 1)
                full[(nbits + i) >> 3] |=
                    (uint8_t)(1u << ((nbits + i) & 7));
        if ((full[0] ^ full[1] ^ full[2] ^ full[3]) != full[4])
            return Pn5180::ERROR;                      // bad BCC
        memcpy(out5, full, 5);
        chip->set_crc(true, true);
        uint8_t selframe[7] = {sel, 0x70, full[0], full[1],
                               full[2], full[3], full[4]};
        uint8_t sak[PN5180_MAX_FRAME];
        size_t sn = 0;
        uint8_t slb = 0;
        Pn5180::Status ss = chip->transceive(selframe, 7, 0, sak,
                                             &sn, &slb, 50);
        if (ss != Pn5180::OK) return ss;
        if (sn != 1) return Pn5180::ERROR;
        *sak_out = sak[0];
        return Pn5180::OK;
    }
    return Pn5180::ERROR;                              // did not converge
}

} // namespace

#ifdef ESP_PLATFORM
Pn5180Spi *pn5180_make_idf_spi(const Pn5180::Pins &pins);
#endif

uint8_t Pn5180::begin(const Pins &pins) {
#ifdef ESP_PLATFORM
    if (!spi_) {
        spi_ = pn5180_make_idf_spi(pins);
    }
#else
    (void)pins;
#endif
    if (!spi_) return 0x00;

    if (spi_->reset()) {
        spi_->delay_us(3000);
    }
    spi_->wait_busy_low(500);
    clear_irqs();
    rf_off();
    write_register_and_mask(SYSTEM_CONFIG, SYS_IDLE_AND);
    return version();
}

uint8_t Pn5180::version() {
    if (!spi_) return 0x00;
    uint8_t raw[6] = {0};
    uint8_t cmd[3] = {CMD_READ_EEPROM, EEPROM_PRODUCT, 6};
    spi_->exchange(cmd, 3, raw, 6);
    if (raw[1] == 0xFF || (raw[0] == 0x00 && raw[1] == 0x00)) {
        return 0x00;
    }
    return raw[1];
}

bool Pn5180::self_test() {
    if (!spi_) return false;
    uint8_t v = version();
    if (v == 0x00 || v == 0xFF) return false;

    clear_irqs();
    uint32_t irq = read_register(IRQ_STATUS);
    if (irq & IRQ_GENERAL_ERROR) return false;

    return true;
}

void Pn5180::antenna(bool on) {
    if (!spi_) return;
    uint32_t rf_stat = read_register(RF_STATUS);
    bool current_field_on = (rf_stat & RF_TX_RF_STATUS) != 0;
    uint32_t crc_tx = read_register(CRC_TX_CONFIG);
    bool config_loaded = ((crc_tx & 0xFFFF0000) == 0x63630000);

    if (on) {
        if (!config_loaded) {
            if (current_field_on) {
                rf_off();
                spi_->delay_us(5100);
                current_field_on = false;
            }
            load_rf_config(RF_TX_ISO14443A_106, RF_RX_ISO14443A_106);
        }
        if (!current_field_on) {
            rf_on();
            spi_->delay_us(5100);
        }
        stop_crypto();
        set_crc(false, false);
    } else {
        if (current_field_on) {
            rf_off();
        }
    }
}

void Pn5180::power_down() {
    antenna(false);
    write_register_and_mask(SYSTEM_CONFIG, SYS_IDLE_AND);
}

Pn5180::Status Pn5180::select(CardUid &uid) {
    memset(&uid, 0, sizeof(uid));
    if (!spi_) return ERROR;
    uint32_t rf_stat = read_register(RF_STATUS);
    if ((rf_stat & RF_TX_RF_STATUS) == 0) {
        antenna(true);
    }
    stop_crypto();
    set_crc(false, false);

    uint8_t wupa = 0x52;
    uint8_t r[PN5180_MAX_FRAME];
    size_t rlen = 0;
    uint8_t lb = 0;

    Status s = transceive_internal(this, &wupa, 1, 7, r, &rlen, &lb, 20, false, false, 894);
    if (s == NO_CARD) {
        return NO_CARD;
    }
    // A collision here is expected when several tags wake together; ignore it
    // and let the anticollision cascade isolate one. Take the ATQA only from
    // a clean answer.
    if (s == OK && rlen == 2) {
        uid.atqa[0] = r[0];
        uid.atqa[1] = r[1];
    }

    static const uint8_t CASCADE_SEL[3] = {0x93, 0x95, 0x97};
    size_t uid_idx = 0;
    for (int cl = 0; cl < 3; cl++) {
        uint8_t r5[5];
        uint8_t sak = 0;
        Status rs = resolve_level(this, CASCADE_SEL[cl], r5, &sak);
        if (rs != OK) {
            return rs;
        }
        if (r5[0] == 0x88) {
            if (uid_idx + 3 > sizeof(uid.bytes)) return ERROR;
            memcpy(uid.bytes + uid_idx, r5 + 1, 3);
            uid_idx += 3;
            continue;
        }
        if (uid_idx + 4 > sizeof(uid.bytes)) return ERROR;
        memcpy(uid.bytes + uid_idx, r5, 4);
        uid_idx += 4;
        if (sak & 0x04) {
            return ERROR;
        }
        uid.sak = sak;
        uid.size = (uint8_t)uid_idx;
        return OK;
    }

    return ERROR;
}

Pn5180::Status Pn5180::authenticate(uint8_t block, const uint8_t key[6],
                                    const CardUid &uid, bool key_b) {
    if (!spi_) return ERROR;
    uint8_t key_type = key_b ? 0x61 : 0x60;
    const uint8_t *uid4 = (uid.size >= 4) ? (uid.bytes + uid.size - 4) : uid.bytes;

    uint8_t frame[13];
    frame[0] = CMD_MIFARE_AUTHENTICATE;
    memcpy(frame + 1, key, 6);
    frame[7] = key_type;
    frame[8] = block;
    memcpy(frame + 9, uid4, 4);

    uint8_t status_byte = 0xFF;
    spi_->exchange(frame, 13, &status_byte, 1);

    if (status_byte == 0) {
        uint32_t cfg = read_register(SYSTEM_CONFIG);
        if (cfg & SYS_MFC_CRYPTO_ON) {
            return OK;
        }
    }

    stop_crypto();
    if (!reselect(uid)) {
        return NO_CARD;
    }
    return AUTH_FAILED;
}

void Pn5180::stop_crypto() {
    if (!spi_) return;
    uint32_t cfg = read_register(SYSTEM_CONFIG);
    if ((cfg & SYS_MFC_CRYPTO_ON) == 0) return;
    write_register_and_mask(SYSTEM_CONFIG, SYS_CRYPTO_OFF_AND);
}

Pn5180::Status Pn5180::read_block(uint8_t block, uint8_t out[16]) {
    if (!spi_ || !out) return ERROR;
    set_crc(true, true);

    uint8_t cmd[2] = {0x30, block};
    uint8_t rx[PN5180_MAX_FRAME];
    size_t rlen = 0;
    uint8_t lb = 0;
    uint32_t t_ms = s_cfg.rx_timeout_ms > 0 ? s_cfg.rx_timeout_ms : 50;

    Status s = transceive_internal(this, cmd, 2, 0, rx, &rlen, &lb, t_ms, false, false, 2167);
    if (s != OK) {
        return s;
    }
    if (rlen != 16) {
        return ERROR;
    }
    memcpy(out, rx, 16);
    return OK;
}

Pn5180::Status Pn5180::write_block(uint8_t block, const uint8_t data[16]) {
    if (!spi_ || !data) return ERROR;
    set_crc(true, false);

    uint8_t cmd[2] = {0xA0, block};
    uint8_t rx[PN5180_MAX_FRAME];
    size_t rlen = 0;
    uint8_t lb = 0;
    uint32_t t_ms1 = s_cfg.rx_timeout_ms > 20 ? s_cfg.rx_timeout_ms : 20;

    Status s = transceive_internal(this, cmd, 2, 0, rx, &rlen, &lb, t_ms1, true, false, 894);
    if (s != OK) {
        set_crc(true, true);
        return s;
    }
    if (rlen > 0 && (rx[0] & 0x0F) != 0x0A) {
        set_crc(true, true);
        return NAK;
    }

    uint32_t t_ms2 = s_cfg.rx_timeout_ms > 30 ? s_cfg.rx_timeout_ms : 30;
    s = transceive_internal(this, data, 16, 0, rx, &rlen, &lb, t_ms2, true, false, 2083);
    set_crc(true, true);

    if (s != OK) {
        return s;
    }
    if (rlen > 0 && (rx[0] & 0x0F) != 0x0A) {
        return NAK;
    }

    return OK;
}

Pn5180::Status Pn5180::halt() {
    set_crc(true, false);
    static const uint8_t hlta[2] = {0x50, 0x00};
    return transmit_frame(this, hlta, 2, 0);
}

void Pn5180::set_rx_timeout_ms(uint16_t ms) {
    s_cfg.rx_timeout_ms = ms;
}

const char *Pn5180::status_name(Status s) {
    switch (s) {
    case OK: return "ok";
    case NO_CARD: return "no card";
    case ERROR: return "protocol error";
    case COLLISION: return "several cards";
    case CRC_ERROR: return "CRC error";
    case NAK: return "card refused";
    case AUTH_FAILED: return "auth failed";
    case CHIP_TIMEOUT: return "reader timeout";
    }
    return "?";
}

void Pn5180::write_register(uint8_t reg, uint32_t value) {
    if (!spi_) return;
    uint8_t frame[6];
    frame[0] = CMD_WRITE_REGISTER;
    frame[1] = reg;
    pack_le32(frame + 2, value);
    spi_->exchange(frame, 6, nullptr, 0);
}

void Pn5180::write_register_or_mask(uint8_t reg, uint32_t mask) {
    if (!spi_) return;
    uint8_t frame[6];
    frame[0] = CMD_WRITE_REGISTER_OR_MASK;
    frame[1] = reg;
    pack_le32(frame + 2, mask);
    spi_->exchange(frame, 6, nullptr, 0);
}

void Pn5180::write_register_and_mask(uint8_t reg, uint32_t mask) {
    if (!spi_) return;
    uint8_t frame[6];
    frame[0] = CMD_WRITE_REGISTER_AND_MASK;
    frame[1] = reg;
    pack_le32(frame + 2, mask);
    spi_->exchange(frame, 6, nullptr, 0);
}

uint32_t Pn5180::read_register(uint8_t reg) {
    if (!spi_) return 0;
    uint8_t frame[2] = {CMD_READ_REGISTER, reg};
    uint8_t rx[4] = {0};
    spi_->exchange(frame, 2, rx, 4);
    return unpack_le32(rx);
}

void Pn5180::load_rf_config(uint8_t tx_config, uint8_t rx_config) {
    if (!spi_) return;
    wait_rf_off_time(spi_);
    uint8_t frame[3] = {CMD_LOAD_RF_CONFIG, tx_config, rx_config};
    spi_->exchange(frame, 3, nullptr, 0);
}

void Pn5180::rf_on() {
    if (!spi_) return;
    wait_rf_off_time(spi_);
    uint8_t frame[2] = {CMD_RF_ON, 0x00};
    spi_->exchange(frame, 2, nullptr, 0);

    uint32_t start = spi_->micros();
    while (true) {
        uint8_t q[3] = {CMD_READ_REGISTER_MULTIPLE, RF_STATUS, IRQ_STATUS};
        uint8_t rx[8] = {0};
        spi_->exchange(q, 3, rx, 8);
        uint32_t rf_stat = unpack_le32(rx);
        uint32_t irq = unpack_le32(rx + 4);

        if (irq & IRQ_RF_ACTIVE_ERROR) {
            break;
        }
        if (rf_stat & RF_TX_RF_STATUS) {
            break;
        }
        if (spi_->micros() - start >= 50000) {
            break;
        }
        spi_->delay_us(1000);
    }
    clear_irqs();
    s_cfg.has_rf_off = false;
}

void Pn5180::rf_off() {
    if (!spi_) return;
    uint32_t rf_stat = read_register(RF_STATUS);
    bool current_field_on = (rf_stat & RF_TX_RF_STATUS) != 0;
    uint8_t frame[2] = {CMD_RF_OFF, 0x00};
    spi_->exchange(frame, 2, nullptr, 0);
    if (current_field_on) {
        s_cfg.last_rf_off_us = spi_->micros();
        s_cfg.has_rf_off = true;
    }
}

void Pn5180::clear_irqs() {
    write_register(IRQ_CLEAR, 0xFFFFFFFF);
}

void Pn5180::set_crc(bool tx, bool rx) {
    if (!spi_) return;
    uint32_t tx_reg = read_register(CRC_TX_CONFIG);
    uint32_t rx_reg = read_register(CRC_RX_CONFIG);
    bool cur_tx = (tx_reg & 1) != 0;
    bool cur_rx = (rx_reg & 1) != 0;
    if (cur_tx == tx && cur_rx == rx) {
        return;
    }

    uint8_t wrm[13];
    wrm[0] = CMD_WRITE_REGISTER_MULTIPLE;
    wrm[1] = CRC_TX_CONFIG;
    wrm[2] = tx ? ACTION_OR : ACTION_AND;
    pack_le32(wrm + 3, tx ? 1 : 0xFFFFFFFE);
    wrm[7] = CRC_RX_CONFIG;
    wrm[8] = rx ? ACTION_OR : ACTION_AND;
    pack_le32(wrm + 9, rx ? 1 : 0xFFFFFFFE);
    spi_->exchange(wrm, 13, nullptr, 0);
}

Pn5180::Status Pn5180::transceive(const uint8_t *data, size_t len, uint8_t valid_bits,
                                  uint8_t *out, size_t *out_len, uint8_t *last_bits,
                                  uint32_t timeout_ms) {
    uint32_t t_ms = timeout_ms > 0 ? timeout_ms : s_cfg.rx_timeout_ms;
    return transceive_internal(this, data, len, valid_bits, out, out_len, last_bits,
                               t_ms, false, false, 0);
}

bool Pn5180::reselect(const CardUid &uid) {
    stop_crypto();
    halt();
    CardUid found;
    if (select(found) != OK) {
        return false;
    }
    if (found.size != uid.size) {
        return false;
    }
    if (memcmp(found.bytes, uid.bytes, uid.size) != 0) {
        return false;
    }
    return true;
}

bool Pn5180::field_reset(const uint8_t expect_uid[4], CardUid &out) {
    if (!spi_) return false;
    stop_crypto();
    halt();
    rf_off();
    spi_->delay_us(5100);
    antenna(true);
    for (int i = 0; i < 5; i++) {
        if (select(out) == OK) {
            if (expect_uid == nullptr || memcmp(out.bytes, expect_uid, 4) == 0) {
                return true;
            }
        }
        spi_->delay_us(2000);
    }
    return false;
}

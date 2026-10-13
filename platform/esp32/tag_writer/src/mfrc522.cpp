// Copyright (C) 2026 JR Lomas <lomas.jr@gmail.com>
// This file may be distributed under the terms of the GNU GPLv3 license.

#include "mfrc522.h"

#include <string.h>

#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_rom_sys.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "mfrc522";

static const uint8_t PICC_WUPA = 0x52;
static const uint8_t PICC_SEL[3] = {0x93, 0x95, 0x97};
static const uint8_t PICC_CASCADE_TAG = 0x88;
static const uint8_t PICC_AUTH_KEY_A = 0x60;
static const uint8_t PICC_AUTH_KEY_B = 0x61;
static const uint8_t PICC_READ = 0x30;
static const uint8_t PICC_WRITE = 0xA0;
static const uint8_t PICC_HALT = 0x50;
static const uint8_t MF_ACK = 0x0A;

static int64_t now_ms() { return esp_timer_get_time() / 1000; }

uint8_t Mfrc522::read_reg(uint8_t reg) {
    spi_transaction_t t;
    memset(&t, 0, sizeof(t));
    t.length = 16;
    t.flags = SPI_TRANS_USE_TXDATA | SPI_TRANS_USE_RXDATA;
    t.tx_data[0] = 0x80 | ((reg << 1) & 0x7E);
    spi_device_polling_transmit(dev_, &t);
    return t.rx_data[1];
}

void Mfrc522::write_reg(uint8_t reg, uint8_t value) {
    spi_transaction_t t;
    memset(&t, 0, sizeof(t));
    t.length = 16;
    t.flags = SPI_TRANS_USE_TXDATA;
    t.tx_data[0] = (reg << 1) & 0x7E;
    t.tx_data[1] = value;
    spi_device_polling_transmit(dev_, &t);
}

// The chip keeps addressing the same register for every byte that follows
// the address in one chip-select frame. Without DMA an ESP32 SPI transaction
// carries at most 64 bytes, so longer bursts go out as several frames.
static const uint8_t BURST_MAX = 63;

void Mfrc522::write_fifo(const uint8_t *data, uint8_t length) {
    while (length) {
        uint8_t n = length < BURST_MAX ? length : BURST_MAX;
        uint8_t tx[BURST_MAX + 1];
        tx[0] = (FIFO_DATA_REG << 1) & 0x7E;
        memcpy(tx + 1, data, n);
        spi_transaction_t t;
        memset(&t, 0, sizeof(t));
        t.length = (n + 1) * 8;
        t.tx_buffer = tx;
        spi_device_polling_transmit(dev_, &t);
        data += n;
        length -= n;
    }
}

void Mfrc522::read_fifo(uint8_t *data, uint8_t length) {
    while (length) {
        uint8_t n = length < BURST_MAX ? length : BURST_MAX;
        uint8_t tx[BURST_MAX + 1], rx[BURST_MAX + 1];
        memset(tx, 0x80 | ((FIFO_DATA_REG << 1) & 0x7E), n);
        tx[n] = 0;
        spi_transaction_t t;
        memset(&t, 0, sizeof(t));
        t.length = (n + 1) * 8;
        t.tx_buffer = tx;
        t.rx_buffer = rx;
        spi_device_polling_transmit(dev_, &t);
        memcpy(data, rx + 1, n);
        data += n;
        length -= n;
    }
}

uint8_t Mfrc522::begin(const Pins &pins) {
    rst_pin_ = pins.rst;
    irq_pin_ = pins.irq;
    host_ = pins.host;
    cs_pin_ = pins.cs;

    spi_bus_config_t bus;
    memset(&bus, 0, sizeof(bus));
    bus.sclk_io_num = pins.sck;
    bus.mosi_io_num = pins.mosi;
    bus.miso_io_num = pins.miso;
    bus.quadwp_io_num = -1;
    bus.quadhd_io_num = -1;
    bus.max_transfer_sz = 128;
    esp_err_t err = spi_bus_initialize(pins.host, &bus, SPI_DMA_DISABLED);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "spi_bus_initialize failed: %s", esp_err_to_name(err));
        return 0;
    }
    if (!set_clock(pins.clock_hz))
        return 0;

    if (irq_pin_ >= 0) {
        gpio_config_t irq = {};
        irq.pin_bit_mask = 1ULL << irq_pin_;
        irq.mode = GPIO_MODE_INPUT;
        gpio_config(&irq);
    }

    // Hard reset through NRSTPD, then wait out the oscillator start-up.
    gpio_config_t rst = {};
    rst.pin_bit_mask = 1ULL << rst_pin_;
    rst.mode = GPIO_MODE_OUTPUT;
    gpio_config(&rst);
    gpio_set_level((gpio_num_t)rst_pin_, 0);
    esp_rom_delay_us(100);
    gpio_set_level((gpio_num_t)rst_pin_, 1);
    vTaskDelay(pdMS_TO_TICKS(50));

    init_registers();
    uint8_t v = version();
    ESP_LOGI(TAG, "VersionReg 0x%02X", v);
    return v;
}

bool Mfrc522::set_clock(int hz) {
    if (dev_) {
        spi_bus_remove_device(dev_);
        dev_ = nullptr;
    }
    spi_device_interface_config_t dev;
    memset(&dev, 0, sizeof(dev));
    dev.mode = 0;
    dev.clock_speed_hz = hz;
    dev.spics_io_num = cs_pin_;
    dev.queue_size = 1;
    esp_err_t err = spi_bus_add_device(host_, &dev, &dev_);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "spi_bus_add_device failed: %s", esp_err_to_name(err));
        return false;
    }
    clock_hz_ = hz;
    return true;
}

void Mfrc522::init_registers() {
    write_reg(COMMAND_REG, CMD_SOFT_RESET);
    int64_t start = now_ms();
    while (read_reg(COMMAND_REG) & 0x10) {  // PowerDown clears when ready
        if (now_ms() - start > 150)
            break;
        vTaskDelay(1);
    }

    write_reg(TX_MODE_REG, 0x00);
    write_reg(RX_MODE_REG, 0x00);
    write_reg(MOD_WIDTH_REG, 0x26);
    // Timer: TAuto, 40 kHz tick. Default 25 ms covers a normal MIFARE write;
    // block-0 / gen3 APDU programming raises this via set_rx_timeout_ms().
    write_reg(T_MODE_REG, 0x80);
    write_reg(T_PRESCALER_REG, 0xA9);
    set_rx_timeout_ms(25);
    write_reg(TX_ASK_REG, 0x40);  // 100 % ASK
    write_reg(MODE_REG, 0x3D);    // CRC preset 0x6363
    write_reg(RF_CFG_REG, 0x70);  // 48 dB receiver gain
    // IRQ pin push-pull and active low with no source enabled: IO35 has no
    // pull resistor, so an open-drain IRQ would float.
    write_reg(DIV_IEN_REG, 0x80);
    write_reg(COM_IEN_REG, 0x80);
}

bool Mfrc522::irq_level() const {
    return irq_pin_ >= 0 && gpio_get_level((gpio_num_t)irq_pin_);
}

void Mfrc522::set_rx_timeout_ms(uint16_t ms) {
    if (ms < 1)
        ms = 1;
    // 40 kHz tick: TReload = ms * 40, capped at 16 bits (~1638 ms).
    uint32_t ticks = (uint32_t)ms * 40;
    if (ticks > 65535)
        ticks = 65535;
    timeout_ms_ = (int)((ticks + 39) / 40);
    write_reg(T_RELOAD_H_REG, (uint8_t)(ticks >> 8));
    write_reg(T_RELOAD_L_REG, (uint8_t)ticks);
}

void Mfrc522::antenna(bool on) {
    if (on) {
        if ((read_reg(TX_CONTROL_REG) & 0x03) != 0x03)
            set_bits(TX_CONTROL_REG, 0x03);
    } else {
        clear_bits(TX_CONTROL_REG, 0x03);
    }
}

void Mfrc522::power_down() {
    antenna(false);
    gpio_set_level((gpio_num_t)rst_pin_, 0);
}

void Mfrc522::crc_a(const uint8_t *data, uint8_t length, uint8_t out[2]) {
    // ISO 14443-A CRC (poly 0x8408 reflected, preset 0x6363), as mfrc522.py.
    uint16_t crc = 0x6363;
    while (length--) {
        uint8_t value = *data++ ^ (crc & 0xFF);
        value ^= value << 4;
        crc = (crc >> 8) ^ ((uint16_t)value << 8) ^ ((uint16_t)value << 3) ^ (value >> 4);
    }
    out[0] = crc & 0xFF;
    out[1] = crc >> 8;
}

Mfrc522::Status Mfrc522::communicate(uint8_t command, const uint8_t *send,
                                     uint8_t send_length, uint8_t *back,
                                     uint8_t *back_length, uint8_t *valid_bits,
                                     bool check_crc) {
    uint8_t tx_last_bits = valid_bits ? *valid_bits : 0;
    uint8_t wait_irq = command == CMD_MF_AUTHENT ? 0x10 : 0x30;

    write_reg(COMMAND_REG, CMD_IDLE);
    write_reg(COM_IRQ_REG, 0x7F);
    write_reg(FIFO_LEVEL_REG, 0x80);
    write_fifo(send, send_length);
    write_reg(BIT_FRAMING_REG, tx_last_bits);
    write_reg(COMMAND_REG, command);
    if (command == CMD_TRANSCEIVE)
        set_bits(BIT_FRAMING_REG, 0x80);  // StartSend

    int64_t start = now_ms();
    for (;;) {
        uint8_t irq = read_reg(COM_IRQ_REG);
        if (irq & wait_irq)
            break;
        if (irq & 0x01) {  // TimerIRq: nothing answered within 25 ms
            clear_bits(BIT_FRAMING_REG, 0x80);
            return NO_CARD;
        }
        if (now_ms() - start > timeout_ms_ + 40) {
            clear_bits(BIT_FRAMING_REG, 0x80);
            return CHIP_TIMEOUT;
        }
    }
    if (command == CMD_TRANSCEIVE)
        clear_bits(BIT_FRAMING_REG, 0x80);

    uint8_t error = read_reg(ERROR_REG);

    uint8_t received_bits = 0;
    if (back && back_length) {
        uint8_t count = read_reg(FIFO_LEVEL_REG);
        if (count > *back_length)
            count = *back_length;
        *back_length = count;
        if (count)
            read_fifo(back, count);
        received_bits = read_reg(CONTROL_REG) & 0x07;
        if (valid_bits)
            *valid_bits = received_bits;
    }
    if (error & 0x13)  // BufferOvfl, ParityErr, ProtocolErr
        return ERROR;
    if (error & 0x08)
        return COLLISION;

    if (back && back_length && check_crc) {
        if (*back_length == 1 && received_bits == 4)
            return NAK;
        if (*back_length < 2 || received_bits != 0)
            return CRC_ERROR;
        uint8_t crc[2];
        crc_a(back, *back_length - 2, crc);
        if (back[*back_length - 2] != crc[0] || back[*back_length - 1] != crc[1])
            return CRC_ERROR;
    }
    return OK;
}

Mfrc522::Status Mfrc522::select(CardUid &uid) {
    uid.size = 0;
    uid.sak = 0;

    clear_bits(COLL_REG, 0x80);
    uint8_t wupa = PICC_WUPA;
    uint8_t atqa_length = sizeof(uid.atqa);
    uint8_t bits = 7;
    Status status = transceive(&wupa, 1, uid.atqa, &atqa_length, &bits);
    if (status != OK)
        return status;
    if (atqa_length != 2 || bits != 0)
        return ERROR;

    for (uint8_t level = 0; level < 3; level++) {
        uint8_t frame[9] = {PICC_SEL[level], 0x20};
        uint8_t part[5];
        uint8_t part_length = sizeof(part);
        clear_bits(COLL_REG, 0x80);
        status = transceive(frame, 2, part, &part_length);
        if (status != OK)
            return status;
        if (part_length != 5 || (part[0] ^ part[1] ^ part[2] ^ part[3]) != part[4])
            return ERROR;

        frame[1] = 0x70;
        memcpy(frame + 2, part, 5);
        crc_a(frame, 7, frame + 7);
        uint8_t sak[3];
        uint8_t sak_length = sizeof(sak);
        status = transceive(frame, 9, sak, &sak_length, nullptr, true);
        if (status != OK)
            return status;
        if (sak_length != 3)
            return ERROR;

        if (part[0] == PICC_CASCADE_TAG) {
            if (uid.size + 3 > sizeof(uid.bytes))
                return ERROR;  // a non-compliant cascade must not run past uid.bytes
            memcpy(uid.bytes + uid.size, part + 1, 3);
            uid.size += 3;
        } else {
            if (uid.size + 4 > sizeof(uid.bytes))
                return ERROR;
            memcpy(uid.bytes + uid.size, part, 4);
            uid.size += 4;
        }
        if (!(sak[0] & 0x04)) {
            uid.sak = sak[0];
            return OK;
        }
    }
    return ERROR;
}

Mfrc522::Status Mfrc522::authenticate(uint8_t block, const uint8_t key[6], const CardUid &uid,
                                      bool key_b) {
    // Crypto1 takes the last four UID bytes, as the OpenAMS reader does.
    uint8_t frame[12];
    frame[0] = key_b ? PICC_AUTH_KEY_B : PICC_AUTH_KEY_A;
    frame[1] = block;
    memcpy(frame + 2, key, 6);
    memcpy(frame + 8, uid.bytes + uid.size - 4, 4);
    Status status = communicate(CMD_MF_AUTHENT, frame, sizeof(frame), nullptr, nullptr,
                                nullptr, false);
    if (status == NO_CARD)
        return AUTH_FAILED;
    if (status != OK)
        return status;
    return (read_reg(STATUS2_REG) & 0x08) ? OK : AUTH_FAILED;
}

void Mfrc522::stop_crypto() { clear_bits(STATUS2_REG, 0x08); }

Mfrc522::Status Mfrc522::read_block_ex(uint8_t cmd, uint8_t block, uint8_t out[16]) {
    uint8_t frame[4] = {cmd, block};
    crc_a(frame, 2, frame + 2);
    uint8_t back[18];
    uint8_t back_length = sizeof(back);
    Status status = transceive(frame, 4, back, &back_length, nullptr, true);
    if (status != OK)
        return status;
    if (back_length < 16)
        return ERROR;
    memcpy(out, back, 16);
    return OK;
}

Mfrc522::Status Mfrc522::read_block(uint8_t block, uint8_t out[16]) {
    return read_block_ex(PICC_READ, block, out);
}

Mfrc522::Status Mfrc522::mifare_command(const uint8_t *data, uint8_t length) {
    uint8_t frame[18];
    memcpy(frame, data, length);
    crc_a(frame, length, frame + length);
    uint8_t back[2];
    uint8_t back_length = sizeof(back);
    uint8_t bits = 0;
    Status status = transceive(frame, length + 2, back, &back_length, &bits);
    if (status != OK)
        return status;
    if (back_length != 1 || bits != 4)
        return ERROR;
    return (back[0] & 0x0F) == MF_ACK ? OK : NAK;
}

Mfrc522::Status Mfrc522::write_block_ex(uint8_t cmd, uint8_t block, const uint8_t data[16]) {
    uint8_t command[2] = {cmd, block};
    Status status = mifare_command(command, 2);
    if (status != OK)
        return status;
    return mifare_command(data, 16);
}

Mfrc522::Status Mfrc522::write_block(uint8_t block, const uint8_t data[16]) {
    return write_block_ex(PICC_WRITE, block, data);
}

Mfrc522::Status Mfrc522::halt() {
    uint8_t frame[4] = {PICC_HALT, 0x00};
    crc_a(frame, 2, frame + 2);
    Status status = transceive(frame, 4, nullptr, nullptr);
    // A halted card stays silent: the timeout is the success case.
    return status == NO_CARD ? OK : (status == OK ? ERROR : status);
}

// Gen1a backdoor: WUPA, then the two magic short frames. Each answers with a
// 4-bit ACK (0x0A). Leaves the tag in unauthenticated full-access mode.
Mfrc522::Status Mfrc522::magic_backdoor_open() {
    stop_crypto();
    halt();  // a halted tag still answers WUPA and the backdoor

    uint8_t back[4];
    uint8_t back_length = sizeof(back);
    uint8_t bits = 7;  // WUPA is a 7-bit short frame
    uint8_t wupa = PICC_WUPA;
    transceive(&wupa, 1, back, &back_length, &bits);  // ATQA, ignored

    uint8_t cmd = 0x40;
    back_length = sizeof(back);
    bits = 7;
    Status status = transceive(&cmd, 1, back, &back_length, &bits);
    if (status != OK || back_length != 1 || bits != 4 || (back[0] & 0x0F) != 0x0A)
        return NAK;

    cmd = 0x43;
    back_length = sizeof(back);
    bits = 0;
    status = transceive(&cmd, 1, back, &back_length, &bits);
    if (status != OK || back_length != 1 || bits != 4 || (back[0] & 0x0F) != 0x0A)
        return NAK;
    return OK;
}

bool Mfrc522::magic_is_gen1a() { return magic_backdoor_open() == OK; }

Mfrc522::Status Mfrc522::magic_gdm_open() {
    // Same shape as gen1a, different short frames: 0x20 (7-bit) then 0x23.
    stop_crypto();
    halt();

    uint8_t back[4];
    uint8_t back_length = sizeof(back);
    uint8_t bits = 7;
    uint8_t wupa = PICC_WUPA;
    transceive(&wupa, 1, back, &back_length, &bits);

    uint8_t cmd = 0x20;
    back_length = sizeof(back);
    bits = 7;
    Status status = transceive(&cmd, 1, back, &back_length, &bits);
    if (status != OK || back_length != 1 || bits != 4 || (back[0] & 0x0F) != 0x0A)
        return NAK;

    cmd = 0x23;
    back_length = sizeof(back);
    bits = 0;
    status = transceive(&cmd, 1, back, &back_length, &bits);
    if (status != OK || back_length != 1 || bits != 4 || (back[0] & 0x0F) != 0x0A)
        return NAK;
    return OK;
}

Mfrc522::Status Mfrc522::magic_gdm_probe(uint8_t out[8], uint8_t *out_len, uint8_t *bits) {
    stop_crypto();
    uint8_t frame[4] = {0x80, 0x00};
    crc_a(frame, 2, frame + 2);
    *out_len = 8;
    *bits = 0;
    Status status = transceive(frame, 4, out, out_len, bits);
    uint8_t err = read_reg(ERROR_REG);
    uint8_t st2 = read_reg(STATUS2_REG);
    printf("magic: gdm 8000 %s n=%u bits=%u err=%02X st2=%02X", status_name(status), *out_len,
           *bits, err, st2);
    for (uint8_t i = 0; i < *out_len; i++)
        printf(" %02X", out[i]);
    printf("\n");
    return status;
}

Mfrc522::Status Mfrc522::magic_gdm_auth(const CardUid &uid) {
    static const uint8_t ZERO_KEY[6] = {0, 0, 0, 0, 0, 0};
    stop_crypto();
    uint8_t frame[12];
    frame[0] = 0x80;
    frame[1] = 0x00;
    memcpy(frame + 2, ZERO_KEY, 6);
    memcpy(frame + 8, uid.bytes + uid.size - 4, 4);
    Status status = communicate(CMD_MF_AUTHENT, frame, sizeof(frame), nullptr, nullptr,
                                nullptr, false);
    uint8_t err = read_reg(ERROR_REG);
    uint8_t st2 = read_reg(STATUS2_REG);
    uint8_t fifo = read_reg(FIFO_LEVEL_REG);
    printf("magic: gdm auth %s err=%02X st2=%02X fifo=%u\n", status_name(status), err, st2,
           fifo);
    if (st2 & 0x08)
        return OK;
    if (status == NO_CARD)
        return AUTH_FAILED;
    if (status != OK)
        return status;
    return AUTH_FAILED;
}

Mfrc522::Status Mfrc522::magic_gen4_read(uint8_t block, uint8_t out[16]) {
    // CF <4-byte pwd 00000000> CE <block> + CRC. Default password.
    stop_crypto();
    uint8_t frame[9] = {0xCF, 0x00, 0x00, 0x00, 0x00, 0xCE, block};
    crc_a(frame, 7, frame + 7);
    int saved = timeout_ms_;
    set_rx_timeout_ms(200);
    uint8_t back[18];
    uint8_t back_length = sizeof(back);
    Status status = transceive(frame, sizeof(frame), back, &back_length, nullptr, true);
    set_rx_timeout_ms((uint16_t)saved);
    if (status != OK)
        return status;
    if (back_length < 16)
        return ERROR;
    memcpy(out, back, 16);
    return OK;
}

Mfrc522::Status Mfrc522::magic_gen4_write(uint8_t block, const uint8_t data[16]) {
    // CF <4-byte pwd 00000000> CD <block> <16 data> + CRC.
    stop_crypto();
    uint8_t frame[25];
    frame[0] = 0xCF;
    memset(frame + 1, 0, 4);
    frame[5] = 0xCD;
    frame[6] = block;
    memcpy(frame + 7, data, 16);
    crc_a(frame, 23, frame + 23);
    int saved = timeout_ms_;
    set_rx_timeout_ms(1000);
    uint8_t back[8];
    uint8_t back_length = sizeof(back);
    uint8_t bits = 0;
    Status status = transceive(frame, sizeof(frame), back, &back_length, &bits);
    set_rx_timeout_ms((uint16_t)saved);
    if (status != OK)
        return status;
    if (back_length == 1 && bits == 4 && (back[0] & 0x0F) == MF_ACK)
        return OK;
    return NAK;
}

Mfrc522::Status Mfrc522::magic_gen3_write_block0(const uint8_t data[16], uint16_t timeout_ms) {
    // ISO14443-3 raw APDU, no Crypto1, no RATS. Proxmark: hf 14a raw -s -c -t 2000
    // 90 F0 CC CC 10 <16-byte block 0>.
    stop_crypto();
    uint8_t frame[23];
    frame[0] = 0x90;
    frame[1] = 0xF0;
    frame[2] = 0xCC;
    frame[3] = 0xCC;
    frame[4] = 0x10;
    memcpy(frame + 5, data, 16);
    crc_a(frame, 21, frame + 21);

    int saved = timeout_ms_;
    set_rx_timeout_ms(timeout_ms);
    uint8_t back[8];
    memset(back, 0, sizeof(back));
    uint8_t back_length = sizeof(back);
    uint8_t bits = 0;
    Status status = transceive(frame, sizeof(frame), back, &back_length, &bits);
    set_rx_timeout_ms((uint16_t)saved);
    if (status == NO_CARD)
        back_length = 0;
    printf("magic: gen3 90F0 %s n=%u bits=%u", status_name(status), back_length, bits);
    for (uint8_t i = 0; i < back_length; i++)
        printf(" %02X", back[i]);
    printf("\n");
    if (status != OK)
        return status;
    if (back_length == 1 && bits == 4 && (back[0] & 0x0F) == MF_ACK)
        return OK;
    if (back_length >= 2 && back[0] == 0x90 && back[1] == 0x00)
        return OK;
    return NAK;
}

Mfrc522::Status Mfrc522::magic_gen3_write_uid(const uint8_t *uid, uint8_t uid_len,
                                             uint16_t timeout_ms) {
    // 90 FB CC CC <len> <uid> + CRC. Length is often ignored.
    if (uid_len != 4 && uid_len != 7)
        return ERROR;
    stop_crypto();
    uint8_t frame[16];
    frame[0] = 0x90;
    frame[1] = 0xFB;
    frame[2] = 0xCC;
    frame[3] = 0xCC;
    frame[4] = uid_len;
    memcpy(frame + 5, uid, uid_len);
    uint8_t n = 5 + uid_len;
    crc_a(frame, n, frame + n);

    int saved = timeout_ms_;
    set_rx_timeout_ms(timeout_ms);
    uint8_t back[8];
    memset(back, 0, sizeof(back));
    uint8_t back_length = sizeof(back);
    uint8_t bits = 0;
    Status status = transceive(frame, n + 2, back, &back_length, &bits);
    set_rx_timeout_ms((uint16_t)saved);
    if (status == NO_CARD)
        back_length = 0;
    printf("magic: gen3 90FB %s n=%u bits=%u", status_name(status), back_length, bits);
    for (uint8_t i = 0; i < back_length; i++)
        printf(" %02X", back[i]);
    printf("\n");
    if (status != OK)
        return status;
    if (back_length == 1 && bits == 4 && (back[0] & 0x0F) == MF_ACK)
        return OK;
    if (back_length >= 2 && back[0] == 0x90 && back[1] == 0x00)
        return OK;
    return NAK;
}

const char *Mfrc522::magic_name(Magic magic) {
    switch (magic) {
    case MAGIC_GEN1A: return "gen1a (UID backdoor)";
    case MAGIC_GEN2: return "gen2 / CUID (direct UID write)";
    case MAGIC_GEN3: return "gen3 (APDU UID write)";
    case MAGIC_GEN4: return "gen4 / GDM (magic-auth UID write)";
    default: return "normal (UID read-only)";
    }
}

const char *Mfrc522::status_name(Status status) {
    switch (status) {
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

// ------------------------------------------------------------- self-test

bool Mfrc522::fifo_loopback(uint32_t seed) {
    uint8_t out[64], in[64];
    for (int i = 0; i < 64; i++) {
        seed = seed * 1103515245u + 12345u;
        out[i] = (uint8_t)(seed >> 16);
    }
    out[0] = 0x00;  // include the all-zero and all-one bytes
    out[1] = 0xFF;
    out[2] = 0xAA;
    out[3] = 0x55;
    write_reg(COMMAND_REG, CMD_IDLE);
    write_reg(FIFO_LEVEL_REG, 0x80);  // flush
    write_fifo(out, 64);
    uint8_t level = read_reg(FIFO_LEVEL_REG);
    read_fifo(in, 64);
    uint8_t after = read_reg(FIFO_LEVEL_REG);
    return level == 64 && after == 0 && !memcmp(out, in, 64);
}

bool Mfrc522::self_test() {
    bool all = true;
    int base_hz = clock_hz_;
    auto report = [&all](const char *name, bool ok, const char *detail) {
        printf("  %-26s %s  %s\n", name, ok ? "PASS" : "FAIL", detail);
        all &= ok;
    };
    char detail[96];
    printf("MFRC522 bus self-test (SPI %d Hz)\n", base_hz);

    // 1. VersionReg must read back the same every time.
    int bad = 0;
    uint8_t first = version();
    for (int i = 0; i < 500; i++)
        bad += version() != first;
    snprintf(detail, sizeof(detail), "0x%02X, %d/500 mismatched reads", first, bad);
    report("VersionReg stable", bad == 0 && first != 0x00 && first != 0xFF, detail);

    // 2. Register write/readback on registers with no side effects.
    static const uint8_t regs[] = {T_RELOAD_H_REG, T_RELOAD_L_REG, MOD_WIDTH_REG};
    bad = 0;
    for (uint8_t reg : regs) {
        uint8_t saved = read_reg(reg);
        for (int v = 0; v < 256; v++) {
            write_reg(reg, (uint8_t)v);
            bad += read_reg(reg) != v;
        }
        write_reg(reg, saved);
    }
    snprintf(detail, sizeof(detail), "3 registers x 256 values, %d errors", bad);
    report("register write/readback", bad == 0, detail);

    // 3. 64-byte FIFO burst loopback, repeated.
    bad = 0;
    for (uint32_t i = 0; i < 200; i++)
        bad += !fifo_loopback(i * 7919u + 1);
    snprintf(detail, sizeof(detail), "200 x 64-byte bursts, %d failed", bad);
    report("FIFO burst loopback", bad == 0, detail);

    // 4. SPI clock sweep (informational margin above the operating speed).
    static const int speeds[] = {1000000, 2000000, 4000000, 8000000, 10000000, 13333333};
    int fastest_ok = 0;
    detail[0] = 0;
    for (int hz : speeds) {
        if (!set_clock(hz))
            continue;
        int fails = 0;
        for (uint32_t i = 0; i < 50; i++)
            fails += !fifo_loopback(i + (uint32_t)hz);
        for (int i = 0; i < 50; i++)
            fails += version() != first;
        size_t n = strlen(detail);
        snprintf(detail + n, sizeof(detail) - n, "%s%.1fM:%s", n ? " " : "", hz / 1e6,
                 fails ? "x" : "ok");
        if (!fails && hz > fastest_ok)
            fastest_ok = hz;
    }
    set_clock(base_hz);
    report("SPI clock sweep", fastest_ok >= base_hz, detail);

    // 5. RST (IO32): a hard reset must return registers to their defaults.
    write_reg(T_RELOAD_L_REG, 0x5A);
    gpio_set_level((gpio_num_t)rst_pin_, 0);
    vTaskDelay(pdMS_TO_TICKS(5));
    uint8_t during = read_reg(VERSION_REG);  // powered down: SPI is dead
    gpio_set_level((gpio_num_t)rst_pin_, 1);
    vTaskDelay(pdMS_TO_TICKS(50));
    uint8_t reload_after = read_reg(T_RELOAD_L_REG);
    uint8_t version_after = version();
    snprintf(detail, sizeof(detail),
             "TReloadL 0x5A -> 0x%02X after reset, VersionReg 0x%02X during / 0x%02X after",
             reload_after, during, version_after);
    report("RST line (IO32)", reload_after == 0x00 && version_after == first, detail);
    init_registers();

    // 6. IRQ (IO35): route the timer interrupt to the pin, active high.
    if (irq_pin_ >= 0) {
        write_reg(DIV_IEN_REG, 0x80);       // push-pull
        write_reg(COM_IEN_REG, 0x01);       // TimerIEn, not inverted
        write_reg(COM_IRQ_REG, 0x7F);       // clear
        vTaskDelay(pdMS_TO_TICKS(2));
        int idle = gpio_get_level((gpio_num_t)irq_pin_);
        set_bits(CONTROL_REG, 0x40);         // TStartNow: 25 ms timer
        vTaskDelay(pdMS_TO_TICKS(40));
        int fired = gpio_get_level((gpio_num_t)irq_pin_);
        write_reg(COM_IRQ_REG, 0x7F);       // clear again
        vTaskDelay(pdMS_TO_TICKS(2));
        int cleared = gpio_get_level((gpio_num_t)irq_pin_);
        snprintf(detail, sizeof(detail), "pin idle=%d, timer fired=%d, cleared=%d", idle, fired,
                 cleared);
        report("IRQ line (IO35)", idle == 0 && fired == 1 && cleared == 0, detail);
        init_registers();
    }

    // 7. Antenna drivers: TxControlReg reflects TX1/TX2 on and off.
    antenna(true);
    uint8_t on = read_reg(TX_CONTROL_REG);
    antenna(false);
    uint8_t off = read_reg(TX_CONTROL_REG);
    snprintf(detail, sizeof(detail), "TxControlReg on=0x%02X off=0x%02X", on, off);
    report("antenna driver", (on & 0x03) == 0x03 && (off & 0x03) == 0, detail);

    printf("self-test %s\n", all ? "PASSED" : "FAILED");
    return all;
}

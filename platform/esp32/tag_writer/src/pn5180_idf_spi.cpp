// Copyright (C) 2026 JR Lomas <lomas.jr@gmail.com>
// This file may be distributed under the terms of the GNU GPLv3 license.

#ifdef ESP_PLATFORM

#include "pn5180.h"
#include <string.h>
#include <stdexcept>
#include "driver/spi_master.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_rom_sys.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "pn5180_idf_spi";

class Pn5180IdfSpi : public Pn5180Spi {
public:
    explicit Pn5180IdfSpi(const Pn5180::Pins &pins)
        : host_((spi_host_device_t)pins.host),
          cs_pin_(pins.cs),
          busy_pin_(pins.busy),
          rst_pin_(pins.rst),
          clock_hz_(pins.clock_hz > 0 ? pins.clock_hz : 1000000) {

        spi_bus_config_t bus;
        memset(&bus, 0, sizeof(bus));
        bus.sclk_io_num = pins.sck;
        bus.mosi_io_num = pins.mosi;
        bus.miso_io_num = pins.miso;
        bus.quadwp_io_num = -1;
        bus.quadhd_io_num = -1;
        bus.max_transfer_sz = 128;
        // ESP_ERR_INVALID_STATE here means the bus is already initialised
        // (the display owns a different host); any other error leaves dev_ null
        // and begin() then reports the reader as not answering.
        esp_err_t berr = spi_bus_initialize(host_, &bus, SPI_DMA_DISABLED);
        if (berr != ESP_OK && berr != ESP_ERR_INVALID_STATE) {
            ESP_LOGE("pn5180", "spi_bus_initialize failed: %s", esp_err_to_name(berr));
            return;
        }

        spi_device_interface_config_t dev;
        memset(&dev, 0, sizeof(dev));
        dev.clock_speed_hz = clock_hz_;
        dev.mode = 0;
        dev.spics_io_num = pins.cs;
        dev.queue_size = 1;
        esp_err_t derr = spi_bus_add_device(host_, &dev, &dev_);
        if (derr != ESP_OK) {
            ESP_LOGE("pn5180", "spi_bus_add_device failed: %s", esp_err_to_name(derr));
            dev_ = nullptr;
            return;
        }

        if (busy_pin_ >= 0) {
            gpio_config_t io_conf = {};
            io_conf.pin_bit_mask = (1ULL << busy_pin_);
            io_conf.mode = GPIO_MODE_INPUT;
            io_conf.pull_up_en = GPIO_PULLUP_DISABLE;
            io_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
            io_conf.intr_type = GPIO_INTR_DISABLE;
            gpio_config(&io_conf);
        }

        if (rst_pin_ >= 0) {
            gpio_config_t io_conf = {};
            io_conf.pin_bit_mask = (1ULL << rst_pin_);
            io_conf.mode = GPIO_MODE_OUTPUT;
            io_conf.pull_up_en = GPIO_PULLUP_DISABLE;
            io_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
            io_conf.intr_type = GPIO_INTR_DISABLE;
            gpio_config(&io_conf);
            gpio_set_level((gpio_num_t)rst_pin_, 1);
        }
    }

    ~Pn5180IdfSpi() override {
        if (dev_) {
            spi_bus_remove_device(dev_);
            dev_ = nullptr;
        }
    }

    void exchange(const uint8_t *frame, size_t flen,
                  uint8_t *readbuf, size_t rlen) override {
        if (!dev_) {
            if (readbuf && rlen)
                memset(readbuf, 0xFF, rlen);  // reads as "no chip": begin() reports not answering
            return;
        }
        wait_busy_low(100);

        spi_transaction_t t;
        memset(&t, 0, sizeof(t));
        t.length = flen * 8;
        t.tx_buffer = frame;
        spi_device_polling_transmit(dev_, &t);

        if (rlen > 0 && readbuf != nullptr) {
            if (rlen > PN5180_MAX_FRAME)
                rlen = PN5180_MAX_FRAME;  // the filler is one frame; never overrun it
            wait_busy_low(100);

            uint8_t filler[PN5180_MAX_FRAME];
            memset(filler, 0xFF, rlen);
            memset(&t, 0, sizeof(t));
            t.length = rlen * 8;
            t.tx_buffer = filler;
            t.rx_buffer = readbuf;
            spi_device_polling_transmit(dev_, &t);
        }
    }

    void wait_busy_low(uint32_t timeout_ms) override {
        if (busy_pin_ < 0) return;
        int64_t start = esp_timer_get_time();
        int64_t timeout_us = (int64_t)timeout_ms * 1000;
        while (gpio_get_level((gpio_num_t)busy_pin_) == 1) {
            int64_t elapsed = esp_timer_get_time() - start;
            if (elapsed >= timeout_us) {
                ESP_LOGE(TAG, "BUSY timed out after %u ms", (unsigned)timeout_ms);
#if __cpp_exceptions
                throw std::runtime_error("PN5180 BUSY timeout");
#endif
                return;
            }
            if (elapsed >= 1000)
                vTaskDelay(1);  // yield core 0 once this is more than a brief spin
            else
                esp_rom_delay_us(10);
        }
    }

    void delay_us(uint32_t us) override {
        if (us >= 2000) {
            vTaskDelay(pdMS_TO_TICKS(us / 1000));
            us %= 1000;
        }
        if (us)
            esp_rom_delay_us(us);
    }

    uint32_t micros() override {
        return (uint32_t)esp_timer_get_time();
    }

    bool reset() override {
        if (rst_pin_ < 0) return false;
        gpio_set_level((gpio_num_t)rst_pin_, 0);
        esp_rom_delay_us(100);
        gpio_set_level((gpio_num_t)rst_pin_, 1);
        esp_rom_delay_us(2000);
        return true;
    }

private:
    spi_host_device_t host_;
    spi_device_handle_t dev_ = nullptr;
    int cs_pin_ = -1;
    int busy_pin_ = -1;
    int rst_pin_ = -1;
    int clock_hz_ = 1000000;
};

Pn5180Spi *pn5180_make_idf_spi(const Pn5180::Pins &pins) {
    return new Pn5180IdfSpi(pins);
}

#endif // ESP_PLATFORM

/**
 * @file touch_gt911.c
 * The ES3C50P's capacitive touch (main/board.h): a Goodix GT911 on I2C (SDA19,
 * SCL20, INT18, RST38, from the LCDWiki ES3C50P specification), exposed to
 * LVGL as a pointer indev. Implements touch.h; a capacitive panel reports
 * panel coordinates, so the calibration entry points of the resistive board
 * have no counterpart here.
 *
 * The GT911 picks its I2C address from INT's level as RST is released: low
 * gives 0x5D. INT is then only an output of the chip, unused: the indev polls.
 */
#include "touch.h"
#include "board.h"

#include "app/platform.h"
#include "ui/ui_theme.h" /* UI_SCREEN_W / UI_SCREEN_H: the LVGL display's size */

#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define PIN_SDA 19
#define PIN_SCL 20
#define PIN_INT 18
#define PIN_RST 38

#define GT911_ADDR      0x5D
#define GT911_ADDR_ALT  0x14
#define I2C_HZ          400000
#define I2C_TIMEOUT_MS  20

#define REG_PRODUCT_ID 0x8140 /* 4 ASCII bytes, "911" */
#define REG_X_MAX      0x8048 /* the configured resolution, little-endian */
#define REG_STATUS     0x814E /* bit 7: a report is ready; bits 0-3: points */
#define REG_POINT1     0x8150 /* point 1: x (LE16), y (LE16), size (LE16); its track id is at 0x814F */

/* While a finger is down the chip reports at its report rate (about every
 * 10 ms). With no report for this long the contact is taken as ended, so a
 * missed "no points" report cannot leave a press stuck. */
#define STALE_MS 150

#define TOUCH_READ_PERIOD_MS 10 /* how often LVGL reads the panel */

#define PANEL_W BOARD_PANEL_W
#define PANEL_H BOARD_PANEL_H
#define UI_W UI_SCREEN_W
#define UI_H UI_SCREEN_H
#define OFFSET_X (((PANEL_W - UI_W) / 2) > 0 ? ((PANEL_W - UI_W) / 2) : 0)
#define OFFSET_Y (((PANEL_H - UI_H) / 2) > 0 ? ((PANEL_H - UI_H) / 2) : 0)

static const char *TAG = "touch";

static i2c_master_dev_handle_t s_dev;
static bool s_pressed;        /* the panel's last report: a finger is down */
static int32_t s_panel_x, s_panel_y;
static int64_t s_report_us;   /* when the last report arrived */
static volatile int64_t s_press_us;
static bool s_flip = BOARD_ROTATED_180;
static bool s_swallow_active;

static esp_err_t reg_read(uint16_t reg, uint8_t *buf, size_t n)
{
    uint8_t addr[2] = { (uint8_t)(reg >> 8), (uint8_t)reg };

    return i2c_master_transmit_receive(s_dev, addr, sizeof addr, buf, n, I2C_TIMEOUT_MS);
}

static esp_err_t reg_write8(uint16_t reg, uint8_t v)
{
    uint8_t frame[3] = { (uint8_t)(reg >> 8), (uint8_t)reg, v };

    return i2c_master_transmit(s_dev, frame, sizeof frame, I2C_TIMEOUT_MS);
}

/* Resets the chip with INT held low, which selects address 0x5D. */
static void gt911_reset(void)
{
    gpio_config_t out = {
        .pin_bit_mask = (1ULL << PIN_RST) | (1ULL << PIN_INT),
        .mode = GPIO_MODE_OUTPUT,
    };
    gpio_config(&out);
    gpio_set_level(PIN_INT, 0);
    gpio_set_level(PIN_RST, 0);
    vTaskDelay(pdMS_TO_TICKS(10));
    gpio_set_level(PIN_RST, 1);
    vTaskDelay(pdMS_TO_TICKS(10));
    gpio_set_direction(PIN_INT, GPIO_MODE_INPUT); /* the chip drives it from here on */
    vTaskDelay(pdMS_TO_TICKS(50));
}

/* Reads the latest report, if there is one, into s_pressed / s_panel_x/y. A
 * report stays valid until the next: the chip only raises the ready bit when
 * something changed. */
static void gt911_poll(void)
{
    uint8_t status;
    int64_t now = esp_timer_get_time();

    if(reg_read(REG_STATUS, &status, 1) != ESP_OK || !(status & 0x80)) {
        if(s_pressed && now - s_report_us > STALE_MS * 1000) s_pressed = false;
        return;
    }
    s_report_us = now;

    uint8_t points = status & 0x0F;
    if(points > 0 && points <= 5) {
        uint8_t p[4];
        if(reg_read(REG_POINT1, p, sizeof p) == ESP_OK) {
            s_panel_x = p[0] | (p[1] << 8);
            s_panel_y = p[2] | (p[3] << 8);
            if(!s_pressed) s_press_us = now;
            s_pressed = true;
        }
    }
    else {
        s_pressed = false;
    }
    reg_write8(REG_STATUS, 0); /* hand the buffer back to the chip */
}

static void touch_read_cb(lv_indev_t *indev, lv_indev_data_t *data)
{
    bool was_pressed = s_pressed;

    (void)indev;
    gt911_poll();

    int32_t px = s_panel_x, py = s_panel_y;
    if(s_flip) {
        px = PANEL_W - 1 - px;
        py = PANEL_H - 1 - py;
    }
    int32_t x = px - OFFSET_X, y = py - OFFSET_Y;
    bool inside = x >= 0 && x < UI_W && y >= 0 && y < UI_H;

    if(s_pressed && !was_pressed) {
        ESP_LOGI(TAG, "press: panel=(%ld,%ld) ui=(%ld,%ld)%s", (long)px, (long)py, (long)x,
                 (long)y, inside ? "" : " [outside UI window, ignored]");
    }

    if(s_swallow_active) {
        if(!s_pressed) s_swallow_active = false; /* contact ended: swallow window over */
        data->state = LV_INDEV_STATE_RELEASED;
        return;
    }

    if(s_pressed && inside) {
        data->point.x = x;
        data->point.y = y;
        data->state = LV_INDEV_STATE_PRESSED;
    }
    else {
        data->state = LV_INDEV_STATE_RELEASED;
    }
}

bool touch_is_pressed_raw(void)
{
    return s_pressed;
}

int64_t touch_take_press_us(void)
{
    int64_t t = s_press_us;

    s_press_us = 0;
    return t;
}

void touch_set_flip(bool flip)
{
    s_flip = flip != BOARD_ROTATED_180; /* as display_set_flip(): from the board's own orientation */
    ESP_LOGI(TAG, "touch flip: %s", flip ? "on" : "off");
}

void touch_swallow_current_press(void)
{
    s_swallow_active = true;
}

/* Settings' "calibrate touch": nothing to calibrate on a capacitive panel. */
void platform_touch_calibrate(void)
{
    ESP_LOGI(TAG, "touch calibration: not needed on this board (capacitive)");
}

lv_indev_t *touch_init(void)
{
    i2c_master_bus_config_t bus_cfg = {
        .i2c_port = -1,
        .sda_io_num = PIN_SDA,
        .scl_io_num = PIN_SCL,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = 1,
    };
    i2c_master_bus_handle_t bus;
    esp_err_t err = i2c_new_master_bus(&bus_cfg, &bus);
    if(err != ESP_OK) {
        ESP_LOGE(TAG, "i2c_new_master_bus failed: %s", esp_err_to_name(err));
        return NULL;
    }

    gt911_reset();

    uint16_t addr = GT911_ADDR;
    if(i2c_master_probe(bus, addr, I2C_TIMEOUT_MS) != ESP_OK) {
        addr = GT911_ADDR_ALT;
        if(i2c_master_probe(bus, addr, I2C_TIMEOUT_MS) != ESP_OK) {
            ESP_LOGE(TAG, "no GT911 at 0x%02X or 0x%02X", GT911_ADDR, GT911_ADDR_ALT);
            return NULL;
        }
    }
    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = addr,
        .scl_speed_hz = I2C_HZ,
    };
    err = i2c_master_bus_add_device(bus, &dev_cfg, &s_dev);
    if(err != ESP_OK) {
        ESP_LOGE(TAG, "i2c_master_bus_add_device failed: %s", esp_err_to_name(err));
        return NULL;
    }

    char id[5] = { 0 };
    uint8_t res[4] = { 0 };
    reg_read(REG_PRODUCT_ID, (uint8_t *)id, 4);
    reg_read(REG_X_MAX, res, sizeof res);

    lv_indev_t *indev = lv_indev_create();
    lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(indev, touch_read_cb);
    lv_timer_set_period(lv_indev_get_read_timer(indev), TOUCH_READ_PERIOD_MS);

    ESP_LOGI(TAG,
             "touch ready: GT%s at 0x%02X, configured %ux%u, panel %dx%d, UI offset (%d,%d), "
             "read period %dms",
             id, addr, res[0] | (res[1] << 8), res[2] | (res[3] << 8), PANEL_W, PANEL_H, OFFSET_X,
             OFFSET_Y, TOUCH_READ_PERIOD_MS);

    return indev;
}

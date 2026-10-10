/**
 * @file display.c
 * ST7796S bring-up. RST is tied to EN on this board, so there is no reset
 * GPIO to pulse — SWRESET (cmd 0x01) is the only reset path.
 */
#include "display.h"
#include "panel_config.h"

#include "ui/ui_theme.h" /* UI_SCREEN_W / UI_SCREEN_H: the LVGL display always matches these */

#include <math.h>
#include <string.h>

#include "driver/gpio.h"
#include "driver/ledc.h"
#include "driver/spi_master.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define PIN_SCK  14
#define PIN_MOSI 13
#define PIN_MISO 12
#define PIN_CS   15
#define PIN_DC   2
#define PIN_BL   27

/* Panel addressing space after MADCTL rotates the native 320x480 panel
 * into landscape. The UI is UI_SCREEN_W x UI_SCREEN_H (currently smaller
 * than the panel, so it is flushed centred with a border filled in the
 * app's background colour; once the UI re-layout lands at 480x320 this
 * offset naturally becomes 0 with no code change here). */
#define PANEL_W 480
#define PANEL_H 320
#define UI_W UI_SCREEN_W
#define UI_H UI_SCREEN_H
#define OFFSET_X (((PANEL_W - UI_W) / 2) > 0 ? ((PANEL_W - UI_W) / 2) : 0)
#define OFFSET_Y (((PANEL_H - UI_H) / 2) > 0 ? ((PANEL_H - UI_H) / 2) : 0)

/* Two stripe buffers of 12 lines (11.5 KB each, the same 23 KB as one 24-line
 * buffer): LVGL draws the next stripe into one while DMA sends the other. */
#define BUF_LINES 12
#define BUF_PIXELS (UI_W * BUF_LINES)

/* #0B0E12 as RGB565 (R5=0x0B>>3=1, G6=0x0E>>2=3, B5=0x12>>3=2 ->
 * (1<<11)|(3<<5)|2 = 0x0862), stored as the big-endian pair the panel
 * expects on the wire. */
#define PANEL_BG_HI 0x08
#define PANEL_BG_LO 0x62

/* Backlight LEDC (PWM): 5 kHz, 10-bit duty. */
#define BL_LEDC_TIMER LEDC_TIMER_0
#define BL_LEDC_MODE  LEDC_LOW_SPEED_MODE
#define BL_LEDC_CH    LEDC_CHANNEL_0
#define BL_LEDC_HZ    5000
#define BL_LEDC_RES   LEDC_TIMER_10_BIT
#define BL_LEDC_MAX   1023

/* MADCTL rotation bits a 180-degree flip toggles (display_set_flip()). */
#define MADCTL_FLIP_BITS 0xC0 /* MX|MY */

static const char *TAG = "display";

static spi_device_handle_t s_spi;
static panel_info_t s_panel_info;
static uint16_t *s_buf1, *s_buf2;

/* The stripe in flight: queued by flush_cb, finished in px_post_cb, which
 * tells LVGL the buffer is free again. FLUSH_TAG marks it among the panel's
 * own transactions (commands and window data are polled, untagged). */
#define DC_DATA   1
#define FLUSH_TAG 2
static spi_transaction_t s_px_trans;
static bool s_px_pending;
static bool s_flip;
static lv_display_t *s_disp; /* set at the end of display_init(); used by display_set_flip()
                              * to force a full redraw in the new orientation */

/* Runtime flush offset: (OFFSET_X, OFFSET_Y) normally (UI window centred on
 * the panel), (0, 0) while the touch calibration overlay owns the full
 * panel. See display_enter_calibration_mode() / display_exit_calibration_mode(). */
static int32_t s_flush_off_x = OFFSET_X;
static int32_t s_flush_off_y = OFFSET_Y;

/* ------------------------------------------------------------- low level */

/* t->user carries the DC level to apply for the duration of this
 * transaction (0 = command, 1 = data); the pre-transfer callback is the
 * only place that actually touches the GPIO, since spi_device_transmit()
 * may reorder around other queued transactions. */
static void IRAM_ATTR dc_pre_cb(spi_transaction_t *t)
{
    gpio_set_level(PIN_DC, (int)(intptr_t)t->user & DC_DATA);
}

/* A queued stripe has left the bus: its buffer is LVGL's again. */
static void IRAM_ATTR px_post_cb(spi_transaction_t *t)
{
    if(((intptr_t)t->user & FLUSH_TAG) && s_disp) lv_display_flush_ready(s_disp);
}

/* Wait for the stripe in flight (if any) before the next one, and before any
 * polled command: the driver will not poll while a queued transfer is out. */
static void px_wait(void)
{
    spi_transaction_t *done;

    if(!s_px_pending) return;
    spi_device_get_trans_result(s_spi, &done, portMAX_DELAY);
    s_px_pending = false;
}

static void lcd_cmd(uint8_t cmd)
{
    spi_transaction_t t;

    px_wait();   /* flip, contrast and sleep commands can come mid-frame */
    memset(&t, 0, sizeof(t));
    t.length = 8;
    t.tx_buffer = &cmd;
    t.user = (void *)0;
    spi_device_polling_transmit(s_spi, &t);
}

static void lcd_data(const uint8_t *data, size_t len)
{
    if(len == 0) return;
    px_wait();
    spi_transaction_t t;
    memset(&t, 0, sizeof(t));
    t.length = len * 8;
    t.tx_buffer = data;
    t.user = (void *)1;
    spi_device_polling_transmit(s_spi, &t);
}

static void lcd_data8(uint8_t v)
{
    lcd_data(&v, 1);
}

/* Sends `cmd`, then reads back nbytes (<=3) discarding the leading dummy
 * byte these panels shift out before real data. */
static uint32_t lcd_read_id(uint8_t cmd, int nbytes)
{
    lcd_cmd(cmd);

    spi_transaction_t t;
    memset(&t, 0, sizeof(t));
    t.length = 8 * (nbytes + 1);
    t.rxlength = 8 * (nbytes + 1);
    t.flags = SPI_TRANS_USE_RXDATA;
    t.user = (void *)1;
    spi_device_polling_transmit(s_spi, &t);

    uint32_t val = 0;
    for(int i = 0; i < nbytes; i++) {
        val = (val << 8) | t.rx_data[i + 1];
    }
    return val;
}

static void lcd_set_window(int x1, int y1, int x2, int y2)
{
    uint8_t buf[4];

    lcd_cmd(0x2A); /* CASET */
    buf[0] = (uint8_t)(x1 >> 8);
    buf[1] = (uint8_t)(x1 & 0xFF);
    buf[2] = (uint8_t)(x2 >> 8);
    buf[3] = (uint8_t)(x2 & 0xFF);
    lcd_data(buf, 4);

    lcd_cmd(0x2B); /* RASET */
    buf[0] = (uint8_t)(y1 >> 8);
    buf[1] = (uint8_t)(y1 & 0xFF);
    buf[2] = (uint8_t)(y2 >> 8);
    buf[3] = (uint8_t)(y2 & 0xFF);
    lcd_data(buf, 4);

    lcd_cmd(0x2C); /* RAMWR */
}

/* ------------------------------------------------------------ detection */

static void detect_panel(void)
{
    memset(&s_panel_info, 0, sizeof(s_panel_info));
    s_panel_info.spi_hz = PANEL_SPI_HZ;

    s_panel_info.id_0xd3 = lcd_read_id(0xD3, 3); /* RDID4 */
    s_panel_info.id_read_ok = (s_panel_info.id_0xd3 & 0xFFFF) == 0x7796;

    ESP_LOGI(TAG, "panel id: RDID4(0xD3)=0x%06X (expect 0x007796) match=%s",
             (unsigned)s_panel_info.id_0xd3, s_panel_info.id_read_ok ? "yes" : "no");
    if(!s_panel_info.id_read_ok) {
        ESP_LOGW(TAG,
                 "ID read did not match ST7796S — continuing with the ST7796S init "
                 "sequence anyway (this is the only panel this build supports). If the "
                 "screen stays white or garbled, try PANEL_SPI_HZ=20000000 in "
                 "panel_config.h next.");
    }

    bool default_invert = true; /* try inversion ON first, per this panel's usual wiring */
    bool default_bgr = true;

    s_panel_info.inversion_on =
        (PANEL_FORCE_INVERT >= 0) ? (PANEL_FORCE_INVERT != 0) : default_invert;
    s_panel_info.bgr = (PANEL_FORCE_BGR >= 0) ? (PANEL_FORCE_BGR != 0) : default_bgr;
    /* This is the BASE (unflipped) MADCTL; display_set_flip() XORs
     * MADCTL_FLIP_BITS onto it at runtime, it never overwrites it. */
    s_panel_info.madctl = (uint8_t)(PANEL_ROTATION_BITS | (s_panel_info.bgr ? 0x08 : 0x00));

    ESP_LOGI(TAG, "panel config: inversion=%s bgr=%s madctl=0x%02X spi_hz=%u",
             s_panel_info.inversion_on ? "on" : "off", s_panel_info.bgr ? "on" : "off",
             s_panel_info.madctl, (unsigned)s_panel_info.spi_hz);
}

/* --------------------------------------------------------- init sequence */

typedef struct {
    uint8_t cmd;
    uint8_t len; /* number of parameter bytes in data[] */
    uint8_t data[14];
    uint16_t delay_ms; /* delay after sending this command, 0 = none */
} init_cmd_t;

/* Command-set unlock (ST7796S-specific: 0xF0 with 0xC3 then 0x96 opens the
 * extension command set that the power/VCOM/gamma registers below live in;
 * the same command with 0x3C then 0x69 closes it again at the end). */
static const init_cmd_t s_unlock_seq[] = {
    { 0xF0, 1, { 0xC3 }, 0 },
    { 0xF0, 1, { 0x96 }, 0 },
};

/* Power control and VCOM — the standard ST7796S values reproduced from the
 * well-known LCDWiki/LovyanGFX Panel_ST7796 init table. Gamma (E0/E1) is
 * NOT here: it is sent separately by display_set_contrast(), which this
 * init sequence also calls (for "standard", the boot default), so contrast
 * has exactly one code path whether it runs at boot or at runtime. */
static const init_cmd_t s_power_seq[] = {
    { 0xB4, 1, { 0x01 }, 0 },                                           /* display inversion control: column inversion */
    { 0xB6, 3, { 0x80, 0x02, 0x3B }, 0 },                               /* display function control */
    { 0xE8, 8, { 0x40, 0x8A, 0x00, 0x00, 0x29, 0x19, 0xA5, 0x33 }, 0 }, /* power control (E8) */
    { 0xC1, 1, { 0x06 }, 0 },                                           /* power control 2 */
    { 0xC2, 1, { 0xA7 }, 0 },                                           /* power control 3 */
    { 0xC5, 1, { 0x18 }, 120 },                                         /* VCOM control */
};

static const init_cmd_t s_lock_seq[] = {
    { 0xF0, 1, { 0x3C }, 0 },
    { 0xF0, 1, { 0x69 }, 120 },
};

static void run_seq(const init_cmd_t *seq, size_t n)
{
    for(size_t i = 0; i < n; i++) {
        lcd_cmd(seq[i].cmd);
        lcd_data(seq[i].data, seq[i].len);
        if(seq[i].delay_ms) vTaskDelay(pdMS_TO_TICKS(seq[i].delay_ms));
    }
}

/* ------------------------------------------------------------- contrast */

/* Two ST7796S gamma presets (E0 positive / E1 negative), applied under the
 * same command-set unlock the init sequence uses. "standard" is the
 * original, known-good table this board shipped with; "high" is an
 * engineering approximation (steeper early/late breakpoints for deeper
 * blacks and brighter whites) reproduced from memory rather than a
 * datasheet-verified preset — tune visually, it is not gospel. */
static const uint8_t s_gamma_std_e0[14] = { 0xF0, 0x09, 0x0B, 0x06, 0x04, 0x15, 0x2F,
                                            0x54, 0x42, 0x3C, 0x17, 0x14, 0x18, 0x1B };
static const uint8_t s_gamma_std_e1[14] = { 0xE0, 0x09, 0x0B, 0x06, 0x04, 0x03, 0x2B,
                                            0x43, 0x42, 0x3B, 0x16, 0x14, 0x17, 0x1B };
static const uint8_t s_gamma_hi_e0[14] = { 0xF0, 0x03, 0x06, 0x03, 0x02, 0x08, 0x1D,
                                           0x54, 0x55, 0x3D, 0x12, 0x10, 0x14, 0x17 };
static const uint8_t s_gamma_hi_e1[14] = { 0xE0, 0x03, 0x06, 0x03, 0x02, 0x05, 0x19,
                                           0x43, 0x5C, 0x3C, 0x11, 0x10, 0x13, 0x17 };

void display_set_contrast(uint8_t contrast)
{
    const uint8_t *e0 = contrast ? s_gamma_hi_e0 : s_gamma_std_e0;
    const uint8_t *e1 = contrast ? s_gamma_hi_e1 : s_gamma_std_e1;

    lcd_cmd(0xF0);
    lcd_data8(0xC3);
    lcd_cmd(0xF0);
    lcd_data8(0x96);

    lcd_cmd(0xE0);
    lcd_data(e0, 14);
    lcd_cmd(0xE1);
    lcd_data(e1, 14);

    lcd_cmd(0xF0);
    lcd_data8(0x3C);
    lcd_cmd(0xF0);
    lcd_data8(0x69);

    ESP_LOGI(TAG, "display contrast: %s", contrast ? "high" : "standard");
}

/* --------------------------------------------------------------- flip */

void display_set_flip(bool flip)
{
    s_flip = flip;
    uint8_t madctl = (uint8_t)(s_panel_info.madctl ^ (flip ? MADCTL_FLIP_BITS : 0x00));

    lcd_cmd(0x36); /* MADCTL */
    lcd_data8(madctl);

    /* MADCTL changes how the panel's GRAM is SCANNED OUT, not what's in it:
     * every pixel already written under the OLD mapping now displays
     * wrong (mirrored) until it is rewritten. Repaint the whole panel to
     * the background first (raw SPI, so it touches every pixel
     * regardless of what LVGL thinks is "clean"), then force LVGL to
     * redraw its entire current screen AND the top layer (modals/toasts)
     * on top of that, instead of waiting for whatever it would normally
     * consider dirty on the next tick. */
    display_clear_panel_bg();
    if(s_disp) {
        lv_obj_t *scr = lv_screen_active();
        if(scr) lv_obj_invalidate(scr);
        lv_obj_invalidate(lv_layer_top());
        lv_refr_now(s_disp);
    }

    ESP_LOGI(TAG, "display flip: %s (madctl=0x%02X), forced full redraw", flip ? "on" : "off",
             madctl);
}

/* -------------------------------------------------------------- backlight */

/* Perceptual gamma (~2.2) so 10-30% duty is visibly distinct instead of
 * crowding near zero — a linear duty-vs-percent map makes the low end of
 * the slider feel like an on/off switch. */
static uint32_t backlight_gamma_duty(uint8_t pct)
{
    if(pct == 0) return 0;
    if(pct > 100) pct = 100;
    float f = (float)pct / 100.0f;
    float g = powf(f, 2.2f);
    uint32_t duty = (uint32_t)(g * BL_LEDC_MAX + 0.5f);
    return duty > BL_LEDC_MAX ? BL_LEDC_MAX : duty;
}

static void backlight_init(void)
{
    ledc_timer_config_t timer_cfg = {
        .speed_mode = BL_LEDC_MODE,
        .duty_resolution = BL_LEDC_RES,
        .timer_num = BL_LEDC_TIMER,
        .freq_hz = BL_LEDC_HZ,
        .clk_cfg = LEDC_AUTO_CLK,
    };
    ledc_timer_config(&timer_cfg);

    ledc_channel_config_t ch_cfg = {
        .gpio_num = PIN_BL,
        .speed_mode = BL_LEDC_MODE,
        .channel = BL_LEDC_CH,
        .intr_type = LEDC_INTR_DISABLE,
        .timer_sel = BL_LEDC_TIMER,
        .duty = 0, /* stay off until display_set_brightness() is called explicitly */
        .hpoint = 0,
    };
    ledc_channel_config(&ch_cfg);

    ledc_fade_func_install(0);
}

void display_set_brightness(uint8_t pct, uint16_t fade_ms)
{
    if(pct > 100) pct = 100;
    uint32_t duty = backlight_gamma_duty(pct);

    if(fade_ms == 0) {
        ledc_set_duty(BL_LEDC_MODE, BL_LEDC_CH, duty);
        ledc_update_duty(BL_LEDC_MODE, BL_LEDC_CH);
    }
    else {
        ledc_set_fade_with_time(BL_LEDC_MODE, BL_LEDC_CH, duty, fade_ms);
        ledc_fade_start(BL_LEDC_MODE, BL_LEDC_CH, LEDC_FADE_NO_WAIT);
    }

    ESP_LOGI(TAG, "backlight brightness %u%% (duty %u/%u%s)", pct, (unsigned)duty, BL_LEDC_MAX,
             fade_ms ? ", fading" : "");
}

/* Fills the whole 480x320 panel with the app's background colour, so any
 * border around a smaller-than-panel UI is the app's colour rather than
 * whatever garbage was in the panel's RAM at power-on. Reuses s_buf1. */
void display_clear_panel_bg(void)
{
    uint8_t *p = (uint8_t *)s_buf1;
    for(uint32_t i = 0; i < BUF_PIXELS; i++) {
        p[2 * i] = PANEL_BG_HI;
        p[2 * i + 1] = PANEL_BG_LO;
    }

    int rows_per_chunk = BUF_PIXELS / PANEL_W;
    if(rows_per_chunk < 1) rows_per_chunk = 1;
    for(int y = 0; y < PANEL_H; y += rows_per_chunk) {
        int y2 = y + rows_per_chunk - 1;
        if(y2 > PANEL_H - 1) y2 = PANEL_H - 1;

        lcd_set_window(0, y, PANEL_W - 1, y2);

        spi_transaction_t t;
        memset(&t, 0, sizeof(t));
        t.length = (size_t)PANEL_W * (size_t)(y2 - y + 1) * 16;
        t.tx_buffer = s_buf1;
        t.user = (void *)1;
        px_wait();
        spi_device_transmit(s_spi, &t);
    }
}

/* --------------------------------------------------------------- flush */

/* What has reached the panel, for the console's `ui` command: a blank screen
 * with the backlight on is told apart from a UI that never drew. */
static volatile uint32_t s_flush_calls;
static volatile uint32_t s_flush_px;

void display_flush_stats(uint32_t *calls, uint32_t *px)
{
    if(calls) *calls = s_flush_calls;
    if(px) *px = s_flush_px;
}

static void flush_cb(lv_display_t *disp, const lv_area_t *area, uint8_t *px_map)
{
    int32_t w = lv_area_get_width(area);
    int32_t h = lv_area_get_height(area);
    uint32_t px_count = (uint32_t)w * (uint32_t)h;

    (void)disp;
    s_flush_calls++;
    s_flush_px += px_count;

    /* Panel RAMWR expects RGB565 MSB-first on the wire; LV_COLOR_16_SWAP is
     * 0 in the shared lv_conf.h (the simulator needs native order), so swap
     * bytes here instead of touching that shared file. */
    lv_draw_rgb565_swap(px_map, px_count);

    /* The previous stripe must be on the panel before the window moves. */
    px_wait();

    /* Normally the UI window centred on the panel (0,0 once the UI fills
     * the whole panel); (0,0) unconditionally during touch calibration,
     * when this same display covers the whole panel regardless of the
     * UI's own size. */
    lcd_set_window(area->x1 + s_flush_off_x, area->y1 + s_flush_off_y,
                    area->x2 + s_flush_off_x, area->y2 + s_flush_off_y);

    /* Queued, not waited for: px_post_cb hands the buffer back to LVGL when
     * DMA is done, and LVGL draws the next stripe into the other buffer in
     * the meantime. */
    memset(&s_px_trans, 0, sizeof(s_px_trans));
    s_px_trans.length = (size_t)px_count * 16;
    s_px_trans.tx_buffer = px_map;
    s_px_trans.user = (void *)(DC_DATA | FLUSH_TAG);
    if(spi_device_queue_trans(s_spi, &s_px_trans, portMAX_DELAY) == ESP_OK) s_px_pending = true;
    else lv_display_flush_ready(s_disp);
}

/* ---------------------------------------------------------------- init */

lv_display_t *display_init(void)
{
    gpio_config_t dc_cfg = {
        .pin_bit_mask = 1ULL << PIN_DC,
        .mode = GPIO_MODE_OUTPUT,
    };
    gpio_config(&dc_cfg);

    backlight_init(); /* duty 0 — stays off until display_set_brightness() is called */

    spi_bus_config_t buscfg;
    memset(&buscfg, 0, sizeof(buscfg));
    buscfg.sclk_io_num = PIN_SCK;
    buscfg.mosi_io_num = PIN_MOSI;
    buscfg.miso_io_num = PIN_MISO;
    buscfg.quadwp_io_num = -1;
    buscfg.quadhd_io_num = -1;
    buscfg.max_transfer_sz = BUF_PIXELS * 2;

    /* Shared with the XPT2046 touch controller (touch.c adds its own
     * device to this same bus, CS33, once this call has set it up). */
    esp_err_t err = spi_bus_initialize(SPI2_HOST, &buscfg, SPI_DMA_CH_AUTO);
    if(err != ESP_OK) {
        ESP_LOGE(TAG, "spi_bus_initialize(SPI2_HOST) failed: %s", esp_err_to_name(err));
        return NULL;
    }

    spi_device_interface_config_t devcfg;
    memset(&devcfg, 0, sizeof(devcfg));
    devcfg.mode = 0;
    devcfg.clock_speed_hz = PANEL_SPI_HZ;
    devcfg.spics_io_num = PIN_CS;
    devcfg.queue_size = 4;
    devcfg.pre_cb = dc_pre_cb;
    devcfg.post_cb = px_post_cb;

    err = spi_bus_add_device(SPI2_HOST, &devcfg, &s_spi);
    if(err != ESP_OK) {
        ESP_LOGE(TAG, "spi_bus_add_device (panel) failed: %s", esp_err_to_name(err));
        return NULL;
    }

    detect_panel();

    lcd_cmd(0x01); /* SWRESET — RST is tied to EN, so this is the only reset path */
    vTaskDelay(pdMS_TO_TICKS(150));

    lcd_cmd(0x11); /* SLPOUT */
    vTaskDelay(pdMS_TO_TICKS(120));

    run_seq(s_unlock_seq, sizeof(s_unlock_seq) / sizeof(s_unlock_seq[0]));

    lcd_cmd(0x36); /* MADCTL (base, unflipped; display_set_flip() rewrites it later if needed) */
    lcd_data8(s_panel_info.madctl);

    lcd_cmd(0x3A); /* COLMOD */
    lcd_data8(0x55); /* 16 bpp */

    run_seq(s_power_seq, sizeof(s_power_seq) / sizeof(s_power_seq[0]));
    display_set_contrast(0); /* standard, the boot default until display_cfg loads a saved value */

    run_seq(s_lock_seq, sizeof(s_lock_seq) / sizeof(s_lock_seq[0]));

    lcd_cmd(s_panel_info.inversion_on ? 0x21 : 0x20); /* INVON / INVOFF */

    lcd_cmd(0x13); /* NORON */
    vTaskDelay(pdMS_TO_TICKS(10));

    lcd_cmd(0x29); /* DISPON */
    vTaskDelay(pdMS_TO_TICKS(50));

    /* Two stripe buffers in the 23 KB one 24-line buffer took (the heap needs
     * the rest: the link's model copy and the core's status copies). */
    s_buf1 = heap_caps_malloc(BUF_PIXELS * sizeof(uint16_t), MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
    s_buf2 = heap_caps_malloc(BUF_PIXELS * sizeof(uint16_t), MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
    if(!s_buf1 || !s_buf2) {
        ESP_LOGE(TAG, "failed to allocate two %u-byte DMA draw buffers",
                 (unsigned)(BUF_PIXELS * sizeof(uint16_t)));
        return NULL;
    }

    display_clear_panel_bg(); /* backlight is still off (duty 0): nothing flashes garbage */

    lv_display_t *disp = lv_display_create(UI_W, UI_H);
    lv_display_set_flush_cb(disp, flush_cb);
    s_disp = disp;   /* before the first flush: px_post_cb reports to it */
    lv_display_set_buffers(disp, s_buf1, s_buf2, BUF_PIXELS * sizeof(uint16_t),
                            LV_DISPLAY_RENDER_MODE_PARTIAL);
    s_disp = disp;

    ESP_LOGI(TAG,
             "display ready: UI %dx%d centred on panel %dx%d (offset %d,%d), draw buffers "
             "2 x %u bytes (DMA/internal, async); backlight still off, display_cfg applies it",
             UI_W, UI_H, PANEL_W, PANEL_H, OFFSET_X, OFFSET_Y,
             (unsigned)(BUF_PIXELS * sizeof(uint16_t)));

    return disp;
}

const panel_info_t *display_get_panel_info(void)
{
    return &s_panel_info;
}

void display_enter_calibration_mode(lv_display_t *disp)
{
    s_flush_off_x = 0;
    s_flush_off_y = 0;
    lv_display_set_resolution(disp, PANEL_W, PANEL_H);
}

void display_exit_calibration_mode(lv_display_t *disp)
{
    s_flush_off_x = OFFSET_X;
    s_flush_off_y = OFFSET_Y;
    lv_display_set_resolution(disp, UI_W, UI_H);
}

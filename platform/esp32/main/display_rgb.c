/**
 * @file display_rgb.c
 * The ES3C50P's panel (main/board.h): an ST7282E 800x480 on the ESP32-S3's
 * 16-bit RGB bus, driven by esp_lcd's RGB panel driver, plus the LVGL display
 * on top of it. Implements display.h. The ES3C43P's ST7265 800x480 is wired
 * the same and runs with the same timing.
 *
 * Pins, from the LCDWiki ES3C50P specification ("ESP32-S3 Pin Assignment"):
 * DE 40, VSYNC 41, HSYNC 39, PCLK 42; R0-R4 45 48 47 21 14; G0-G5 5 6 7 15
 * 16 4; B0-B4 8 3 46 9 1. The backlight is an SY7200 boost driver, PWM
 * dimmable, on GPIO2: the one GPIO the specification leaves unassigned, and
 * the same as on the Sunton ESP32-8048S050, whose pinout this board shares.
 *
 * The panel has no command interface: it only needs the timing below. Its
 * frame buffer is in PSRAM, and two bounce buffers in internal RAM feed the
 * DMA (PSRAM alone cannot keep up while the CPU uses it too, and the image
 * would drift). LVGL draws stripes into internal RAM; each flush copies its
 * stripe into the frame buffer.
 *
 * The LVGL display is UI_SCREEN_W x UI_SCREEN_H (src/ui/ui_theme.h), centred
 * on the panel; once the UI is laid out at the panel's size the offset is 0.
 */
#include "display.h"
#include "backlight.h"
#include "board.h"

#include "ui/ui_theme.h" /* UI_SCREEN_W / UI_SCREEN_H: the LVGL display always matches these */
#include "ui/ui_tokens.h"

#include <string.h>

#include "esp_heap_caps.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_rgb.h"
#include "esp_log.h"

#define PIN_DE    40
#define PIN_VSYNC 41
#define PIN_HSYNC 39
#define PIN_PCLK  42
#define PIN_BL    2

/* Data lines, D0 first: the frame buffer holds native RGB565, so bit 0 of a
 * pixel is blue's lowest bit and bit 15 red's highest. */
static const int s_data_pins[16] = {
    8, 3, 46, 9, 1,         /* B0-B4 */
    5, 6, 7, 15, 16, 4,     /* G0-G5 */
    45, 48, 47, 21, 14,     /* R0-R4 */
};

/* Timing. The panel latches on DE, so the porches only need to be legal;
 * these are the ones the Sunton ESP32-8048S050 runs this pinout with. The
 * pixel clock is what PSRAM sustains with bounce buffers: 800x480 at 16 MHz
 * is about 38 frames per second. Override any of them to tune a panel. */
#ifndef PANEL_PCLK_HZ
#define PANEL_PCLK_HZ (16 * 1000 * 1000)
#endif
#ifndef PANEL_HSYNC_PULSE
#define PANEL_HSYNC_PULSE 4
#endif
#ifndef PANEL_HSYNC_BACK
#define PANEL_HSYNC_BACK 8
#endif
#ifndef PANEL_HSYNC_FRONT
#define PANEL_HSYNC_FRONT 8
#endif
#ifndef PANEL_VSYNC_PULSE
#define PANEL_VSYNC_PULSE 4
#endif
#ifndef PANEL_VSYNC_BACK
#define PANEL_VSYNC_BACK 8
#endif
#ifndef PANEL_VSYNC_FRONT
#define PANEL_VSYNC_FRONT 8
#endif

#define PANEL_W BOARD_PANEL_W
#define PANEL_H BOARD_PANEL_H
#define UI_W UI_SCREEN_W
#define UI_H UI_SCREEN_H
#define OFFSET_X (((PANEL_W - UI_W) / 2) > 0 ? ((PANEL_W - UI_W) / 2) : 0)
#define OFFSET_Y (((PANEL_H - UI_H) / 2) > 0 ? ((PANEL_H - UI_H) / 2) : 0)

/* Two bounce buffers of this many lines feed the DMA from internal RAM. */
#define BOUNCE_LINES 10

/* LVGL's two stripe buffers, internal RAM (drawing there is faster than in
 * PSRAM): a fixed number of pixels each, 38.4 KB, which is 40 lines of a
 * 480-wide UI and 24 of an 800-wide one. More would come out of the heap the
 * printer link and Wi-Fi need. */
#define BUF_PIXELS 19200

static const char *TAG = "display";

static esp_lcd_panel_handle_t s_panel;
static panel_info_t s_panel_info;
static lv_display_t *s_disp;

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
    s_flush_calls++;
    s_flush_px += (uint32_t)lv_area_get_width(area) * (uint32_t)lv_area_get_height(area);

    /* Copies the stripe into the frame buffer before it returns (the
     * bounce buffers read it from there), so the buffer is LVGL's again. */
    esp_lcd_panel_draw_bitmap(s_panel, area->x1 + OFFSET_X, area->y1 + OFFSET_Y,
                              area->x2 + 1 + OFFSET_X, area->y2 + 1 + OFFSET_Y, px_map);
    lv_display_flush_ready(disp);
}

/* ------------------------------------------------------------ settings */

void display_set_contrast(uint8_t contrast)
{
    /* The panel has no gamma registers to reach over this bus. */
    (void)contrast;
}

void display_set_flip(bool flip)
{
    bool turned = flip != BOARD_ROTATED_180; /* the setting turns from the board's own orientation */

    esp_lcd_panel_mirror(s_panel, turned, turned);
    /* The mirror applies to what is copied from now on: redraw everything,
     * and repaint the border the UI never draws. */
    display_clear_panel_bg();
    if(s_disp) lv_obj_invalidate(lv_display_get_screen_active(s_disp));
    ESP_LOGI(TAG, "display flip: %s, forced full redraw", flip ? "on" : "off");
}

/* Fills the whole panel with the app's background colour, so the border
 * around a smaller-than-panel UI is the app's colour rather than whatever
 * PSRAM held at power-on. */
void display_clear_panel_bg(void)
{
    void *fb = NULL;

    if(esp_lcd_rgb_panel_get_frame_buffer(s_panel, 1, &fb) != ESP_OK || !fb) return;
    uint16_t bg = lv_color_to_u16(UI_TOK_BG);
    uint16_t *p = fb;
    for(uint32_t i = 0; i < (uint32_t)PANEL_W * PANEL_H; i++) p[i] = bg;
}

/* No calibration on a capacitive panel; the overlay is never shown. */
void display_enter_calibration_mode(lv_display_t *disp)
{
    (void)disp;
}

void display_exit_calibration_mode(lv_display_t *disp)
{
    (void)disp;
}

/* ---------------------------------------------------------------- init */

lv_display_t *display_init(void)
{
    s_panel_info.name = BOARD_PANEL_NAME " (RGB)";
    s_panel_info.id_read_ok = true; /* nothing to read: the RGB bus is one-way */

    backlight_init(PIN_BL); /* duty 0 — stays off until display_set_brightness() is called */

    esp_lcd_rgb_panel_config_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.clk_src = LCD_CLK_SRC_DEFAULT;
    cfg.timings.pclk_hz = PANEL_PCLK_HZ;
    cfg.timings.h_res = PANEL_W;
    cfg.timings.v_res = PANEL_H;
    cfg.timings.hsync_pulse_width = PANEL_HSYNC_PULSE;
    cfg.timings.hsync_back_porch = PANEL_HSYNC_BACK;
    cfg.timings.hsync_front_porch = PANEL_HSYNC_FRONT;
    cfg.timings.vsync_pulse_width = PANEL_VSYNC_PULSE;
    cfg.timings.vsync_back_porch = PANEL_VSYNC_BACK;
    cfg.timings.vsync_front_porch = PANEL_VSYNC_FRONT;
    cfg.timings.flags.pclk_active_neg = 1;
    cfg.data_width = 16;
    cfg.in_color_format = LCD_COLOR_FMT_RGB565;
    cfg.out_color_format = LCD_COLOR_FMT_RGB565;
    cfg.num_fbs = 1;
    cfg.bounce_buffer_size_px = PANEL_W * BOUNCE_LINES;
    cfg.dma_burst_size = 64;
    cfg.hsync_gpio_num = PIN_HSYNC;
    cfg.vsync_gpio_num = PIN_VSYNC;
    cfg.de_gpio_num = PIN_DE;
    cfg.pclk_gpio_num = PIN_PCLK;
    cfg.disp_gpio_num = -1;
    for(int i = 0; i < 16; i++) cfg.data_gpio_nums[i] = s_data_pins[i];
    cfg.flags.fb_in_psram = 1;

    esp_err_t err = esp_lcd_new_rgb_panel(&cfg, &s_panel);
    if(err != ESP_OK) {
        ESP_LOGE(TAG, "esp_lcd_new_rgb_panel failed: %s", esp_err_to_name(err));
        return NULL;
    }
    err = esp_lcd_panel_reset(s_panel);
    if(err == ESP_OK) err = esp_lcd_panel_init(s_panel);
    if(err != ESP_OK) {
        ESP_LOGE(TAG, "RGB panel init failed: %s", esp_err_to_name(err));
        return NULL;
    }

    esp_lcd_panel_mirror(s_panel, BOARD_ROTATED_180, BOARD_ROTATED_180);
    display_clear_panel_bg(); /* backlight is still off (duty 0): nothing flashes garbage */

    uint16_t *buf1 = heap_caps_malloc(BUF_PIXELS * sizeof(uint16_t), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    uint16_t *buf2 = heap_caps_malloc(BUF_PIXELS * sizeof(uint16_t), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if(!buf1 || !buf2) {
        ESP_LOGE(TAG, "failed to allocate two %u-byte draw buffers",
                 (unsigned)(BUF_PIXELS * sizeof(uint16_t)));
        return NULL;
    }

    lv_display_t *disp = lv_display_create(UI_W, UI_H);
    lv_display_set_flush_cb(disp, flush_cb);
    lv_display_set_buffers(disp, buf1, buf2, BUF_PIXELS * sizeof(uint16_t),
                           LV_DISPLAY_RENDER_MODE_PARTIAL);
    s_disp = disp;

    ESP_LOGI(TAG,
             "display ready: UI %dx%d centred on panel %dx%d (offset %d,%d), RGB %u Hz, frame "
             "buffer in PSRAM, draw buffers 2 x %u bytes; backlight still off, display_cfg "
             "applies it",
             UI_W, UI_H, PANEL_W, PANEL_H, OFFSET_X, OFFSET_Y, (unsigned)PANEL_PCLK_HZ,
             (unsigned)(BUF_PIXELS * sizeof(uint16_t)));

    return disp;
}

const panel_info_t *display_get_panel_info(void)
{
    return &s_panel_info;
}

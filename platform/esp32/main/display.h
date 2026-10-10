/**
 * @file display.h
 * ST7796S SPI display bring-up for the 4.0" board (HSPI: SCK14 MOSI13
 * MISO12 CS15 DC2, backlight GPIO27 via LEDC PWM; RST is tied to EN, so
 * bring-up is a software reset only), plus the LVGL display driver on top
 * of it.
 *
 * The panel is 480x320 (landscape); the LVGL display is created at
 * UI_SCREEN_W x UI_SCREEN_H (src/ui/ui_theme.h) and flushed centred on the
 * panel — see PANEL_W/H, UI_W/H and OFFSET_X/Y in display.c and touch.c.
 * Today UI_SCREEN_W/H (320x240) is smaller than the panel, so there is a
 * border; once the UI fills the whole panel the offset is 0 automatically.
 */
#ifndef DISPLAY_H
#define DISPLAY_H

#include <stdint.h>
#include <stdbool.h>

#include "lvgl.h"

typedef struct {
    uint32_t id_0xd3;    /* RDID4 response (3 bytes, packed MSB-first); ST7796S: 0x007796 */
    bool     id_read_ok; /* true if id_0xd3's low 16 bits matched 0x7796 */
    bool     inversion_on;
    bool     bgr;
    uint8_t  madctl;
    uint32_t spi_hz;
} panel_info_t;

/* Brings up the SPI bus + panel, reads the ID, runs the ST7796S init
 * sequence, clears the full 480x320 panel to the app's background colour,
 * turns the backlight on, and creates a 320x240 LVGL display centred on the
 * panel with two 320x24-line DMA-capable partial draw buffers. Returns
 * NULL on failure (SPI bus or device add error). */
lv_display_t *display_init(void);

/* What display_init() detected/decided. Valid only after display_init(). */
const panel_info_t *display_get_panel_info(void);

/* How many flushes, and how many pixels, have reached the panel since boot. */
void display_flush_stats(uint32_t *calls, uint32_t *px);

/* Switches the display between the centred 320x240 UI window (flushed with
 * an 80,40 offset) and full 480x320 panel coverage (offset 0,0), for the
 * touch calibration overlay, which draws directly in panel coordinates.
 * touch.c's calibration path uses PANEL_W/PANEL_H = 480/320 to match. */
void display_enter_calibration_mode(lv_display_t *disp);
void display_exit_calibration_mode(lv_display_t *disp);

/* Fills the whole 480x320 panel with the app's background colour (raw SPI,
 * bypassing LVGL). display_init() calls this once at boot; the touch
 * calibration teardown calls it again afterward, since the calibration
 * overlay painted the border area (outside the 320x240 UI window) that the
 * app's own screen never repaints. */
void display_clear_panel_bg(void);

/* Backlight (LEDC PWM, gamma-corrected). fade_ms=0 sets it immediately;
 * otherwise fades from whatever the duty currently is. display_init()
 * leaves the backlight at 0 (off) — nothing calls this until display_cfg
 * loads a saved (or default) brightness at boot. */
void display_set_brightness(uint8_t pct, uint16_t fade_ms);

/* ST7796S gamma preset: 0 = standard, 1 = high contrast (steeper curve).
 * Rewrites E0/E1 at runtime, under the same SPI serialization as the
 * flush (both run from the same task — see touch.c's file comment). */
void display_set_contrast(uint8_t contrast);

/* Rewrites MADCTL for a 180-degree rotation (XORs MX|MY onto the base
 * rotation bits detect_panel() computed). touch_set_flip() must be given
 * the same value to keep the touch mapping matching what's on screen. */
void display_set_flip(bool flip);

#endif /* DISPLAY_H */

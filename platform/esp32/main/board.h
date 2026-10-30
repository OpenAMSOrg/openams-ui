/**
 * @file board.h
 * Which display board this image is for, and what that board has. One image
 * is built for one board: platform/esp32/CMakeLists.txt sets MMU_BOARD (and
 * with it the chip target) and defines MMU_BOARD_<NAME> for every component.
 *
 * - E32R40T (default): LCDWiki 4.0" ESP32-32E, ESP32 without PSRAM, ST7796S
 *   480x320 over SPI, XPT2046 resistive touch, RGB status LED.
 * - ES3C50P: LCDWiki / Hosyond 5.0" ESP32-S3 (N16R8), ST7282E 800x480 over a
 *   16-bit RGB bus, capacitive touch on I2C, no status LED.
 * - ES3C43P: LCDWiki / Hosyond 4.3" ESP32-S3 (N16R8), the ES3C50P's twin: the
 *   same pins, an ST7265 800x480 on the same bus, the same touch.
 */
#ifndef BOARD_H
#define BOARD_H

#if defined(MMU_BOARD_ES3C50P)
#define BOARD_NAME "ES3C50P"
#define BOARD_PANEL_NAME "ST7282E"
#define BOARD_PANEL_W 800
#define BOARD_PANEL_H 480
/* Capacitive touch reports panel coordinates: no calibration screen. */
#define BOARD_TOUCH_NEEDS_CALIBRATION 0
/* GPIO16/17/22 are LCD data and the expansion port here, not an LED. */
#define BOARD_HAS_RGB_LED 0
/* The image (and touch) are turned 180 degrees from the panel's own scan
 * order: the way the owner mounts this board (2026-10-27). The display
 * settings' flip turns it from there. */
#define BOARD_ROTATED_180 1
#elif defined(MMU_BOARD_ES3C43P)
#define BOARD_NAME "ES3C43P"
#define BOARD_PANEL_NAME "ST7265"
#define BOARD_PANEL_W 800
#define BOARD_PANEL_H 480
#define BOARD_TOUCH_NEEDS_CALIBRATION 0
#define BOARD_HAS_RGB_LED 0
/* Turned 180 degrees like the ES3C50P: upright with the USB-C on the right
 * and the connectors along the top, as the spool station holds it (checked on
 * the board, 2026-10-30). */
#define BOARD_ROTATED_180 1
#else
#define BOARD_NAME "E32R40T"
#define BOARD_PANEL_W 480
#define BOARD_PANEL_H 320
#define BOARD_TOUCH_NEEDS_CALIBRATION 1
#define BOARD_HAS_RGB_LED 1
#define BOARD_ROTATED_180 0
#endif

#endif /* BOARD_H */

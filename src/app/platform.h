/**
 * @file platform.h
 * Platform hooks beyond actions.h's platform_action(): things a screen can
 * ask the platform to do that have no meaningful effect on some platforms.
 */
#ifndef PLATFORM_H
#define PLATFORM_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/** Re-run touch calibration. ESP32: erases the stored fit and reboots into
 * the calibration screen app_main() runs whenever none is stored. The
 * simulator (desktop, Windows, web — all platform/sim) has no touchscreen
 * to calibrate, so it is a no-op that logs. */
void platform_touch_calibrate(void);

/** Display settings a screen can read and change. ESP32: backlight
 * brightness (PWM), an ST7796S gamma preset, the dim/off inactivity timers
 * and a 180-degree flip (with the touch mapping mirrored to match).
 * Persisted; loaded and applied at boot before the backlight comes on.
 * The simulator has no backlight/gamma/orientation to drive, so it is a
 * static struct that just logs each change. */
typedef struct {
    uint8_t brightness_pct;  /**< 10..100, step 10; default 100 */
    uint8_t contrast;        /**< 0 = standard, 1 = high; default 0 */
    uint16_t dim_after_s;    /**< 0 = never, else 60/300/600; default 300 (5 min) */
    uint16_t off_after_s;    /**< backlight off: 0 = never, else 600/900/1800; default 900
                              * (15 min); never shorter than dim_after_s */
    bool flip;               /**< rotate 180 degrees; default false */
} platform_display_cfg_t;

void platform_display_get(platform_display_cfg_t *out);
void platform_display_set(const platform_display_cfg_t *cfg); /* applies and persists */

/** The language the device last ran in, as a language code from
 * design/strings ("en", "ja", ...). platform_language_get() writes "" into
 * @p out when nothing is stored yet, so a first boot starts on whatever
 * app_init() falls back to. Persisted separately from the display settings
 * and restored before the first screen is drawn. The simulator keeps it in
 * memory for the session (it serves the browser build too, which has no
 * storage); the ESP32 keeps it in NVS. */
void platform_language_get(char *out, size_t n);
void platform_language_set(const char *code);

#endif /* PLATFORM_H */

/**
 * @file touch.h
 * XPT2046 resistive touch, sharing the display's SPI bus (HSPI: SCK14
 * MOSI13 MISO12) with its own CS33 device and IRQ36, exposed to LVGL as a
 * pointer indev.
 *
 * touch_init() adds its device to that bus, so display_init() MUST run
 * first.
 */
#ifndef TOUCH_H
#define TOUCH_H

#include <stdbool.h>
#include <stdint.h>

#include "lvgl.h"

/* Adds the touch device to the (already-initialised) display SPI bus and
 * registers an LVGL pointer indev backed by it. Returns NULL on failure. */
lv_indev_t *touch_init(void);

/* Raw sample for the calibration screen and boot-time diagnostics only:
 * bypasses the affine transform, but reuses the same IRQ-gate, pressure and
 * spread filtering the LVGL indev callback uses. Returns false if not
 * currently pressed, or the reading was rejected (unstable / too light). */
bool touch_acquire_raw(uint16_t *raw_x, uint16_t *raw_y, uint16_t *z1);

/* Same as touch_acquire_raw(), but with the pressure floor and per-axis
 * spread ceiling as parameters instead of the normal-mode constants —
 * calibration needs both relaxed, since resistive panels read lower
 * pressure and noisier positions near the corners. */
bool touch_acquire_raw_ex(uint16_t *raw_x, uint16_t *raw_y, uint16_t *z1, uint16_t z1_min,
                          uint16_t spread_max);

/* True while the pen-down IRQ line reads active (low) — no SPI traffic. */
bool touch_is_pressed_raw(void);

/* The 6-coefficient affine raw->panel transform the indev callback applies:
 * x = coef[0]*rx + coef[1]*ry + coef[2]; y = coef[3]*rx + coef[4]*ry + coef[5].
 * touch_init() seeds a harmless identity-ish default; the real one comes
 * from NVS (touch_calib_store_load()) or a fresh calibration run
 * (touch_calib_run()) at boot. */
void touch_set_affine(const float coef[6]);

/** When the last press was recognized (esp_timer_get_time() us), or 0; taking
 * it clears it. The `perf` command measures press-to-screen latency with it. */
int64_t touch_take_press_us(void);
void touch_get_affine(float coef[6]);

/* Mirrors the touch mapping to match display_set_flip(flip): applied to
 * the panel-space point AFTER the affine transform (x'=479-x, y'=319-y),
 * so a stored calibration is unaffected by the flip setting. */
void touch_set_flip(bool flip);

/* Marks the CURRENT (already in progress) touch contact as consumed by
 * platform power management (a wake-from-dim/off tap): the LVGL indev
 * reports RELEASED for the rest of this contact, so it never reaches a
 * widget as a click. Cleared automatically once the physical contact ends. */
void touch_swallow_current_press(void);

#endif /* TOUCH_H */

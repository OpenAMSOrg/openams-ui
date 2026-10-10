/**
 * @file touch_calib_store.h
 * Persists the 6-coefficient touch affine transform in NVS (namespace
 * "touch"), so a calibration survives a reboot.
 */
#ifndef TOUCH_CALIB_STORE_H
#define TOUCH_CALIB_STORE_H

#include <stdbool.h>

/* Brings up NVS (idempotent; erases and retries on a version mismatch or a
 * full partition, same as every other ESP-IDF nvs_flash_init() caller).
 * Call once at boot before any load/save. */
void touch_calib_store_init(void);

/* Loads the 6 affine coefficients from NVS. Returns false if absent, the
 * wrong size, or the stored fit fails a basic sanity check (degenerate
 * linear part, NaN/Inf, or an absurdly large coefficient) — any of which
 * means app_main() should run touch_calib_run() instead of trusting it. */
bool touch_calib_store_load(float coef[6]);

/* Saves the 6 coefficients. */
void touch_calib_store_save(const float coef[6]);

#endif /* TOUCH_CALIB_STORE_H */

/**
 * @file display_cfg_store.h
 * Persists the display settings (src/app/platform.h's platform_display_cfg_t)
 * in NVS (namespace "display"), and implements platform_display_get()/
 * platform_display_set() for this platform.
 */
#ifndef DISPLAY_CFG_STORE_H
#define DISPLAY_CFG_STORE_H

/* Brings up NVS (idempotent — safe to call alongside touch_calib_store's
 * own nvs_flash_init()), loads the saved config (or the documented
 * defaults on first boot, which it then persists), and applies it to the
 * hardware: contrast, flip (display + touch mirroring), then brightness
 * last — this is what turns the backlight on for the first time. Call
 * once at boot, after display_init()/touch_init(), before anything that
 * needs the screen visible (the touch calibration screen included). */
void display_cfg_store_init(void);

#endif /* DISPLAY_CFG_STORE_H */

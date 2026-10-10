/**
 * @file display_power.h
 * Dim/off-on-inactivity and wake-on-tap, driven by platform_display_cfg_t's
 * dim_after_s/off_after_s.
 */
#ifndef DISPLAY_POWER_H
#define DISPLAY_POWER_H

#include "lvgl.h"

/* Call once at boot, after display_init(). */
void display_power_init(lv_display_t *disp);

/* Call every lvgl_task iteration (alongside lv_timer_handler()). Cheap: one
 * GPIO read plus, at most, one LVGL API call. */
void display_power_tick(void);

#endif /* DISPLAY_POWER_H */

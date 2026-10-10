/**
 * @file touch_calib_ui.h
 * Interactive 4-point touch calibration, drawn at the full 480x320 panel
 * resolution.
 */
#ifndef TOUCH_CALIB_UI_H
#define TOUCH_CALIB_UI_H

#include "lvgl.h"

/* Runs the calibration: switches @p disp to full-panel mode, shows a
 * crosshair target at each of 4 corners in turn (averaging a stable press
 * at each), fits an affine raw->panel transform by least squares, applies
 * it (touch_set_affine()) and saves it (touch_calib_store_save()), shows a
 * 5s "tap anywhere" test screen, then restores @p disp to the centred
 * 320x240 UI window. Blocks (pumps lv_timer_handler() itself) until done —
 * safe to call before the LVGL task task exists (app_main(), at boot) or
 * from within it. */
void touch_calib_run(lv_display_t *disp);

#endif /* TOUCH_CALIB_UI_H */

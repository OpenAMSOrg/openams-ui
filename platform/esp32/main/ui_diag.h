/**
 * @file ui_diag.h
 * The console's `ui` command: what the UI task is doing, told from the serial
 * port, so a blank screen can be diagnosed without looking at it.
 */
#ifndef UI_DIAG_H
#define UI_DIAG_H

/* Ask the UI task to print its state on its next loop (it alone may touch
 * LVGL): flushes and pixels sent to the panel, the active screen's objects,
 * LVGL's memory, the heap, and the UI and startup tasks' stack headroom. */
void ui_diag_request(void);

/* Ask the UI task to print its timing since the last `perf`, then reset it:
 * refreshes and their render+flush time, pixels sent, the loop's and the
 * app's per-pass time, screen builds, model syncs, and press-to-screen
 * latency. */
void ui_perf_request(void);

/* Ask the UI task to go to screen @p id (ui_nav.h), timed like a tap would. */
void ui_nav_request(int id);

/* Ask the UI task to tap the screen at (@p x, @p y): a press held for
 * @p hold_ms, then a release, through a second LVGL pointer, exactly as a
 * finger would arrive. For 600 ms afterwards the task prints the top layer's
 * objects (position and size) whenever they change, to see a modal open. */
void ui_tap_request(int x, int y, int hold_ms);

#endif

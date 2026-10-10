/**
 * @file ui_widgets.h
 * The few pieces every screen shares: top bar and the global menu and toast.
 */

#ifndef UI_WIDGETS_H
#define UI_WIDGETS_H

#include "lvgl.h"
#include <stdint.h>

/** Two small ints carried as one pointer through LVGL's event user_data. */
#define UI_ENC(a, b)  ((void *)(intptr_t)((((a) & 0x3F) << 6) | ((b) & 0x3F)))
#define UI_DEC_A(p)   ((int)((((intptr_t)(p)) >> 6) & 0x3F))
#define UI_DEC_B(p)   ((int)(((intptr_t)(p)) & 0x3F))

/** The top bar: optional back arrow, a title (long-press opens the hidden
 * debug screen) and the alert bell. */
lv_obj_t *ui_top_bar(lv_obj_t *parent, const char *title, bool with_back);

/** The modal "Menu" grid: one button per screen. Tapping one logs, closes the
 * modal and navigates. Used by the bottom nav's Menu button and the home
 * "Screens" button. */
void ui_open_menu(void);

/** Remove a live toast, if any. Called on navigation. */
void ui_toast_close(void);

#endif /* UI_WIDGETS_H */

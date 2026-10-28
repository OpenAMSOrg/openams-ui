/**
 * @file ui_sheet.h
 * Bottom sheets: the frame every dialog, the Menu and the toast are built
 * from. A sheet lives on lv_layer_top(), not on a screen, so it survives
 * screen changes and is covered by the simulator's snapshots.
 *
 * See docs/VISUAL_SPEC_480.md, "Slot sheet" (the frame and the rows) and
 * "Motion" (the scrim and the sheet entrance).
 */

#ifndef UI_SHEET_H
#define UI_SHEET_H

#include <stdbool.h>
#include <stdint.h>

#include "lvgl.h"
#include "ui/ui_scale.h"

/** Smallest and largest visible height of a sheet, in pixels. */
#define UI_SHEET_MIN_H UI_PX(60)
#define UI_SHEET_MAX_H (UI_SCREEN_H - UI_PX(48))

/** Open a sheet @p h tall (clamped to UI_SHEET_MIN_H..UI_SHEET_MAX_H) with a
 * @p title, over a clickable scrim. @p h is only the height shown before the
 * first fit: ui_sheet_row()/ui_sheet_button() re-fit the sheet to its content
 * as they add it (see ui_sheet_fit()), so the final height tracks whatever
 * ends up in the body. A click on the scrim itself closes it. Any sheet
 * already open is closed first. Returns the sheet. */
lv_obj_t *ui_sheet_open(const char *title, int h);

/** The column of rows inside @p sheet. Rows and buttons go in here. */
lv_obj_t *ui_sheet_body(lv_obj_t *sheet);

/** Resize @p sheet to fit its body's current content: title area (48 px) +
 * content height + 12 px bottom padding, clamped to
 * UI_SHEET_MIN_H..UI_SHEET_MAX_H (above the clamp the body scrolls instead of
 * growing further). ui_sheet_row() and ui_sheet_button() call this after
 * adding their child; call it directly after adding content to the body some
 * other way (a custom grid, a label added straight to ui_sheet_body()). */
void ui_sheet_fit(lv_obj_t *sheet);

/** Title-row accessories. @p swatch_rgb is a filament color packed as
 * 0xRRGGBB (negative = no swatch); the swatch shifts the title right.
 * @p right_text is right-aligned, with an optional NFC icon to its left. */
void ui_sheet_header(lv_obj_t *sheet, int32_t swatch_rgb, const char *right_text,
                     bool tag_icon);

/** A 40 px row (56 px when @p reason is set). With no reason the row is
 * enabled: it is clickable, @p primary tints its icon, and a chevron invites
 * the tap. With a reason the row is disabled: the reason explains why. */
lv_obj_t *ui_sheet_row(lv_obj_t *sheet, const char *icon, const char *label,
                       const char *reason, lv_event_cb_t cb, void *user, bool primary);

/** A 36 px button in the strip along the bottom of the body. The strip is
 * created by the first call; buttons share its width. */
lv_obj_t *ui_sheet_button(lv_obj_t *sheet, const char *label, lv_event_cb_t cb,
                          void *user, bool primary);

/** Delete @p sheet at once. NULL closes whichever sheet is open. */
void ui_sheet_close(lv_obj_t *sheet);

/** Open the Menu sheet: a grid of tiles, one per screen. */
void ui_sheet_menu(void);

/** Show @p text in a pill near the bottom of the screen for 1500 ms. A
 * toast already up is replaced, so there is only ever one. */
void ui_sheet_toast(const char *text);

/** Remove the toast now, if there is one. */
void ui_sheet_toast_close(void);

#endif /* UI_SHEET_H */

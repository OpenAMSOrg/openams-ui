/**
 * @file ui_spool_sheets.h
 * Two bottom sheets for the Spoolman link flow (D4a):
 *
 *   ui_spool_confirm_open   "New spool, full?" — confirms the weight of a newly
 *                           detected spool via `spool confirm <slot> <grams>`.
 *
 *   ui_spool_assign_open    "Assign spool" — picks a Spoolman spool from the
 *                           cached list and links it via `link <slot> <id>`.
 */

#ifndef UI_SPOOL_SHEETS_H
#define UI_SPOOL_SHEETS_H

/**
 * Open the "New spool, full?" sheet for the slot at (@p unit, @p slot).
 * The sheet asks whether the spool is full or lets the user pick a percentage.
 * Logs `spool confirm <slot> <grams>` when the user confirms.
 */
void ui_spool_confirm_open(int unit, int slot);

/**
 * Open the "Assign spool" list sheet for the slot at (@p unit, @p slot).
 * Shows the cached g_mmu.spools list (or three demo spools in non-live mode).
 * Logs `link <slot> <spool_id>` on a tap; also shows "Unlink" when the slot
 * already has a spool linked.
 */
void ui_spool_assign_open(int unit, int slot);

#endif /* UI_SPOOL_SHEETS_H */

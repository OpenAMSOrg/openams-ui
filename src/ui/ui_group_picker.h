/**
 * @file ui_group_picker.h
 * The "change group" sheet, shared by the slot sheet (home), the Groups
 * screen's "Not in a group" rows, and the print mapping screen's tool rows.
 * See docs/GROUPS.md.
 */

#ifndef UI_GROUP_PICKER_H
#define UI_GROUP_PICKER_H

/** Open a sheet for slot (@p unit, @p slot): lists this lane's groups the bay
 * could join (name + members' ids), "Remove from group" when the bay is
 * already assigned, and "New group". Picking one calls the model helper,
 * closes the sheet, toasts "<id> -> <group>" and refreshes the active
 * screen. */
void ui_group_picker_open(int unit, int slot);

/** Open a sheet listing this unit's own bays with real filament, to assign
 * one of them into @p group - used from the print mapping screen, where the
 * group to fill is already known (the project's tool). Same toast/refresh
 * behavior as ui_group_picker_open(). */
void ui_group_assign_bay_open(int group);

#endif /* UI_GROUP_PICKER_H */

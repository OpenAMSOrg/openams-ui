/**
 * @file gallery_sheet.c
 * Gallery page for the sheet family: the slot sheet with its header, one
 * enabled row and one with a reason (the disabled state).
 */

#include "gallery.h"

#include "model/mmu_model.h"
#include "ui/ui_icons.h"
#include "ui/ui_sheet.h"

void gallery_page_sheet(lv_obj_t *scr)
{
    lv_obj_t *sheet;

    (void)scr; /* a sheet lives on lv_layer_top(), not on the page */

    mmu_model_set_scenario("idle");

    sheet = ui_sheet_open("A2 \xC2\xB7 PETG-CF", 192);
    ui_sheet_header(sheet, 0x22262B, "45%", false);
    ui_sheet_row(sheet, UI_ICON_TRAY_ARROW_DOWN, "Load", NULL, NULL, NULL, true);
    ui_sheet_row(sheet, UI_ICON_PENCIL_OUTLINE, "Edit", NULL, NULL, NULL, false);
    ui_sheet_row(sheet, UI_ICON_REFRESH, "Re-read", "No tag in this slot", NULL, NULL, false);
}

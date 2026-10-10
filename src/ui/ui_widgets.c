/**
 * @file ui_widgets.c
 * Shared widgets. Everything is laid out with explicit coordinates so the same
 * code can serve the 74 px "four across" tile and the 152 px single-slot tile.
 */

#include "ui/ui_widgets.h"

#include "ui/ui_bars.h"
#include "ui/ui_sheet.h"

lv_obj_t *ui_top_bar(lv_obj_t *parent, const char *title, bool with_back)
{
    return ui_bar_top(parent, title, with_back);
}

void ui_open_menu(void)
{
    ui_sheet_menu();
}

void ui_toast_close(void)
{
    ui_sheet_toast_close();
}

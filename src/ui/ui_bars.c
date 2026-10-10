/**
 * @file ui_bars.c
 * The chrome the screens that predate the restyle share (see ui_bars.h).
 */

#include "ui/ui_bars.h"

#include "ui/ui_kit.h"

lv_obj_t *ui_bar_top(lv_obj_t *parent, const char *title, bool with_back)
{
    (void)with_back;
    return ui_kit_header(parent, title, true);
}

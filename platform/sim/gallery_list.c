/**
 * @file gallery_list.c
 * Gallery page for the list family: a scrolling column of rows, one with a
 * toggle stand-in in its trailing slot and one with a very long title.
 */

#include "gallery.h"

#include "ui/ui_icons.h"
#include "ui/ui_list.h"
#include "ui/ui_theme.h"

static void on_row_click(lv_event_t *e)
{
    (void)e;
}

/* Stand-in for a toggle: a 40x24 accent capsule. */
static void add_toggle(lv_obj_t *slot)
{
    lv_obj_t *tog = lv_obj_create(slot);

    lv_obj_remove_style_all(tog);
    lv_obj_set_size(tog, 40, 24);
    lv_obj_set_style_bg_color(tog, UI_COL_ACCENT, 0);
    lv_obj_set_style_bg_opa(tog, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(tog, UI_RADIUS_PILL, 0);
    lv_obj_set_scrollable(tog, false);
}

/* Row 4's title runs to 399 px at UI_FONT_M, close to the full 456 px list
 * row width the real screens use, so a narrower column would elide it with
 * dots and the layout audit would report the missing words. Let this one
 * title wrap instead: the whole sentence stays visible across two lines and
 * the row grows to fit. */
static void wrap_row_title(lv_obj_t *row)
{
    lv_obj_t *text = lv_obj_get_child(row, 0);   /* no icon: child 0 is the text block */
    lv_obj_t *title = lv_obj_get_child(text, 0);

    lv_label_set_long_mode(title, LV_LABEL_LONG_MODE_WRAP);
}

void gallery_page_list(lv_obj_t *scr)
{
    /* The real list row width (x = 12, w = 456); tall enough for all five
     * rows to fit without scrolling now that there is a 320 px screen to use. */
    lv_obj_t *col = ui_list_column(scr, 12, 8, 456, 300);
    lv_obj_t *row;

    row = ui_list_row(col, UI_ICON_NFC_VARIANT, "Read filament on insertion",
                      "Read the spool tag when a spool is inserted (about 20 s)",
                      NULL, NULL, NULL);
    add_toggle(ui_list_row_trailing(row));

    ui_list_row(col, NULL, "Firmware", NULL, "01.07.00.00", NULL, NULL);

    ui_list_row(col, UI_ICON_LINK_VARIANT, "This display", "AMS 2 Pro (A)", NULL,
                on_row_click, NULL);

    row = ui_list_row(col, NULL, "Auto-refill switches to an identical spool when one runs out",
                      NULL, NULL, NULL, NULL);
    wrap_row_title(row);

    ui_list_row(col, NULL, "B3 info",
                "Manual filament information is used for B3 because the tag could not be read",
                "2 min", NULL, NULL);
}

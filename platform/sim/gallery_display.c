/**
 * @file gallery_display.c
 * Gallery page for the display family: the hero readout, the progress ring,
 * the status pills, the filament swatches, a list-section caption and the
 * empty state, laid out the way a real screen would stack them.
 *
 * The caption row is narrower than a real list would make it: the empty state
 * beside it owns the right-hand column, and the caption may not run under it.
 */

#include "gallery.h"

#include "ui/ui_display.h"
#include "ui/ui_icons.h"
#include "ui/ui_theme.h"

/* A transparent, non-scrolling holder so a component can be placed at an
 * absolute spot on the page. */
static lv_obj_t *mk_holder(lv_obj_t *scr, int x, int y, int w, int h)
{
    lv_obj_t *o = lv_obj_create(scr);

    lv_obj_remove_style_all(o);
    lv_obj_set_pos(o, x, y);
    lv_obj_set_size(o, w, h);
    lv_obj_set_scrollable(o, false);
    lv_obj_set_style_bg_opa(o, LV_OPA_TRANSP, 0);
    return o;
}

void gallery_page_display(lv_obj_t *scr)
{
    lv_obj_t *row;
    lv_obj_t *sec;
    lv_obj_t *empty;

    /* readout */
    ui_hero_value(scr, 16, 8, "31", "% RH");
    ui_hero_value(scr, 172, 8, "24", "\xC2\xB0" "C");

    /* rings: empty, just started, half, full */
    ui_ring(scr, 16, 52, 32, 0, UI_COL_WARN);
    ui_ring(scr, 60, 52, 32, 3, UI_COL_WARN);
    ui_ring(scr, 104, 52, 32, 50, UI_COL_ACCENT);
    ui_ring(scr, 148, 52, 32, 100, UI_COL_ACCENT);

    /* pills, packed left to right */
    row = mk_holder(scr, 16, 96, LV_SIZE_CONTENT, 20);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(row, 6, 0);
    ui_pill(row, "In use", UI_STYLE_PILL_ACCENT);
    ui_pill(row, "Update available", UI_STYLE_PILL_WARN);
    ui_pill(row, "Tag", UI_STYLE_PILL);
    ui_pill(row, "Error", UI_STYLE_PILL_ERR);

    /* swatches: bright red, charcoal, white, blue; the round one is a full circle */
    ui_swatch(scr, 0xD23B3B, 16, 128, 24, false);
    ui_swatch(scr, 0x22262B, 48, 128, 24, false);
    ui_swatch(scr, 0xE8E8E8, 80, 128, 24, false);
    ui_swatch(scr, 0x2F6FB2, 112, 128, 24, true);

    /* a caption row, and the empty state beside it */
    sec = mk_holder(scr, 10, 162, 150, 20);
    ui_section_label(sec, "Filament detection");

    empty = mk_holder(scr, 164, 162, 150, 70);
    ui_empty_state(empty, UI_ICON_CHECK_CIRCLE, "All clear");
}

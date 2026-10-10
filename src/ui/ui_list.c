/**
 * @file ui_list.c
 * The list family: a scrolling column and the rows the slot sheet is built
 * from. A row is an optional leading icon, one or two lines of text and a
 * trailing slot. The slot is always the row's last child, so a caller can put
 * a toggle or a pill in it (ui_list_row_trailing).
 */

#include "ui/ui_list.h"

#include "ui/ui_icons.h"
#include "ui/ui_theme.h"

/* --------------------------------------------------------------- helpers */

static lv_obj_t *mk_label(lv_obj_t *parent, const char *text, const lv_font_t *font,
                          lv_color_t color, lv_label_long_mode_t mode)
{
    lv_obj_t *l = lv_label_create(parent);

    lv_obj_remove_style_all(l);
    lv_label_set_text(l, text);
    lv_label_set_long_mode(l, mode);
    lv_obj_set_style_text_font(l, font, 0);
    lv_obj_set_style_text_color(l, color, 0);
    lv_obj_set_scrollable(l, false);
    return l;
}

/* ------------------------------------------------------------ list column */

lv_obj_t *ui_list_column(lv_obj_t *parent, int x, int y, int w, int h)
{
    lv_obj_t *col = lv_obj_create(parent);

    lv_obj_remove_style_all(col);
    lv_obj_set_pos(col, x, y);
    lv_obj_set_size(col, w, h);
    lv_obj_set_style_bg_opa(col, LV_OPA_TRANSP, 0);
    lv_obj_set_style_pad_all(col, 0, 0);
    lv_obj_set_style_pad_row(col, 8, 0); /* the shared default; a screen overrides for 4 or 6 */
    lv_obj_set_flex_flow(col, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(col, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_scrollable(col, true);
    lv_obj_set_scroll_dir(col, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(col, LV_SCROLLBAR_MODE_OFF);
    return col;
}

/* -------------------------------------------------------------- list row */

/* A transparent, pad-free flex column: the row's text block. */
static lv_obj_t *mk_text_block(lv_obj_t *row)
{
    lv_obj_t *text = lv_obj_create(row);

    lv_obj_remove_style_all(text);
    /* width 1 + grow 1: the flex row gives it what the icon and the trailing
     * slot leave over. */
    lv_obj_set_width(text, 1);
    lv_obj_set_height(text, LV_SIZE_CONTENT);
    lv_obj_set_flex_grow(text, 1);
    lv_obj_set_style_bg_opa(text, LV_OPA_TRANSP, 0);
    lv_obj_set_style_pad_all(text, 0, 0);
    lv_obj_set_flex_flow(text, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(text, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_scrollable(text, false);
    return text;
}

/* A transparent, content-sized container: the row's trailing slot. It is
 * created empty and always last, so ui_list_row_trailing() can find it. */
static lv_obj_t *mk_trailing_slot(lv_obj_t *row)
{
    lv_obj_t *slot = lv_obj_create(row);

    lv_obj_remove_style_all(slot);
    lv_obj_set_size(slot, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(slot, LV_OPA_TRANSP, 0);
    lv_obj_set_style_pad_all(slot, 0, 0);
    lv_obj_set_scrollable(slot, false);
    return slot;
}

lv_obj_t *ui_list_row(lv_obj_t *parent, const char *icon, const char *title, const char *subtitle,
                      const char *trailing, lv_event_cb_t cb, void *user)
{
    lv_obj_t *row = lv_obj_create(parent);
    lv_obj_t *text, *slot;

    lv_obj_remove_style_all(row);
    lv_obj_set_width(row, LV_PCT(100));
    lv_obj_set_height(row, LV_SIZE_CONTENT);
    lv_obj_set_style_min_height(row, 44, 0);
    lv_obj_set_style_bg_color(row, UI_COL_PANEL, 0);
    lv_obj_set_style_bg_opa(row, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(row, UI_RADIUS, 0);
    lv_obj_set_style_pad_left(row, 12, 0);
    lv_obj_set_style_pad_right(row, 12, 0);
    lv_obj_set_style_pad_top(row, 10, 0);
    lv_obj_set_style_pad_bottom(row, 10, 0);
    lv_obj_set_style_pad_column(row, 10, 0);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_scrollable(row, false);
    lv_obj_set_clickable(row, cb != NULL);
    if(cb) {
        lv_obj_set_style_bg_color(row, UI_COL_PANEL_HI, LV_STATE_PRESSED);
        lv_obj_add_event_cb(row, cb, LV_EVENT_CLICKED, user);
    }

    if(icon) {
        lv_obj_t *ic = mk_label(row, icon, UI_FONT_ICON, UI_COL_TEXT_DIM, LV_LABEL_LONG_MODE_CLIP);
        lv_obj_set_width(ic, 18);
    }

    text = mk_text_block(row);
    {
        lv_obj_t *t = mk_label(text, title, UI_FONT_M, UI_COL_TEXT, LV_LABEL_LONG_MODE_DOTS);
        lv_obj_set_width(t, LV_PCT(100));
    }
    if(subtitle) {
        lv_obj_t *s = mk_label(text, subtitle, UI_FONT_S, UI_COL_TEXT_DIM,
                               LV_LABEL_LONG_MODE_WRAP);
        lv_obj_set_width(s, LV_PCT(100));
    }

    slot = mk_trailing_slot(row);
    if(trailing) {
        mk_label(slot, trailing, UI_FONT_S, UI_COL_TEXT_DIM, LV_LABEL_LONG_MODE_CLIP);
    }
    else if(cb) {
        mk_label(slot, UI_ICON_CHEVRON_RIGHT, UI_FONT_ICON, UI_COL_TEXT_FAINT,
                 LV_LABEL_LONG_MODE_CLIP);
    }

    return row;
}

lv_obj_t *ui_list_row_trailing(lv_obj_t *row)
{
    uint32_t n;

    if(!row) return NULL;
    n = lv_obj_get_child_count(row);
    if(n == 0) return NULL;
    return lv_obj_get_child(row, (int32_t)(n - 1));
}

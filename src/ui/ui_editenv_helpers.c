/**
 * @file ui_editenv_helpers.c
 * See ui_editenv_helpers.h.
 */

#include "ui/ui_editenv_helpers.h"

#include <stdio.h>

lv_obj_t *ui_editenv_panel(lv_obj_t *parent, ui_style_id_t style,
                           int x, int y, int w, int h)
{
    lv_obj_t *o = lv_obj_create(parent);

    lv_obj_remove_style_all(o);
    lv_obj_add_style(o, ui_style(style), 0);
    lv_obj_set_pos(o, x, y);
    lv_obj_set_size(o, w, h);
    lv_obj_set_scrollable(o, false);
    return o;
}

lv_obj_t *ui_editenv_label(lv_obj_t *parent, const char *text,
                           ui_style_id_t style, int x, int y)
{
    lv_obj_t *l = lv_label_create(parent);

    lv_obj_remove_style_all(l);
    lv_obj_add_style(l, ui_style(style), 0);
    lv_label_set_long_mode(l, LV_LABEL_LONG_MODE_CLIP);
    lv_label_set_text(l, text);
    lv_obj_align(l, LV_ALIGN_TOP_LEFT, x, y);
    return l;
}

lv_obj_t *ui_editenv_label_mid(lv_obj_t *parent, const char *text,
                               ui_style_id_t style, int x, int dy)
{
    lv_obj_t *l = lv_label_create(parent);

    lv_obj_remove_style_all(l);
    lv_obj_add_style(l, ui_style(style), 0);
    lv_label_set_long_mode(l, LV_LABEL_LONG_MODE_CLIP);
    lv_label_set_text(l, text);
    lv_obj_align(l, LV_ALIGN_LEFT_MID, x, dy);
    return l;
}

lv_obj_t *ui_editenv_button(lv_obj_t *parent, const char *text, ui_style_id_t style,
                            int x, int y, int w, int h,
                            lv_event_cb_t cb, void *user)
{
    lv_obj_t *b = lv_button_create(parent);
    lv_obj_t *l;

    lv_obj_remove_style_all(b);
    lv_obj_add_style(b, ui_style(style), 0);
    lv_obj_set_pos(b, x, y);
    lv_obj_set_size(b, w, h);
    lv_obj_set_scrollable(b, false);

    l = lv_label_create(b); /* inherits the button's font and text color */
    lv_obj_remove_style_all(l);
    lv_label_set_text(l, text);
    lv_label_set_long_mode(l, LV_LABEL_LONG_MODE_CLIP);
    lv_obj_center(l);

    if(cb) lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, user);
    return b;
}

lv_obj_t *ui_editenv_stepper(lv_obj_t *parent, int x, int y, int w, int h,
                             bool enabled, lv_event_cb_t cb,
                             void *minus_user, void *plus_user)
{
    int bw = UI_TOUCH_MIN;
    lv_obj_t *box;
    lv_obj_t *val;

    /* Keep the value readable when the caller gives us a narrow column. */
    if(bw * 2 + 24 > w) bw = (w - 24) / 2;
    if(bw < 18) bw = 18;

    ui_editenv_button(parent, LV_SYMBOL_MINUS,
                      enabled ? UI_STYLE_BTN : UI_STYLE_BTN_DISABLED,
                      x, y, bw, h, enabled ? cb : NULL, minus_user);

    box = ui_editenv_panel(parent, UI_STYLE_PANEL, x + bw + 2, y, w - 2 * bw - 4, h);
    val = lv_label_create(box);
    lv_obj_remove_style_all(val);
    lv_obj_add_style(val, ui_style(UI_STYLE_LABEL), 0);
    lv_obj_set_style_text_font(val, UI_FONT_L, 0);
    if(!enabled) lv_obj_set_style_text_color(val, UI_COL_TEXT_DIM, 0);
    lv_label_set_long_mode(val, LV_LABEL_LONG_MODE_CLIP);
    lv_label_set_text(val, "-");
    lv_obj_center(val);

    ui_editenv_button(parent, LV_SYMBOL_PLUS,
                      enabled ? UI_STYLE_BTN : UI_STYLE_BTN_DISABLED,
                      x + w - bw, y, bw, h, enabled ? cb : NULL, plus_user);

    return val;
}

void ui_editenv_color_text(uint32_t rgb, char *out, size_t out_len)
{
    if(out_len == 0) return;
    snprintf(out, out_len, "#%06X", (unsigned)(rgb & 0xFFFFFFu));
}

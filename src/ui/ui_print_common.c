/**
 * @file ui_print_common.c
 */

#include "ui/ui_print_common.h"
#include "view/strings.h"
#include "view/view.h"

#include "model/mmu_model.h"
#include "ui/ui_widgets.h"

#include <stdio.h>
#include <string.h>

/* ------------------------------------------------------------ color names */

/* The palette and its names are the view's (view_palette_*): the spool editor
 * of both renderers offers the same colors, under the same names. */

const char *print_color_name(uint32_t rgb)
{
    return view_color_name(rgb);
}

int print_palette_count(void)
{
    return view_palette_count();
}

uint32_t print_palette_color(int i)
{
    return view_palette_color(i);
}

const char *print_side_text(int unit_idx)
{
    if(!g_mmu.dual_extruder) return "";
    if(unit_idx < 0 || unit_idx >= (int)g_mmu.unit_count) return "";
    switch(g_mmu.units[unit_idx].side) {
        case MMU_SIDE_LEFT:  return view_str(VS_SIDE_LEFT);
        case MMU_SIDE_RIGHT: return view_str(VS_SIDE_RIGHT);
        default:             return "";
    }
}

/* ---------------------------------------------------------------- building */

lv_obj_t *print_label(lv_obj_t *parent, const char *text, lv_style_t *style,
                      int x, int y, int w)
{
    lv_obj_t *l = lv_label_create(parent);

    lv_obj_remove_style_all(l);
    lv_label_set_text(l, text ? text : "");
    lv_label_set_long_mode(l, w > 0 ? LV_LABEL_LONG_MODE_WRAP : LV_LABEL_LONG_MODE_CLIP);
    if(w > 0) lv_obj_set_width(l, w);
    if(style) lv_obj_add_style(l, style, 0);
    lv_obj_set_pos(l, x, y);
    return l;
}

lv_obj_t *print_panel(lv_obj_t *parent, ui_style_id_t style,
                      int x, int y, int w, int h)
{
    lv_obj_t *o = lv_obj_create(parent);

    lv_obj_remove_style_all(o);
    lv_obj_add_style(o, ui_style(style), 0);
    lv_obj_set_pos(o, x, y);
    lv_obj_set_size(o, w, h);
    /* Decorative by default: a clickable child with no callback would swallow
     * the tap instead of letting it reach the interactive ancestor, because
     * LVGL only bubbles CHILD_* events. Callers that want a tap target add
     * LV_OBJ_FLAG_CLICKABLE back. */
    lv_obj_set_scrollable(o, false);
    lv_obj_set_clickable(o, false);
    return o;
}

lv_obj_t *print_button(lv_obj_t *parent, const char *text, ui_style_id_t style,
                       int x, int y, int w, int h,
                       lv_event_cb_t cb, void *user)
{
    lv_obj_t *b = lv_button_create(parent);

    lv_obj_remove_style_all(b);
    lv_obj_add_style(b, ui_style(style), 0);
    lv_obj_set_pos(b, x, y);
    lv_obj_set_size(b, w, h);
    lv_obj_set_scrollable(b, false);

    if(text) {
        lv_obj_t *l = lv_label_create(b);
        lv_obj_remove_style_all(l);
        lv_label_set_text(l, text);
        lv_label_set_long_mode(l, LV_LABEL_LONG_MODE_CLIP);
        lv_obj_center(l);
    }
    if(cb) lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, user);
    return b;
}

lv_obj_t *print_swatch(lv_obj_t *parent, uint32_t rgb, bool unknown,
                       int x, int y, int w, int h)
{
    lv_obj_t *sw = lv_obj_create(parent);

    lv_obj_remove_style_all(sw);
    lv_obj_set_style_bg_color(sw, unknown ? UI_COL_PANEL_HI : lv_color_hex(rgb), 0);
    lv_obj_set_style_bg_opa(sw, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(sw, UI_PX(4), 0);
    lv_obj_set_style_border_width(sw, UI_PX(1), 0);
    lv_obj_set_style_border_color(sw, UI_COL_LINE, 0);
    lv_obj_set_pos(sw, x, y);
    lv_obj_set_size(sw, w, h);
    lv_obj_set_scrollable(sw, false);
    lv_obj_set_clickable(sw, false);

    if(unknown) {
        lv_obj_t *q = lv_label_create(sw);
        lv_obj_remove_style_all(q);
        lv_label_set_text(q, "?");
        lv_obj_add_style(q, ui_style(UI_STYLE_TITLE), 0);
        lv_obj_center(q);
    }
    return sw;
}

lv_obj_t *print_ring(lv_obj_t *parent, uint32_t rgb, const char *center,
                     int x, int y, int size)
{
    lv_obj_t *ring = lv_obj_create(parent);

    lv_obj_remove_style_all(ring);
    lv_obj_set_style_bg_color(ring, UI_COL_PANEL, 0);
    lv_obj_set_style_bg_opa(ring, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(ring, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(ring, UI_PX(5), 0);
    lv_obj_set_style_border_color(ring, lv_color_hex(rgb), 0);
    lv_obj_set_style_border_opa(ring, LV_OPA_COVER, 0);
    lv_obj_set_pos(ring, x, y);
    lv_obj_set_size(ring, size, size);
    lv_obj_set_scrollable(ring, false);
    lv_obj_set_clickable(ring, false);

    if(center) {
        lv_obj_t *l = lv_label_create(ring);
        lv_obj_remove_style_all(l);
        lv_label_set_text(l, center);
        lv_obj_add_style(l, ui_style(UI_STYLE_TITLE), 0);
        lv_obj_center(l);
    }
    return ring;
}

lv_obj_t *print_slot_chip(lv_obj_t *parent, int unit, int slot,
                          int x, int y, int w, int h,
                          lv_event_cb_t cb, void *user)
{
    mmu_slot_t *s;
    lv_obj_t *chip;
    char id[MMU_SLOT_ID_LEN];
    char buf[24];

    if(unit < 0 || unit >= (int)g_mmu.unit_count) return NULL;
    if(slot < 0 || slot >= (int)g_mmu.units[unit].slot_count) return NULL;
    s = &g_mmu.units[unit].slots[slot];

    chip = print_panel(parent, UI_STYLE_PANEL_HI, x, y, w, h);
    if(cb) {
        lv_obj_set_clickable(chip, true);
        lv_obj_add_event_cb(chip, cb, LV_EVENT_CLICKED,
                            user ? user : UI_ENC(unit, slot));
    }

    /* color stripe down the left edge */
    {
        lv_obj_t *stripe = lv_obj_create(chip);
        lv_obj_remove_style_all(stripe);
        lv_obj_set_style_bg_color(stripe, s->color_known ? lv_color_hex(s->color)
                                                         : UI_COL_TEXT_FAINT, 0);
        lv_obj_set_style_bg_opa(stripe, LV_OPA_COVER, 0);
        lv_obj_set_style_radius(stripe, UI_PX(2), 0);
        lv_obj_set_pos(stripe, UI_PX(2), UI_PX(2));
        lv_obj_set_size(stripe, UI_PX(6), h - UI_PX(4));
        lv_obj_set_scrollable(stripe, false);
        lv_obj_set_clickable(stripe, false);
    }

    mmu_slot_id(unit, slot, id, sizeof id);
    {
        lv_obj_t *l = print_label(chip, id, ui_style(UI_STYLE_LABEL), UI_PX(12), UI_PX(1), 0);
        lv_obj_set_style_text_font(l, UI_FONT_S, 0);
    }

    if(s->remaining_pct >= 0) snprintf(buf, sizeof buf, "%d%%", (int)s->remaining_pct);
    else                      snprintf(buf, sizeof buf, "--");
    print_label(chip, buf, ui_style(UI_STYLE_LABEL_DIM), UI_PX(12), h - UI_PX(15), 0);

    return chip;
}

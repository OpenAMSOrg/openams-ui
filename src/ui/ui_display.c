/**
 * @file ui_display.c
 * The display pieces: hero readout, progress ring, status pill, filament
 * swatch, list-section caption and empty state.
 *
 * Plain objects start with lv_obj_remove_style_all() so the dark default theme
 * does not repaint them as panels; the pieces set every style they need by
 * hand. Labels are left without text properties so they inherit them from the
 * container that carries the intent (the pill, the empty state, the screen).
 */

#include "ui/ui_display.h"

#include <string.h>

/* ----------------------------------------------------------------- helpers */

/* A transparent, non-interactive container. Width/height may be
 * LV_SIZE_CONTENT. */
static lv_obj_t *mk_box(lv_obj_t *parent, int x, int y, int w, int h)
{
    lv_obj_t *o = lv_obj_create(parent);

    lv_obj_remove_style_all(o);
    lv_obj_set_pos(o, x, y);
    lv_obj_set_size(o, w, h);
    lv_obj_set_scrollable(o, false);
    lv_obj_set_clickable(o, false);
    lv_obj_set_style_bg_opa(o, LV_OPA_TRANSP, 0);
    return o;
}

/* A label that draws left-aligned and never scrolls; long modes are CLIP
 * unless the caller overrides them. */
static lv_obj_t *mk_label(lv_obj_t *parent, const char *text, const lv_font_t *font,
                          lv_color_t color)
{
    lv_obj_t *l = lv_label_create(parent);

    lv_obj_remove_style_all(l);
    lv_label_set_long_mode(l, LV_LABEL_LONG_MODE_CLIP);
    lv_label_set_text(l, text ? text : "");
    lv_obj_set_style_text_font(l, font, 0);
    lv_obj_set_style_text_color(l, color, 0);
    lv_obj_set_scrollable(l, false);
    return l;
}

/* ------------------------------------------------------------- hero readout */

/* children: 0 = number, 1 = unit */
static void hero_apply(lv_obj_t *box, const char *number, const char *unit)
{
    lv_obj_t *num;
    lv_obj_t *un;

    if(lv_obj_get_child_count(box) < 2) return;

    num = lv_obj_get_child(box, 0);
    un = lv_obj_get_child(box, 1);

    lv_label_set_text(num, number ? number : "");
    lv_label_set_text(un, unit ? unit : "");

    /* 1 px gap to the right of the number, unit bottom aligned with it then
     * lifted 2 px so the two baselines sit the way the spec draws them. */
    lv_obj_align_to(un, num, LV_ALIGN_OUT_RIGHT_BOTTOM, UI_PX(1), UI_PX(-2));
}

lv_obj_t *ui_hero_value(lv_obj_t *parent, int x, int y, const char *number, const char *unit)
{
    lv_obj_t *box = mk_box(parent, x, y, LV_SIZE_CONTENT, LV_SIZE_CONTENT);

    mk_label(box, "", UI_FONT_HERO, UI_COL_TEXT);
    mk_label(box, "", UI_FONT_M, UI_COL_TEXT_DIM);
    hero_apply(box, number, unit);
    return box;
}

void ui_hero_value_set(lv_obj_t *hero, const char *number, const char *unit)
{
    if(hero) hero_apply(hero, number, unit);
}

/* ---------------------------------------------------------------- the ring */

/* An empty ring is a bare track; a hair under 8% still shows a sliver so the
 * ring reads as "started" rather than as an empty track. */
static int ring_sweep(int pct)
{
    int sweep;

    if(pct <= 0) return 0;

    sweep = pct * 360 / 100;
    return sweep < 8 ? 8 : sweep;
}

lv_obj_t *ui_ring(lv_obj_t *parent, int x, int y, int size, int pct, lv_color_t color)
{
    lv_obj_t *arc = lv_arc_create(parent);

    lv_obj_set_pos(arc, x, y);
    lv_obj_set_size(arc, size, size);
    lv_obj_set_scrollable(arc, false);
    lv_obj_set_clickable(arc, false);
    lv_obj_set_style_bg_opa(arc, LV_OPA_TRANSP, 0);

    lv_arc_set_rotation(arc, 270); /* 0 degrees at 12 o'clock */
    lv_arc_set_bg_angles(arc, 0, 360);

    lv_obj_set_style_arc_width(arc, UI_PX(3), 0);
    lv_obj_set_style_arc_color(arc, UI_COL_LINE, 0);
    lv_obj_set_style_arc_width(arc, UI_PX(3), LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(arc, color, LV_PART_INDICATOR);
    lv_obj_set_style_arc_rounded(arc, true, LV_PART_INDICATOR);

    /* The knob is a touch target we never want: make it invisible. */
    lv_obj_set_style_bg_opa(arc, LV_OPA_TRANSP, LV_PART_KNOB);
    lv_obj_set_style_pad_all(arc, 0, LV_PART_KNOB);

    ui_ring_set(arc, pct);
    return arc;
}

void ui_ring_set(lv_obj_t *ring, int pct)
{
    if(ring) lv_arc_set_angles(ring, 0, ring_sweep(pct));
}

/* ---------------------------------------------------------------- the pill */

lv_obj_t *ui_pill(lv_obj_t *parent, const char *text, ui_style_id_t pill_style)
{
    lv_obj_t *pill = lv_obj_create(parent);
    lv_obj_t *l;

    lv_obj_remove_style_all(pill);
    lv_obj_add_style(pill, ui_style(pill_style), 0);

    /* The capsule geometry is fixed: 20 px tall, 0/8 padding, the medium cut
     * of the small size. The style carries only the fill and the text color. */
    lv_obj_set_style_text_font(pill, UI_FONT_S_MED, 0);
    lv_obj_set_style_pad_hor(pill, UI_PX(8), 0);
    lv_obj_set_style_pad_ver(pill, 0, 0);
    lv_obj_set_height(pill, UI_PX(20));
    lv_obj_set_width(pill, LV_SIZE_CONTENT);
    lv_obj_set_scrollable(pill, false);
    lv_obj_set_clickable(pill, false);

    lv_obj_set_flex_flow(pill, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(pill, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    l = lv_label_create(pill);
    lv_obj_remove_style_all(l); /* inherit the pill's font and color */
    lv_label_set_long_mode(l, LV_LABEL_LONG_MODE_CLIP);
    lv_label_set_text(l, text ? text : "");
    lv_obj_set_width(l, LV_SIZE_CONTENT);
    lv_obj_set_height(l, LV_SIZE_CONTENT);
    return pill;
}

/* -------------------------------------------------------------- the swatch */

lv_obj_t *ui_swatch(lv_obj_t *parent, uint32_t rgb, int x, int y, int size, bool round)
{
    lv_obj_t *sw = lv_obj_create(parent);
    lv_color_t top;
    lv_color_t bottom;
    bool dark;

    ui_filament_gradient(rgb, &top, &bottom);
    /* A dark fill - charcoal-neutral or dark-saturated alike - is the one
     * that needs an outline to hold its edge against the panel. */
    dark = ui_filament_needs_outline(rgb);

    lv_obj_remove_style_all(sw);
    lv_obj_set_pos(sw, x, y);
    lv_obj_set_size(sw, size, size);
    lv_obj_set_scrollable(sw, false);
    lv_obj_set_clickable(sw, false);
    lv_obj_set_style_radius(sw, round ? size / 2 : UI_PX(2), 0);
    lv_obj_set_style_bg_opa(sw, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(sw, top, 0);
    lv_obj_set_style_bg_grad_color(sw, bottom, 0);
    lv_obj_set_style_bg_grad_dir(sw, LV_GRAD_DIR_VER, 0);

    if(dark) {
        lv_obj_set_style_border_width(sw, UI_PX(1), 0);
        lv_obj_set_style_border_color(sw, UI_COL_TEXT_FAINT, 0);
        lv_obj_set_style_border_opa(sw, LV_OPA_COVER, 0);
    }
    return sw;
}

/* ------------------------------------------------------- section caption */

lv_obj_t *ui_section_label(lv_obj_t *parent, const char *text)
{
    lv_obj_t *l = mk_label(parent, text, UI_FONT_S, UI_COL_TEXT_FAINT);

    lv_obj_set_width(l, LV_PCT(100));
    lv_obj_set_style_pad_left(l, UI_PX(4), 0);
    lv_obj_set_style_pad_top(l, UI_PX(6), 0);
    return l;
}

/* ---------------------------------------------------------- empty state */

lv_obj_t *ui_empty_state(lv_obj_t *parent, const char *icon, const char *text)
{
    lv_obj_t *col = mk_box(parent, 0, 0, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_t *ic;
    lv_obj_t *tx;

    lv_obj_set_flex_flow(col, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(col, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(col, UI_PX(8), 0);

    ic = mk_label(col, icon, UI_FONT_ICON_L, UI_COL_TEXT_FAINT);
    lv_obj_set_width(ic, LV_SIZE_CONTENT);
    lv_obj_set_height(ic, LV_SIZE_CONTENT);

    /* The copy wraps inside the parent's width minus a 16 px margin a side.
     * A freshly created parent has no size yet, so ask for the layout to run
     * first and read the width it will really have. */
    lv_obj_update_layout(parent);
    tx = mk_label(col, text, UI_FONT_S, UI_COL_TEXT_DIM);
    lv_label_set_long_mode(tx, LV_LABEL_LONG_MODE_WRAP);
    lv_obj_set_style_text_align(tx, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_width(tx, lv_obj_get_width(parent) - UI_PX(32));
    lv_obj_set_height(tx, LV_SIZE_CONTENT);

    lv_obj_center(col);
    return col;
}

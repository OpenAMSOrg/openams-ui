/**
 * @file ui_input.c
 * See ui_input.h. Everything is placed with explicit coordinates and is
 * static storage only: no allocation, no theme styles (each widget owns its
 * own fills so it renders the same on any parent).
 */

#include "ui/ui_input.h"

#include "ui/ui_icons.h"
#include "ui/ui_theme.h"

/* --------------------------------------------------------------- geometry */

#define UI_INPUT_BOX_H      UI_PX(36) /* stepper / chip height; also the touch minimum */
#define UI_INPUT_SEG_W      UI_PX(36) /* stepper minus / plus segment width */
#define UI_INPUT_RULE_Y     UI_PX(8)
#define UI_INPUT_RULE_H     UI_PX(20)
#define UI_INPUT_CHIP_MIN   UI_PX(56)
#define UI_INPUT_CHIP_PAD   UI_PX(24) /* 12 px each side of the chip label */
#define UI_INPUT_TOG_W      UI_PX(40)
#define UI_INPUT_TOG_H      UI_PX(24)
#define UI_INPUT_KNOB       UI_PX(18)
#define UI_INPUT_KNOB_Y     UI_PX(3)
#define UI_INPUT_KNOB_OFF_X UI_PX(3)
/* 19: the off inset mirrored, so the knob sits as far from either end */
#define UI_INPUT_KNOB_ON_X  (UI_INPUT_TOG_W - UI_INPUT_KNOB - UI_INPUT_KNOB_OFF_X)
#define UI_INPUT_ANIM_MS   120

/* ------------------------------------------------------------------ helpers */

static lv_obj_t *mk_plain(lv_obj_t *parent, int x, int y, int w, int h)
{
    lv_obj_t *o = lv_obj_create(parent);

    lv_obj_remove_style_all(o);
    lv_obj_set_pos(o, x, y);
    lv_obj_set_size(o, w, h);
    lv_obj_set_scrollable(o, false);
    return o;
}

static lv_obj_t *mk_label(lv_obj_t *parent, const char *text, const lv_font_t *font,
                          lv_color_t color)
{
    lv_obj_t *l = lv_label_create(parent);

    lv_obj_remove_style_all(l);
    lv_label_set_text(l, text);
    lv_label_set_long_mode(l, LV_LABEL_LONG_MODE_CLIP);
    lv_obj_set_style_text_font(l, font, 0);
    lv_obj_set_style_text_color(l, color, 0);
    lv_obj_set_scrollable(l, false);
    return l;
}

/* ---------------------------------------------------------------- stepper */

/* One 36 px slice of a stepper: transparent, fills UI_COL_LINE while pressed,
 * and clips under the container's rounded corners. */
static lv_obj_t *mk_segment(lv_obj_t *box, int x, const char *icon,
                            lv_event_cb_t cb, void *user)
{
    lv_obj_t *b = lv_button_create(box);
    lv_obj_t *l;

    lv_obj_remove_style_all(b);
    lv_obj_set_pos(b, x, 0);
    lv_obj_set_size(b, UI_INPUT_SEG_W, UI_INPUT_BOX_H);
    lv_obj_set_scrollable(b, false);
    lv_obj_set_style_radius(b, 0, 0);
    lv_obj_set_style_bg_opa(b, LV_OPA_TRANSP, 0);
    lv_obj_set_style_bg_opa(b, LV_OPA_COVER, LV_STATE_PRESSED);
    lv_obj_set_style_bg_color(b, UI_COL_LINE, LV_STATE_PRESSED);

    l = mk_label(b, icon, UI_FONT_ICON, UI_COL_TEXT);
    lv_obj_center(l);

    if(cb) lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, user);
    return b;
}

lv_obj_t *ui_stepper(lv_obj_t *parent, int x, int y, int w, const char *value,
                     lv_event_cb_t on_minus, lv_event_cb_t on_plus, void *user)
{
    lv_obj_t *box = mk_plain(parent, x, y, w, UI_INPUT_BOX_H);
    lv_obj_t *val;
    lv_obj_t *rule;

    lv_obj_set_style_bg_color(box, UI_COL_PANEL_HI, 0);
    lv_obj_set_style_bg_opa(box, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(box, UI_RADIUS, 0);
    lv_obj_set_style_clip_corner(box, true, 0);
    lv_obj_set_clickable(box, false);

    mk_segment(box, 0, UI_ICON_MINUS, on_minus, user);
    mk_segment(box, w - UI_INPUT_SEG_W, UI_ICON_PLUS, on_plus, user);

    val = mk_label(box, value, UI_FONT_L, UI_COL_TEXT);
    lv_obj_set_width(val, w - 2 * UI_INPUT_SEG_W);
    lv_obj_set_style_text_align(val, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(val, LV_ALIGN_CENTER, 0, 0);

    rule = mk_plain(box, UI_INPUT_SEG_W, UI_INPUT_RULE_Y, UI_PX(1), UI_INPUT_RULE_H);
    lv_obj_set_style_bg_color(rule, UI_COL_LINE, 0);
    lv_obj_set_style_bg_opa(rule, LV_OPA_COVER, 0);

    rule = mk_plain(box, w - UI_INPUT_SEG_W - UI_PX(1), UI_INPUT_RULE_Y, UI_PX(1), UI_INPUT_RULE_H);
    lv_obj_set_style_bg_color(rule, UI_COL_LINE, 0);
    lv_obj_set_style_bg_opa(rule, LV_OPA_COVER, 0);

    return val;
}

lv_obj_t *ui_stepper_captioned(lv_obj_t *parent, int x, int y, int w, const char *caption,
                               const char *value, lv_event_cb_t on_minus, lv_event_cb_t on_plus,
                               void *user)
{
    lv_obj_t *cap = mk_label(parent, caption, UI_FONT_S, UI_COL_TEXT_DIM);

    lv_obj_set_pos(cap, x, y);
    return ui_stepper(parent, x, y + UI_PX(16), w, value, on_minus, on_plus, user);
}

/* ------------------------------------------------------------------- chip */

static void chip_apply(lv_obj_t *chip, bool selected)
{
    lv_obj_t *l = lv_obj_get_child(chip, 0);

    lv_obj_set_style_bg_color(chip, selected ? UI_COL_PANEL_HI : UI_COL_PANEL, 0);
    lv_obj_set_style_border_color(chip, selected ? UI_COL_ACCENT : UI_COL_PANEL, 0);
    if(l) lv_obj_set_style_text_color(l, selected ? UI_COL_ACCENT : UI_COL_TEXT, 0);
    if(selected) lv_obj_add_state(chip, LV_STATE_CHECKED);
    else         lv_obj_remove_state(chip, LV_STATE_CHECKED);
}

/* @p w < 0 means content-driven: max(UI_INPUT_CHIP_MIN, label width + pad).
 * @p w >= 0 is an exact width the caller has already decided on. */
static lv_obj_t *chip_build(lv_obj_t *parent, const char *text, int w, bool selected,
                            lv_event_cb_t cb, void *user)
{
    lv_obj_t *chip = lv_obj_create(parent);
    lv_obj_t *l;
    int32_t chip_w = w;

    lv_obj_remove_style_all(chip);
    lv_obj_set_scrollable(chip, false);

    /* The width comes from the label: measure it before sizing the chip. */
    l = lv_label_create(chip);
    lv_obj_remove_style_all(l);
    lv_label_set_text(l, text);
    lv_label_set_long_mode(l, LV_LABEL_LONG_MODE_CLIP);
    lv_obj_set_style_text_font(l, UI_FONT_M, 0);
    lv_obj_set_scrollable(l, false);

    if(chip_w < 0) {
        int32_t label_w;

        lv_obj_update_layout(l);
        label_w = lv_obj_get_width(l);
        chip_w = label_w + UI_INPUT_CHIP_PAD;
        if(chip_w < UI_INPUT_CHIP_MIN) chip_w = UI_INPUT_CHIP_MIN;
    }

    lv_obj_set_size(chip, chip_w, UI_INPUT_BOX_H);
    lv_obj_set_scrollable(chip, false);
    lv_obj_set_clickable(chip, true);
    lv_obj_set_style_radius(chip, UI_RADIUS, 0);
    lv_obj_set_style_border_width(chip, UI_PX(1), 0);
    lv_obj_set_style_bg_opa(chip, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(chip, UI_COL_PANEL_HI, LV_STATE_PRESSED);
    lv_obj_center(l);

    /* Settle the layout so a caller can read the resolved width right away. */
    lv_obj_update_layout(chip);

    chip_apply(chip, selected);
    if(cb) lv_obj_add_event_cb(chip, cb, LV_EVENT_CLICKED, user);
    return chip;
}

lv_obj_t *ui_chip(lv_obj_t *parent, const char *text, bool selected, lv_event_cb_t cb,
                  void *user)
{
    return chip_build(parent, text, -1, selected, cb, user);
}

lv_obj_t *ui_chip_fixed(lv_obj_t *parent, const char *text, int w, bool selected,
                        lv_event_cb_t cb, void *user)
{
    return chip_build(parent, text, w, selected, cb, user);
}

void ui_chip_set_selected(lv_obj_t *chip, bool selected)
{
    if(!chip) return;
    chip_apply(chip, selected);
}

/* ----------------------------------------------------------------- toggle */

static void toggle_apply(lv_obj_t *t, bool on, bool animate)
{
    lv_obj_t *knob = lv_obj_get_child(t, 0);

    lv_obj_set_style_bg_color(t, on ? UI_COL_ACCENT : UI_COL_LINE, 0);
    if(on) lv_obj_add_state(t, LV_STATE_CHECKED);
    else   lv_obj_remove_state(t, LV_STATE_CHECKED);

    if(!knob) return;
    if(animate) {
        lv_anim_t a;

        lv_anim_delete(knob, (lv_anim_exec_xcb_t)lv_obj_set_x);
        lv_anim_init(&a);
        lv_anim_set_var(&a, knob);
        lv_anim_set_exec_cb(&a, (lv_anim_exec_xcb_t)lv_obj_set_x);
        lv_anim_set_values(&a, lv_obj_get_x(knob), on ? UI_INPUT_KNOB_ON_X : UI_INPUT_KNOB_OFF_X);
        lv_anim_set_duration(&a, UI_INPUT_ANIM_MS);
        lv_anim_set_path_cb(&a, lv_anim_path_ease_out);
        lv_anim_start(&a);
    }
    else {
        lv_obj_set_x(knob, on ? UI_INPUT_KNOB_ON_X : UI_INPUT_KNOB_OFF_X);
    }
}

/* Registered first so the state is already flipped when a caller's handler
 * runs (LVGL dispatches event callbacks in registration order). */
static void on_toggle_click(lv_event_t *e)
{
    lv_obj_t *t = lv_event_get_target_obj(e);

    toggle_apply(t, !lv_obj_has_state(t, LV_STATE_CHECKED), true);
}

lv_obj_t *ui_toggle(lv_obj_t *parent, bool on, lv_event_cb_t cb, void *user)
{
    lv_obj_t *t = lv_button_create(parent);
    lv_obj_t *knob;

    lv_obj_remove_style_all(t);
    lv_obj_set_size(t, UI_INPUT_TOG_W, UI_INPUT_TOG_H);
    lv_obj_set_scrollable(t, false);
    lv_obj_set_clickable(t, true);
    lv_obj_set_style_radius(t, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_opa(t, LV_OPA_COVER, 0);

    knob = mk_plain(t, on ? UI_INPUT_KNOB_ON_X : UI_INPUT_KNOB_OFF_X, UI_INPUT_KNOB_Y,
                    UI_INPUT_KNOB, UI_INPUT_KNOB);
    lv_obj_set_style_radius(knob, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(knob, UI_COL_TEXT, 0);
    lv_obj_set_style_bg_opa(knob, LV_OPA_COVER, 0);
    lv_obj_set_clickable(knob, false);

    toggle_apply(t, on, false);
    lv_obj_add_event_cb(t, on_toggle_click, LV_EVENT_CLICKED, NULL);
    if(cb) lv_obj_add_event_cb(t, cb, LV_EVENT_CLICKED, user);
    return t;
}

void ui_toggle_set(lv_obj_t *toggle, bool on)
{
    if(!toggle) return;
    toggle_apply(toggle, on, false);
}

bool ui_toggle_get(lv_obj_t *toggle)
{
    return toggle && lv_obj_has_state(toggle, LV_STATE_CHECKED);
}

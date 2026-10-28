/**
 * @file ui_band.c
 * The bottom action band: one tap target across the full width, 36 px tall, so
 * the action never competes with the content above it. A screen that offers two
 * ways out splits it in halves with a 1 px hairline on the seam.
 */

#include "ui/ui_band.h"

#include "ui/ui_theme.h"

#include <stdint.h>

#define BAND_RULE_H UI_PX(20)
#define BAND_TRANSITION_MS 150

/* A band button carries its kind in its own user data, offset by one so that
 * everything else (labels, the hairline) reads as "no kind". */
#define KIND_CODE(k) ((void *)(intptr_t)((k) + 1))
#define KIND_OF(o)   ((ui_action_kind_t)((int)(intptr_t)lv_obj_get_user_data(o) - 1))

/* Enabling/disabling a band recolors its fill and its label; both animate,
 * so a screen that gains something to do "lights up" rather than snapping. */
static const lv_style_prop_t s_band_tr_props[] = { LV_STYLE_BG_COLOR, LV_STYLE_TEXT_COLOR, 0 };
static lv_style_transition_dsc_t s_band_transition;
static bool s_band_transition_ready;

static void band_ensure_transition(void)
{
    if(s_band_transition_ready) return;
    s_band_transition_ready = true;
    lv_style_transition_dsc_init(&s_band_transition, s_band_tr_props, lv_anim_path_linear,
                                 BAND_TRANSITION_MS, 0, NULL);
}

/* A disabled button reads as a dimmed version of its own kind, so it is
 * recognisable as a button that exists but is off, not an empty dark bar. */
static void disabled_colors(ui_action_kind_t kind, lv_color_t *fill, lv_color_t *text)
{
    switch(kind) {
    case UI_ACTION_ACCENT:
        *fill = lv_color_mix(UI_COL_ACCENT, UI_COL_BG, 70);
        *text = lv_color_mix(UI_COL_ACCENT, UI_COL_BG, 140);
        break;
    case UI_ACTION_DANGER:
        *fill = UI_COL_PANEL;
        *text = lv_color_mix(UI_COL_ERR, UI_COL_BG, 110);
        break;
    case UI_ACTION_NEUTRAL:
    default:
        *fill = UI_COL_PANEL;
        *text = UI_COL_TEXT_FAINT;
        break;
    }
}

/* Apply the enabled or disabled look of @p kind to one band button. */
static void band_style(lv_obj_t *btn, ui_action_kind_t kind, bool enabled)
{
    lv_obj_t *label = lv_obj_get_child(btn, 0);
    bool accent = enabled && kind == UI_ACTION_ACCENT;
    lv_color_t fill, text;

    if(!enabled)     disabled_colors(kind, &fill, &text);
    else if(accent) { fill = UI_COL_ACCENT;   text = UI_COL_BG; }
    else            { fill = UI_COL_PANEL_HI; text = (kind == UI_ACTION_DANGER) ? UI_COL_ERR
                                                                                : UI_COL_TEXT; }

    lv_obj_set_style_bg_color(btn, fill, 0);
    lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(btn, enabled ? lv_color_lighten(fill, 20) : fill,
                              LV_STATE_PRESSED);

    /* Accent is the one filled band; the others are told apart from the screen
     * by a hairline at their top edge. */
    lv_obj_set_style_border_width(btn, accent ? 0 : UI_PX(1), 0);
    lv_obj_set_style_border_color(btn, UI_COL_LINE, 0);
    lv_obj_set_style_border_side(btn, LV_BORDER_SIDE_TOP, 0);
    lv_obj_set_style_border_opa(btn, LV_OPA_COVER, 0);

    if(label != NULL) lv_obj_set_style_text_color(label, text, 0);
    lv_obj_set_clickable(btn, enabled);
}

/* One half of a band: a button with a centered label, positioned by the caller. */
static lv_obj_t *band_button(lv_obj_t *parent, const char *label, ui_action_kind_t kind,
                             lv_event_cb_t cb, void *user, int x, int w)
{
    lv_obj_t *btn = lv_button_create(parent);
    lv_obj_t *l;

    lv_obj_remove_style_all(btn);
    lv_obj_set_pos(btn, x, 0);
    lv_obj_set_size(btn, w, UI_ACTIONBAR_H);
    lv_obj_set_scrollable(btn, false);
    lv_obj_set_style_pad_all(btn, 0, 0);
    lv_obj_set_style_radius(btn, 0, 0);
    lv_obj_set_user_data(btn, KIND_CODE(kind));

    band_ensure_transition();
    lv_obj_set_style_transition(btn, &s_band_transition, 0);

    l = lv_label_create(btn);
    lv_obj_remove_style_all(l);
    lv_label_set_text(l, label != NULL ? label : "");
    lv_label_set_long_mode(l, LV_LABEL_LONG_MODE_CLIP);
    lv_obj_set_style_text_font(l, UI_FONT_L, 0);
    lv_obj_center(l);

    band_style(btn, kind, true);

    if(cb != NULL) lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, user);
    return btn;
}

lv_obj_t *ui_action_bar(lv_obj_t *parent, const char *label, ui_action_kind_t kind,
                        lv_event_cb_t cb, void *user)
{
    lv_obj_t *band = band_button(parent, label, kind, cb, user, 0, UI_SCREEN_W);

    lv_obj_set_y(band, UI_SCREEN_H - UI_ACTIONBAR_H);
    return band;
}

lv_obj_t *ui_action_bar2(lv_obj_t *parent, const char *left, ui_action_kind_t left_kind,
                         lv_event_cb_t left_cb, void *left_user, const char *right,
                         ui_action_kind_t right_kind, lv_event_cb_t right_cb, void *right_user)
{
    const int half = UI_SCREEN_W / 2;
    lv_obj_t *band = lv_obj_create(parent);
    lv_obj_t *rule;

    lv_obj_remove_style_all(band);
    lv_obj_set_pos(band, 0, UI_SCREEN_H - UI_ACTIONBAR_H);
    lv_obj_set_size(band, UI_SCREEN_W, UI_ACTIONBAR_H);
    lv_obj_set_scrollable(band, false);
    lv_obj_set_clickable(band, false);
    lv_obj_set_style_pad_all(band, 0, 0);

    band_button(band, left, left_kind, left_cb, left_user, 0, half);
    band_button(band, right, right_kind, right_cb, right_user, half, half);

    /* 1 px wide, so it sits on the seam between the two halves. */
    rule = lv_obj_create(band);
    lv_obj_remove_style_all(rule);
    lv_obj_set_size(rule, UI_PX(1), BAND_RULE_H);
    lv_obj_set_pos(rule, half - UI_PX(1), (UI_ACTIONBAR_H - BAND_RULE_H) / 2);
    lv_obj_set_scrollable(rule, false);
    lv_obj_set_style_bg_color(rule, UI_COL_LINE, 0);
    lv_obj_set_style_bg_opa(rule, LV_OPA_COVER, 0);

    return band;
}

void ui_action_bar_set_enabled(lv_obj_t *bar, bool enabled)
{
    uint32_t i, n;

    if(bar == NULL) return;

    if(lv_obj_get_user_data(bar) != NULL) band_style(bar, KIND_OF(bar), enabled);

    n = lv_obj_get_child_count(bar);
    for(i = 0; i < n; i++) {
        lv_obj_t *half = lv_obj_get_child(bar, (int32_t)i);

        if(lv_obj_get_user_data(half) != NULL) band_style(half, KIND_OF(half), enabled);
    }
}

void ui_action_bar2_set_half_enabled(lv_obj_t *bar, int half, bool enabled)
{
    lv_obj_t *btn;

    if(bar == NULL || half < 0 || half > 1) return;

    /* The two halves are the first two children; the seam rule follows them. */
    btn = lv_obj_get_child(bar, half);
    if(btn == NULL || lv_obj_get_user_data(btn) == NULL) return;
    band_style(btn, KIND_OF(btn), enabled);
}

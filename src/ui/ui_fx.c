/**
 * @file ui_fx.c
 * The design system's motion. Deliberately tiny: one modal entrance. Anything
 * that moves without telling the user something is noise.
 */

#include "ui/ui_fx.h"

#include "ui/ui_theme.h"

static void anim_opa_cb(void *var, int32_t v)
{
    lv_obj_set_style_bg_opa((lv_obj_t *)var, (lv_opa_t)v, 0);
}

static void anim_translate_y_cb(void *var, int32_t v)
{
    lv_obj_set_style_translate_y((lv_obj_t *)var, v, 0);
}

void ui_fx_button_style(lv_obj_t *btn, ui_style_id_t kind)
{
    lv_style_t *state;

    if(btn == NULL) return;

    lv_obj_add_style(btn, ui_style(kind), 0);

    /* Pressed and disabled cannot share one lv_style_t, so they live in their
     * own styles that the theme hands out per-button-kind. */
    state = ui_style_state(kind, UI_STATE_STYLE_PRESSED);
    if(state != NULL) lv_obj_add_style(btn, state, LV_STATE_PRESSED);

    state = ui_style_state(kind, UI_STATE_STYLE_DISABLED);
    if(state != NULL) lv_obj_add_style(btn, state, LV_STATE_DISABLED);
}

lv_obj_t *ui_fx_button(lv_obj_t *parent, const char *label, ui_style_id_t kind,
                       int x, int y, int w, int h, lv_event_cb_t cb, void *user_data)
{
    lv_obj_t *btn = lv_button_create(parent);
    lv_obj_t *lbl;

    lv_obj_remove_style_all(btn);
    ui_fx_button_style(btn, kind);
    lv_obj_set_pos(btn, x, y);
    lv_obj_set_size(btn, w, h);

    if(label != NULL) {
        lbl = lv_label_create(btn);
        lv_obj_remove_style_all(lbl);
        lv_label_set_text(lbl, label);
        lv_obj_center(lbl);
    }

    if(cb != NULL) lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, user_data);

    return btn;
}

void ui_fx_fade_in(lv_obj_t *obj, lv_opa_t to, uint32_t ms)
{
    lv_anim_t a;

    if(obj == NULL) return;

    if(!UI_FX_MOTION) {
        lv_obj_set_style_bg_opa(obj, to, 0);
        return;
    }

    lv_obj_set_style_bg_opa(obj, LV_OPA_TRANSP, 0);

    lv_anim_init(&a);
    lv_anim_set_var(&a, obj);
    lv_anim_set_exec_cb(&a, anim_opa_cb);
    lv_anim_set_values(&a, LV_OPA_TRANSP, to);
    lv_anim_set_duration(&a, ms);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_out);
    lv_anim_start(&a);
}

void ui_fx_slide_up_in(lv_obj_t *obj, int dy, uint32_t ms)
{
    lv_anim_t a;

    if(obj == NULL) return;

    if(!UI_FX_MOTION) return;   /* already where it ends */

    lv_obj_set_style_translate_y(obj, dy, 0);

    lv_anim_init(&a);
    lv_anim_set_var(&a, obj);
    lv_anim_set_exec_cb(&a, anim_translate_y_cb);
    lv_anim_set_values(&a, dy, 0);
    lv_anim_set_duration(&a, ms);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_out);
    lv_anim_start(&a);
}

void ui_fx_modal_animate_in(lv_obj_t *scrim, lv_obj_t *card)
{
    ui_fx_fade_in(scrim, LV_OPA_60, UI_FX_MODAL_MS);
    ui_fx_slide_up_in(card, UI_FX_MODAL_RISE, UI_FX_MODAL_MS);
}

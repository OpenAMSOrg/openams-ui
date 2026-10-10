/**
 * @file ui_steps.c
 * The loading step list: one 40 px row per step, each a 16 px state circle
 * plus its label, threaded by a rail. See docs/VISUAL_SPEC_480.md, "Loading".
 */

#include "ui/ui_steps.h"

#include "ui/ui_icons.h"
#include "ui/ui_theme.h"

#define STEP_ROW_H    40
#define STEP_DOT_X    16
#define STEP_DOT_SIZE 16
#define STEP_DOT_Y    ((STEP_ROW_H - STEP_DOT_SIZE) / 2)
#define STEP_LABEL_X  44
#define STEP_LABEL_W  (UI_SCREEN_W - STEP_LABEL_X - 16)

#define STEP_RAIL_X 23
#define STEP_RAIL_W 2

/* The 16 px circle every state starts from: no theme styles, no interaction. */
static lv_obj_t *step_dot(lv_obj_t *row)
{
    lv_obj_t *dot = lv_obj_create(row);

    lv_obj_remove_style_all(dot);
    lv_obj_set_pos(dot, STEP_DOT_X, STEP_DOT_Y);
    lv_obj_set_size(dot, STEP_DOT_SIZE, STEP_DOT_SIZE);
    lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_opa(dot, LV_OPA_TRANSP, 0);
    lv_obj_set_clickable(dot, false);
    lv_obj_set_scrollable(dot, false);
    return dot;
}

/* The glyph inside a done / failed circle. The 12 px text font falls back to
 * the icon font, so the icon fits and centers in the 16 px circle. */
static void step_dot_icon(lv_obj_t *dot, const char *icon)
{
    lv_obj_t *l = lv_label_create(dot);

    lv_obj_remove_style_all(l);
    lv_label_set_text(l, icon);
    lv_obj_set_style_text_font(l, UI_FONT_S, 0);
    lv_obj_set_style_text_color(l, UI_COL_BG, 0);
    lv_obj_center(l);
}

static void step_arc_spin_cb(void *var, int32_t v)
{
    lv_arc_set_end_angle((lv_obj_t *)var, v);
}

/* The active circle: a ring whose indicator sweeps once every 1100 ms. */
static void step_arc(lv_obj_t *row)
{
    lv_obj_t *arc = lv_arc_create(row);
    lv_anim_t a;

    lv_obj_remove_style_all(arc);
    lv_obj_set_pos(arc, STEP_DOT_X, STEP_DOT_Y);
    lv_obj_set_size(arc, STEP_DOT_SIZE, STEP_DOT_SIZE);
    lv_arc_set_rotation(arc, 270);
    lv_arc_set_bg_angles(arc, 0, 360);
    lv_obj_set_style_arc_width(arc, 2, 0);
    lv_obj_set_style_arc_color(arc, UI_COL_LINE, 0);
    lv_obj_set_style_arc_width(arc, 2, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(arc, UI_COL_ACCENT, LV_PART_INDICATOR);
    lv_obj_set_style_arc_rounded(arc, true, LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(arc, LV_OPA_TRANSP, LV_PART_KNOB);
    lv_obj_set_style_pad_all(arc, 0, LV_PART_KNOB);
    lv_obj_set_clickable(arc, false);
    lv_obj_set_scrollable(arc, false);

    lv_anim_init(&a);
    lv_anim_set_var(&a, arc);
    lv_anim_set_exec_cb(&a, step_arc_spin_cb);
    lv_anim_set_values(&a, 0, 360);
    lv_anim_set_duration(&a, 1100);
    lv_anim_set_path_cb(&a, lv_anim_path_linear);
    lv_anim_set_repeat_count(&a, LV_ANIM_REPEAT_INFINITE);
    lv_anim_start(&a);
}

static void step_label(lv_obj_t *row, const char *text, lv_color_t color)
{
    lv_obj_t *l = lv_label_create(row);

    lv_obj_remove_style_all(l);
    lv_label_set_text(l, text);
    lv_label_set_long_mode(l, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_set_width(l, STEP_LABEL_W);
    lv_obj_set_style_text_font(l, UI_FONT_M, 0);
    lv_obj_set_style_text_color(l, color, 0);
    lv_obj_align(l, LV_ALIGN_LEFT_MID, STEP_LABEL_X, 0);
}

lv_obj_t *ui_step_row(lv_obj_t *parent, int y, const char *label, ui_step_state_t state)
{
    lv_obj_t *row = lv_obj_create(parent);
    lv_obj_t *dot;
    lv_color_t label_color;

    lv_obj_remove_style_all(row);
    lv_obj_set_pos(row, 0, y);
    lv_obj_set_size(row, UI_SCREEN_W, STEP_ROW_H);
    lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_pad_all(row, 0, 0);
    lv_obj_set_clickable(row, false);
    lv_obj_set_scrollable(row, false);

    switch(state) {
        case UI_STEP_DONE:
            dot = step_dot(row);
            lv_obj_set_style_bg_color(dot, UI_COL_ACCENT, 0);
            lv_obj_set_style_bg_opa(dot, LV_OPA_COVER, 0);
            step_dot_icon(dot, UI_ICON_CHECK);
            label_color = UI_COL_TEXT_DIM;
            break;
        case UI_STEP_ACTIVE:
            step_arc(row);
            label_color = UI_COL_TEXT;
            break;
        case UI_STEP_PENDING:
            dot = step_dot(row);
            lv_obj_set_style_border_width(dot, 1, 0);
            lv_obj_set_style_border_color(dot, UI_COL_TEXT_FAINT, 0);
            lv_obj_set_style_border_opa(dot, LV_OPA_COVER, 0);
            label_color = UI_COL_TEXT_FAINT;
            break;
        default: /* UI_STEP_FAILED */
            dot = step_dot(row);
            lv_obj_set_style_bg_color(dot, UI_COL_ERR, 0);
            lv_obj_set_style_bg_opa(dot, LV_OPA_COVER, 0);
            step_dot_icon(dot, UI_ICON_CLOSE);
            label_color = UI_COL_ERR;
            break;
    }

    step_label(row, label, label_color);
    return row;
}

static void step_rail_segment(lv_obj_t *parent, int y, int h, lv_color_t color)
{
    lv_obj_t *o = lv_obj_create(parent);

    lv_obj_remove_style_all(o);
    lv_obj_set_pos(o, STEP_RAIL_X, y);
    lv_obj_set_size(o, STEP_RAIL_W, h);
    lv_obj_set_style_bg_color(o, color, 0);
    lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
    lv_obj_set_clickable(o, false);
    lv_obj_set_scrollable(o, false);
}

void ui_step_rail(lv_obj_t *parent, int y_first_center, int y_last_center,
                  int y_progress_center)
{
    step_rail_segment(parent, y_first_center, y_last_center - y_first_center + 1,
                      UI_COL_LINE);

    if(y_progress_center > y_first_center)
        step_rail_segment(parent, y_first_center, y_progress_center - y_first_center + 1,
                          UI_COL_ACCENT);
}

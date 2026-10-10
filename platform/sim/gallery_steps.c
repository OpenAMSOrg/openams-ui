/**
 * @file gallery_steps.c
 * Gallery page for the steps family: the loading screen's body (header,
 * swatch, rail and the five step rows).
 */

#include "gallery.h"

#include "ui/ui_steps.h"
#include "ui/ui_theme.h"

static const struct {
    const char *label;
    ui_step_state_t state;
} s_steps[] = {
    { "Heat the nozzle", UI_STEP_DONE },
    { "Cut filament", UI_STEP_DONE },
    { "Pull back current filament", UI_STEP_ACTIVE },
    { "Push new filament", UI_STEP_PENDING },
    { "Purge", UI_STEP_FAILED },
};

static lv_obj_t *page_label(lv_obj_t *parent, const char *text,
                            const lv_font_t *font, lv_color_t color, int x, int y)
{
    lv_obj_t *l = lv_label_create(parent);

    lv_obj_remove_style_all(l);
    lv_label_set_text(l, text);
    lv_label_set_long_mode(l, LV_LABEL_LONG_MODE_CLIP);
    lv_obj_set_style_text_font(l, font, 0);
    lv_obj_set_style_text_color(l, color, 0);
    lv_obj_set_pos(l, x, y);
    return l;
}

void gallery_page_steps(lv_obj_t *scr)
{
    lv_color_t top;
    lv_color_t bottom;
    lv_obj_t *swatch;
    int i;

    page_label(scr, "Loading A3", UI_FONT_XL, UI_COL_TEXT, 16, 6);

    swatch = lv_obj_create(scr);
    lv_obj_remove_style_all(swatch);
    lv_obj_set_pos(swatch, 16, 36);
    lv_obj_set_size(swatch, 10, 10);
    lv_obj_set_style_radius(swatch, LV_RADIUS_CIRCLE, 0);
    ui_filament_gradient(0xD23B3B, &top, &bottom);
    lv_obj_set_style_bg_color(swatch, top, 0);
    lv_obj_set_style_bg_opa(swatch, LV_OPA_COVER, 0);
    lv_obj_set_clickable(swatch, false);
    lv_obj_set_scrollable(swatch, false);

    page_label(scr, "PLA · Red", UI_FONT_M, UI_COL_TEXT, 32, 34);

    /* Row i's dot center is at its 40 px row's own y + 20 (see ui_step_row):
     * row 0 at 60 centers at 80, the last (row 4) at 220 centers at 240. */
    ui_step_rail(scr, 80, 240, 160);

    for(i = 0; i < (int)(sizeof s_steps / sizeof s_steps[0]); i++)
        ui_step_row(scr, 60 + i * 40, s_steps[i].label, s_steps[i].state);
}

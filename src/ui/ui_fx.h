/**
 * @file ui_fx.h
 * Motion and widget-state helpers. This is the only place the design system
 * animates anything: a modal opening, and nothing else. Screens do not get
 * transitions between them.
 */

#ifndef UI_FX_H
#define UI_FX_H

#include "lvgl.h"

#include "ui/ui_theme.h"

/** Duration of the modal entrance, and how far the card rises. */
/* Motion is off on the ESP32. A modal's first frame redraws the whole screen
 * under its scrim, which takes about 300 ms there, so a 140 ms entrance
 * cannot be shown: the first frame lands mid-animation and the next one jumps
 * to the end - the sheet visibly moves and grows right after it appears.
 * Without motion it appears once, where it stays, and no animation frame
 * redraws the screen again. The simulator keeps the motion. */
#if defined(ESP_PLATFORM)
    #define UI_FX_MOTION 0
#else
    #define UI_FX_MOTION 1
#endif

#define UI_FX_MODAL_MS   160
#define UI_FX_MODAL_RISE 12

/** Apply @p kind to @p btn together with its pressed and disabled states and
 * its 100 ms background transition. Prefer this over a bare
 * lv_obj_add_style() for anything the user can touch. */
void ui_fx_button_style(lv_obj_t *btn, ui_style_id_t kind);

/** A button styled by ui_fx_button_style() with a centered label. @p cb may be
 * NULL. The label inherits the button's font and color. */
lv_obj_t *ui_fx_button(lv_obj_t *parent, const char *label, ui_style_id_t kind,
                       int x, int y, int w, int h, lv_event_cb_t cb, void *user_data);

/** Fade a background from transparent to @p to over @p ms (ease-out). */
void ui_fx_fade_in(lv_obj_t *obj, lv_opa_t to, uint32_t ms);

/** Slide @p obj into place: it starts @p dy lower and eases up to 0. */
void ui_fx_slide_up_in(lv_obj_t *obj, int dy, uint32_t ms);

/** The modal entrance: fade @p scrim in to 60% while @p card rises 12 px. */
void ui_fx_modal_animate_in(lv_obj_t *scrim, lv_obj_t *card);

#endif /* UI_FX_H */

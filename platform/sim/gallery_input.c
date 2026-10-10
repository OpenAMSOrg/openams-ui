/**
 * @file gallery_input.c
 * Gallery page for the input family: steppers (plain and captioned), a row of
 * material chips, and both toggle states.
 */

#include "gallery.h"

#include "ui/ui_input.h"
#include "ui/ui_theme.h"

static void on_noop(lv_event_t *e)
{
    (void)e;
}

/* Chips are independent here; clicking one just flips its own state. */
static void on_chip_click(lv_event_t *e)
{
    lv_obj_t *c = lv_event_get_target_obj(e);

    ui_chip_set_selected(c, !lv_obj_has_state(c, LV_STATE_CHECKED));
}

void gallery_page_input(lv_obj_t *scr)
{
    static const char *mats[] = { "PLA", "PETG", "ABS", "PA", "TPU" };
    /* The Environment screen's own chip row: x = 14, 106, 198, 290, 382. */
    static const int chip_x[5] = { 14, 106, 198, 290, 382 };
    lv_obj_t *t;
    int i;

    (void)ui_stepper(scr, 8, 8, 220, "55\xC2\xB0""C", on_noop, on_noop, NULL);
    (void)ui_stepper(scr, 244, 8, 220, "12 h", on_noop, on_noop, NULL);

    (void)ui_stepper_captioned(scr, 8, 56, 220, "Nozzle min", "190\xC2\xB0""C",
                               on_noop, on_noop, NULL);
    (void)ui_stepper_captioned(scr, 244, 56, 220, "K value", "0.020",
                               on_noop, on_noop, NULL);

    /* The 84 px fixed-width chip - ui_chip_fixed(), not the content-driven
     * ui_chip() a caller uses where the spec leaves the width to the label. */
    for(i = 0; i < 5; i++) {
        lv_obj_t *c = ui_chip_fixed(scr, mats[i], 84, i == 0, on_chip_click, NULL);

        lv_obj_set_pos(c, chip_x[i], 120);
    }

    t = ui_toggle(scr, true, on_noop, NULL);
    lv_obj_set_pos(t, 8, 170);
    t = ui_toggle(scr, false, on_noop, NULL);
    lv_obj_set_pos(t, 60, 170);
}

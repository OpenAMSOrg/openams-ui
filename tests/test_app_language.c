/**
 * @file test_app_language.c
 * A display on a printer boots through app_live_init(), not app_init(), and
 * that start-up applied no language: the one chosen in Settings was dropped at
 * every boot (English), and the four font slots the Geist faces fall back to
 * were never filled, so the first glyph Geist lacks ("日本語" in Settings, a
 * symbol in a host's or a spool's name) called a null lookup and crashed.
 *
 * Real LVGL with the dummy SDL driver; the simulator's display_cfg.c is the
 * language store, as on the device's NVS.
 */

#include "app/app.h"
#include "app/app_live.h"
#include "app/platform.h"
#include "lvgl.h"

#include "model/mmu_model.h"
#include "ui/fonts/ui_fonts_home.h"
#include "ui/ui_home.h"
#include "ui/ui_theme.h"
#include "view/strings.h"

#include <stdbool.h>
#include <stdio.h>
#include <string.h>

/* --------------------------------------------------------------- the hooks */

void platform_action(const char *line)
{
    (void)line;
}

platform_link_t platform_link_status(char *detail, size_t n)
{
    if(detail && n) detail[0] = '\0';
    return PLATFORM_LINK_READY;
}

unsigned platform_link_units(char *names, size_t n)
{
    if(names && n) names[0] = '\0';
    return 0;
}

void platform_touch_calibrate(void)
{
}

/* ----------------------------------------------------------------- harness */

static int s_failures;

static void check(bool ok, const char *what)
{
    printf("%s %s\n", ok ? "ok  " : "FAIL", what);
    if(!ok) s_failures++;
}

static const char *current(void)
{
    return view_lang_code(view_lang_current());
}

/* ------------------------------------------------------------------- tests */

static void test_live_boot_with_nothing_stored(void)
{
    lv_obj_t *l;

    app_live_init();
    check(strcmp(current(), "en") == 0, "a live boot with nothing stored is in English");
    check(ui_font_cjk_slot_12.get_glyph_dsc && ui_font_cjk_slot_13.get_glyph_dsc &&
          ui_font_cjk_slot_15.get_glyph_dsc && ui_font_cjk_slot_20.get_glyph_dsc,
          "  and every font slot has a glyph lookup");
    if(!ui_font_cjk_slot_13.get_glyph_dsc) return;   /* the label below would crash */

    /* What crashed: a label in a Geist face, with text Geist does not carry. */
    l = lv_label_create(lv_screen_active());
    lv_obj_set_style_text_font(l, UI_HOME_FONT_BODY, 0);
    lv_label_set_text(l, "\xE6\x97\xA5\xE6\x9C\xAC\xE8\xAA\x9E \xE2\x84\xA2");   /* 日本語 ™ */
    lv_obj_update_layout(l);
    check(true, "  and a glyph Geist lacks is laid out without a crash");
}

static void test_live_boot_applies_the_stored_language(void)
{
    platform_language_set("de");
    app_live_init();
    check(strcmp(current(), "de") == 0, "a live boot applies the language chosen in Settings");
}

static void test_a_code_not_built_in_falls_back_to_english(void)
{
    platform_language_set("xx");
    app_live_init();
    check(strcmp(current(), "en") == 0, "a stored code this image does not carry boots in English");
}

int main(void)
{
    lv_init();
    {
        lv_display_t *disp = lv_sdl_window_create(UI_SCREEN_W, UI_SCREEN_H);

        (void)disp;
        lv_sdl_mouse_create();
    }

    mmu_model_init();

    test_live_boot_with_nothing_stored();
    test_live_boot_applies_the_stored_language();
    test_a_code_not_built_in_falls_back_to_english();

    printf("%s: %d failure(s)\n", s_failures ? "FAILED" : "PASSED", s_failures);
    return s_failures ? 1 : 0;
}

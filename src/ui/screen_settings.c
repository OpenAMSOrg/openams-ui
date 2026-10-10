/**
 * @file screen_settings.c
 * Screen 9: settings, in the design language (ui_kit). The view's panel
 * settings drive it: the Language row is a choice, drawn as a selectable list
 * (a tap switches the view's language and rebuilds the screen), the host's five
 * settings are switches. Below them: a demo-only drying row, the Display
 * section (touch calibration, brightness, contrast, dim and off timers,
 * rotation) and the rows that lead to the display binding, unit info and the
 * Groups screen. OpenAMS has no auto-refill on/off switch - a group's declared
 * order is the whole policy - so there is none here (docs/GROUPS.md).
 */
#include "ui/ui_screens.h"
#include "ui/ui_misc.h"

#include "app/actions.h"
#include "app/platform.h"

#include "model/mmu_model.h"
#include "ui/ui_icons.h"
#include "ui/ui_kit.h"
#include "ui/ui_language.h"
#include "ui/ui_nav.h"
#include "view/strings.h"
#include "view/view.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

/* Switch state lives here: one screen is alive at a time and these survive a
 * rebuild, which is all the dummy phase needs. The five rows are the host's
 * five settings (C2 4.9); each logs the C2 key, which is what the actions map
 * turns into OAMSM_SET_OPTION (H7). */
#define SETTINGS_N 5

static const char *const s_key[SETTINGS_N] = {
    /* lit-ok: the host's C2 setting keys, wire tokens */
    "read_tag_on_insertion", "read_tags_at_startup", "auto_reload_on_runout", /* lit-ok */
    "auto_create_spoolman_spools", "apply_pa_on_load", /* lit-ok */
};

/** The model field a row index edits. */
static bool *setting_field(mmu_settings_t *s, int i)
{
    switch(i) {
    case 0: return &s->read_tag_on_insertion;
    case 1: return &s->read_tags_at_startup;
    case 2: return &s->auto_reload_on_runout;
    case 3: return &s->auto_create_spoolman_spools;
    case 4: return &s->apply_pa_on_load;
    }
    return NULL;
}

/* ------------------------------------------------------------- display cfg */

static platform_display_cfg_t s_disp;
static lv_obj_t *s_bright_val;
static lv_obj_t *s_contrast_chips[2];
static lv_obj_t *s_dim_chips[4];
static lv_obj_t *s_off_chips[4];

static const uint16_t s_dim_opts[4] = { 0, 60, 300, 600 };
static const uint16_t s_off_opts[4] = { 0, 600, 900, 1800 };

/* The timer chips' labels: "Never" for 0, else "N min", in the current language. */
static void timer_labels(char (*buf)[32], const char **out, const uint16_t *opts)
{
    int i;

    for(i = 0; i < 4; i++) {
        if(opts[i] == 0) {
            snprintf(buf[i], 32, "%s", view_str(VS_SET_NEVER));
        }
        else {
            vs_format(VS_SET_MINUTES, buf[i], 32, (vs_arg_t[]){ VS_ARG_N("n", opts[i] / 60) }, 1);
        }
        out[i] = buf[i];
    }
}

static int find_opt(const uint16_t *opts, int n, uint16_t v)
{
    int i;

    for(i = 0; i < n; i++)
        if(opts[i] == v) return i;
    return 0;
}

static void apply_disp(void)
{
    platform_display_set(&s_disp);
}

static void sync_dim_chips(void)
{
    int sel = find_opt(s_dim_opts, 4, s_disp.dim_after_s);
    int i;

    for(i = 0; i < 4; i++)
        if(s_dim_chips[i]) ui_kit_chip_set_selected(s_dim_chips[i], i == sel);
}

static void sync_off_chips(void)
{
    int sel = find_opt(s_off_opts, 4, s_disp.off_after_s);
    int i;

    for(i = 0; i < 4; i++)
        if(s_off_chips[i]) ui_kit_chip_set_selected(s_off_chips[i], i == sel);
}

/** The off timer never fires before the dim timer: "Never" (0) on either side
 * is exempt (it never fires at all, so it cannot be the shorter one). */
static void clamp_off_after_dim(void)
{
    int i;

    if(s_disp.dim_after_s == 0 || s_disp.off_after_s == 0) return;
    if(s_disp.off_after_s >= s_disp.dim_after_s) return;

    for(i = 0; i < 4; i++) {
        if(s_off_opts[i] >= s_disp.dim_after_s) {
            s_disp.off_after_s = s_off_opts[i];
            return;
        }
    }
    s_disp.off_after_s = s_off_opts[3];
}

static void adjust_brightness(int dir)
{
    char buf[24];
    int v = (int)s_disp.brightness_pct + dir * 10;

    if(v < 10) v = 10;
    if(v > 100) v = 100;
    s_disp.brightness_pct = (uint8_t)v;
    apply_disp();
    actions_log("display brightness %d", v);
    vs_format(VS_SET_BRIGHT_VALUE, buf, sizeof buf, (vs_arg_t[]){ VS_ARG_N("pct", v) }, 1);
    if(s_bright_val) lv_label_set_text(s_bright_val, buf);
}

static void on_brightness_minus(lv_event_t *e) { (void)e; adjust_brightness(-1); }
static void on_brightness_plus(lv_event_t *e)  { (void)e; adjust_brightness(+1); }

static void on_contrast(lv_event_t *e)
{
    int idx = (int)(intptr_t)lv_event_get_user_data(e);
    int i;

    s_disp.contrast = (uint8_t)idx;
    apply_disp();
    actions_log("display contrast %s", idx ? "high" : "standard");
    for(i = 0; i < 2; i++)
        if(s_contrast_chips[i]) ui_kit_chip_set_selected(s_contrast_chips[i], i == idx);
}

static void on_dim(lv_event_t *e)
{
    int idx = (int)(intptr_t)lv_event_get_user_data(e);

    s_disp.dim_after_s = s_dim_opts[idx];
    clamp_off_after_dim();
    apply_disp();
    actions_log("display dim-after %ds", s_disp.dim_after_s);
    sync_dim_chips();
    sync_off_chips();
}

static void on_off(lv_event_t *e)
{
    int idx = (int)(intptr_t)lv_event_get_user_data(e);

    s_disp.off_after_s = s_off_opts[idx];
    /* The option domains never let this side get shorter than dim (off's
     * shortest nonzero option equals dim's longest), but the invariant is
     * enforced from both directions on principle. */
    if(s_disp.dim_after_s != 0 && s_disp.off_after_s != 0 &&
       s_disp.off_after_s < s_disp.dim_after_s) {
        int j;

        for(j = 3; j >= 0; j--) {
            if(s_dim_opts[j] <= s_disp.off_after_s) {
                s_disp.dim_after_s = s_dim_opts[j];
                break;
            }
        }
        sync_dim_chips();
    }
    apply_disp();
    actions_log("display off-after %ds", s_disp.off_after_s);
    sync_off_chips();
}

static void on_rotate(lv_event_t *e)
{
    lv_obj_t *toggle = lv_event_get_target_obj(e);

    if(!toggle) return;
    s_disp.flip = ui_kit_switch_get(toggle);
    apply_disp();
    actions_log("display flip %s", s_disp.flip ? "on" : "off");
}

static void on_dry_rotate(lv_event_t *e)
{
    lv_obj_t *toggle = lv_event_get_target_obj(e);
    bool on;

    if(!toggle) return;
    on = ui_kit_switch_get(toggle);
    mmu_this()->dry_rotate = on;
    actions_log("rotate %s", on ? "on" : "off");
}

static void on_toggle(lv_event_t *e)
{
    lv_obj_t *toggle = lv_event_get_target_obj(e);
    int i = (int)(intptr_t)lv_event_get_user_data(e);
    bool *field;

    if(!toggle) return;
    field = setting_field(&g_mmu.settings, i);
    if(!field) return;
    *field = ui_kit_switch_get(toggle);
    actions_log("setting %s %s", s_key[i], *field ? "on" : "off");
}

/* ------------------------------------------------------------------ rows */

/* A card row with a title, a note and a switch at the right. */
static lv_obj_t *switch_row(lv_obj_t *col, const char *title, const char *note, bool on,
                            lv_event_cb_t cb, void *user)
{
    lv_obj_t *row = ui_kit_row(col, 0, NULL, NULL);
    int text_w = UI_HOME_INNER_W - 24 - 40 - 12;
    lv_obj_t *t = ui_kit_text(row, title, UI_HOME_FONT_BODY, UI_TOK_TEXT, 0, 0, text_w);
    lv_obj_t *sw;

    if(note && note[0]) {
        lv_obj_t *n = ui_kit_text(row, note, UI_HOME_FONT_META, UI_TOK_TEXT_MUTED, 0, 0, text_w);

        lv_obj_align_to(n, t, LV_ALIGN_OUT_BOTTOM_LEFT, 0, 2);
    }
    sw = ui_kit_switch(row, 0, 0, on, cb, user);
    lv_obj_align(sw, LV_ALIGN_RIGHT_MID, 0, 0);
    lv_obj_set_style_min_height(row, 46, 0);
    return row;
}

static void on_nav_row(lv_event_t *e)
{
    ui_nav_goto_async((ui_screen_id_t)(int)(intptr_t)lv_event_get_user_data(e));
}

static void on_calibrate_touch(lv_event_t *e)
{
    (void)e;
    platform_touch_calibrate();
}

static void nav_row(lv_obj_t *col, const char *title, const char *sub, ui_screen_id_t id)
{
    (void)ui_kit_text_row(col, title, sub, UI_ICON_CHEVRON_RIGHT, UI_TOK_TEXT_FAINT,
                          on_nav_row, (void *)(intptr_t)id);
}

/* A row with a title and a "-  value  +" stepper. */
static lv_obj_t *stepper_row(lv_obj_t *col, const char *title, const char *value,
                             lv_event_cb_t on_minus, lv_event_cb_t on_plus)
{
    lv_obj_t *row = ui_kit_row(col, 46, NULL, NULL);
    lv_obj_t *v;
    lv_obj_t *box;

    ui_home_label(row, title, UI_HOME_FONT_BODY, UI_TOK_TEXT, 0,
                  (46 - 24 - lv_font_get_line_height(UI_HOME_FONT_BODY)) / 2);
    v = ui_kit_stepper(row, UI_HOME_INNER_W - 24 - 140, (46 - 24 - 34) / 2, 140, value,
                       on_minus, on_plus);
    (void)box;
    return v;
}

/* A row with a title and a run of chips under it. */
static void chip_row(lv_obj_t *col, const char *title, const char *const *labels, int n,
                     lv_obj_t **out, lv_event_cb_t cb)
{
    lv_obj_t *row = ui_kit_row(col, 12 + 20 + 6 + 30 + 12, NULL, NULL);
    int x = 0;
    int i;

    ui_home_label(row, title, UI_HOME_FONT_BODY, UI_TOK_TEXT, 0, 0);
    for(i = 0; i < n; i++) {
        out[i] = ui_kit_chip(row, labels[i], x, 26, 0, false, cb, (void *)(intptr_t)i);
        x += ui_home_text_w(labels[i], UI_HOME_FONT_BODY) + 24 + 8;
    }
}

/* ------------------------------------------------------------- language */

static void on_language(lv_event_t *e)
{
    int idx = (int)(intptr_t)lv_event_get_user_data(e);
    const char *code = view_lang_code(idx);

    if(!code[0]) return;
    actions_log("language %s", code);
    /* Strings, font slots and the stored choice together. */
    if(ui_set_language(code)) ui_nav_refresh_async();
}

static void language_rows(lv_obj_t *col, const view_setting_t *st)
{
    int i;

    ui_kit_section(col, st->label);
    for(i = 0; i < (int)st->option_count; i++) {
        bool sel = (i == st->selected);

        (void)ui_kit_text_row(col, st->options[i].label, NULL,
                              sel ? UI_ICON_CHECK : NULL, UI_TOK_ACCENT,
                              on_language, (void *)(intptr_t)st->options[i].value);
    }
}

/* ------------------------------------------------------------------ build */

static lv_obj_t *build(void)
{
    lv_obj_t *scr = ui_kit_screen();
    lv_obj_t *col;
    mmu_settings_t *st = &g_mmu.settings;
    view_panel_t *panel = malloc(sizeof *panel);
    char sub[64];
    char dim_buf[4][32], off_buf[4][32];
    const char *dim_labels[4], *off_labels[4];
    int i;

    (void)ui_kit_header(scr, view_str(VS_LABEL_SETTINGS), true);
    platform_display_get(&s_disp);

    col = ui_kit_column(scr, UI_HOME_HEADER_H + 2, UI_HOME_H - UI_HOME_HEADER_H - 2);

    if(panel) view_panel(&g_mmu, panel);

    /* The language row: the view's choice, as a selectable list. */
    if(panel && panel->setting_count > VIEW_LANGUAGE_SETTING_ROW) {
        const view_setting_t *lang = &panel->settings[VIEW_LANGUAGE_SETTING_ROW];

        if(lang->is_choice) language_rows(col, lang);
    }

    ui_kit_section(col, view_str(VS_SET_FILAMENT));
    if(panel) {
        for(i = 0; i < SETTINGS_N && i < (int)panel->setting_count; i++) {
            const view_setting_t *v = &panel->settings[i];

            if(v->is_choice) continue;
            switch_row(col, v->label, v->note, *setting_field(st, i), on_toggle,
                       (void *)(intptr_t)i);
        }
    }

    /* Dry-rotate is a printer config option, not one of the host's five
     * settings, so a live host owns it: the row is demo-only. */
    if(!mmu_model_is_live()) {
        ui_kit_section(col, view_str(VS_SET_DRYING));
        switch_row(col, view_str(VS_SET_ROTATE), view_str(VS_SET_ROTATE_NOTE),
                   mmu_this()->dry_rotate, on_dry_rotate, NULL);
    }

    ui_kit_section(col, view_str(VS_ORDER_THIS_DISPLAY));
    snprintf(sub, sizeof sub, "%s (%s)", mmu_variant_name(mmu_this()->variant),
             mmu_this()->name);
    nav_row(col, view_str(VS_ORDER_THIS_DISPLAY), sub, UI_SCREEN_ORDER);
    nav_row(col, view_str(VS_SCREEN_UNITINFO), view_str(VS_SET_UNITINFO_SUB), UI_SCREEN_UNIT_INFO);
    nav_row(col, view_str(VS_SCREEN_GROUPS), view_str(VS_SET_GROUPS_SUB),
            UI_SCREEN_REFILL);

    ui_kit_section(col, view_str(VS_SET_DISPLAY));
    (void)ui_kit_text_row(col, view_str(VS_SET_CALIBRATE), view_str(VS_SET_CALIBRATE_SUB),
                          UI_ICON_CHEVRON_RIGHT, UI_TOK_TEXT_FAINT, on_calibrate_touch, NULL);
    {
        char bv[24];

        vs_format(VS_SET_BRIGHT_VALUE, bv, sizeof bv,
                  (vs_arg_t[]){ VS_ARG_N("pct", (int)s_disp.brightness_pct) }, 1);
        s_bright_val = stepper_row(col, view_str(VS_SET_BRIGHTNESS), bv, on_brightness_minus,
                                   on_brightness_plus);
    }
    {
        const char *contrast_labels[2] = { view_str(VS_SET_STANDARD), view_str(VS_SET_HIGH) };

        chip_row(col, view_str(VS_SET_CONTRAST), contrast_labels, 2, s_contrast_chips, on_contrast);
        for(i = 0; i < 2; i++)
            ui_kit_chip_set_selected(s_contrast_chips[i], i == s_disp.contrast);
    }
    timer_labels(dim_buf, dim_labels, s_dim_opts);
    chip_row(col, view_str(VS_SET_DIM), dim_labels, 4, s_dim_chips, on_dim);
    sync_dim_chips();
    timer_labels(off_buf, off_labels, s_off_opts);
    chip_row(col, view_str(VS_SET_OFF), off_labels, 4, s_off_chips, on_off);
    sync_off_chips();
    switch_row(col, view_str(VS_SET_ROTATE180), NULL, s_disp.flip, on_rotate, NULL);

    free(panel);
    return scr;
}

static int modal_count(void)
{
    return 0;
}

static void open_modal(int index)
{
    (void)index;
}

const ui_screen_ops_t screen_settings_ops = { build, modal_count, open_modal, NULL };

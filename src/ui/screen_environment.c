/**
 * @file screen_environment.c
 * Screen 6: environment and drying for the unit this display is mounted on -
 * what the header's environment pill opens - in the design language (ui_kit).
 *
 * One unit only: the humidity and temperature readout with the drying state
 * (the view's dryer text, which carries the time left, and tone), the material presets, the target
 * temperature and hours, and the Start / Stop button. A unit that cannot dry
 * right now says nothing about why: just its readouts, at the same positions.
 * Rotate spools lives in Settings.
 *
 * modal_count() == 2: 0 = the drying confirmation (or the cannot-dry
 * explanation), 1 = the humidity-scale explainer (the sheets are still the
 * shared bottom sheets; U3-B2 restyles them).
 */

#include "ui/ui_screens.h"

#include "app/actions.h"
#include "backend/actions_map.h"
#include "model/mmu_model.h"
#include "ui/ui_editenv_helpers.h"
#include "ui/ui_icons.h"
#include "ui/ui_kit.h"
#include "ui/ui_nav.h"
#include "ui/ui_sheet.h"
#include "ui/ui_theme.h"
#include "view/strings.h"
#include "view/view.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* ------------------------------------------------------------ drying rules */

typedef struct {
    const char *name;
    int c_std;      /* target for a 45-65 C unit */
    int c_ht;       /* target for a high-temperature unit */
    int hours;
} dry_preset_t;

static const dry_preset_t s_presets[] = {
    { "PLA",  45, 45, 12 },
    { "PETG", 65, 65, 12 },
    { "ABS",  65, 80,  8 },
    { "PA",   65, 85, 12 },
    { "TPU",  65, 75, 18 },
};
#define PRESET_COUNT ((int)(sizeof(s_presets) / sizeof(s_presets[0])))

#define DRY_T_MIN   45
#define DRY_T_STD   65
#define DRY_T_HT    85
#define DRY_H_MIN   1
#define DRY_H_MAX   24
/* Assumed cycle length when the drying was not started from this screen. */
#define DRY_TOTAL_DEFAULT 720

#define DEG   "\xC2\xB0"       /* degree sign */
#define MID   "\xC2\xB7"       /* middle dot separator */

/* ------------------------------------------------------------------ layout */

#define EN_PAD          UI_HOME_HEADER_PAD
#define EN_CARD_Y       48
#define EN_CARD_H       74
#define EN_CHIP_Y       130
#define EN_CHIP_W       84
#define EN_CHIP_GAP     9
#define EN_STEP_Y       168
#define EN_STEP_H       64
#define EN_STEP_W       224
#define EN_INFO_Y       240
#define EN_BTN_Y        268
#define EN_BTN_H        40

/* ------------------------------------------------------------------- state */

enum { A_TEMP, A_HOURS };

static int s_unit = -1;
static int s_target_c = 55;
static int s_hours = 12;
static int s_total_min;                 /* 0 = we did not start the cycle */
static int s_preset = -1;
static const char *s_bound_scenario;
static mmu_dry_state_t s_last_state = MMU_DRY_IDLE;

static struct {
    lv_obj_t *scr;
    lv_obj_t *humidity;     /* value labels of the readout card */
    lv_obj_t *temp;
    lv_obj_t *fill;         /* the drying progress bar's fill */
    lv_obj_t *chips[PRESET_COUNT];
    lv_obj_t *temp_val;
    lv_obj_t *hours_val;
    lv_obj_t *info;         /* preset name and the unit's range */
    lv_obj_t *elapsed;
    bool has_controls;
} s_ui;

static void env_refresh(void);
static void open_dry_confirm(void);
static void open_dry_info(const char *reason);
static void open_scale(void);

/* ---------------------------------------------------------------- helpers */

static bool dry_available(const mmu_unit_t *u)
{
    return u->can_dry && actions_dry_reason(u)[0] == '\0';
}

/** The variant sets the drying ceiling: AMS HT 85 C, everything else 65 C. */
static int unit_t_max(const mmu_unit_t *u)
{
    return (u->variant == MMU_VARIANT_AMS_HT) ? DRY_T_HT : DRY_T_STD;
}

static int preset_target(const mmu_unit_t *u, int i)
{
    return (u->variant == MMU_VARIANT_AMS_HT) ? s_presets[i].c_ht : s_presets[i].c_std;
}

static int preset_match(const mmu_unit_t *u, int c, int h)
{
    int i;

    for(i = 0; i < PRESET_COUNT; i++)
        if(preset_target(u, i) == c && s_presets[i].hours == h) return i;
    return -1;
}

static const char *reason_icon(const char *reason)
{
    if(strstr(reason, "adapter") || strstr(reason, "Adapter")
       || strstr(reason, "power") || strstr(reason, "Power"))
        return UI_ICON_POWER_PLUG;
    return UI_ICON_ALERT_CIRCLE;
}

/** How much of a cool-down span from the last drying target down to
 * ambient + 5 C is still ahead: 100 right after Stop, falling to 0 as the
 * unit settles. There is no measured cool-down total to divide elapsed time
 * by (unlike a dry cycle's hours), so this reads it off the temperature
 * itself. */
static int cooling_pct(const mmu_unit_t *u)
{
    int endc = u->ambient_c + 5;
    int startc = (s_target_c > endc) ? s_target_c : unit_t_max(u);
    int span = startc - endc;
    int done;

    if(span <= 0) return 0;
    done = u->temp_c - endc;
    if(done < 0) done = 0;
    if(done > span) done = span;
    return (done * 100) / span;
}

/** Bind to this unit (and reset the defaults) only when the binding or the
 * scenario changed, so the user's own targets survive a rebuild. */
static void env_bind(void)
{
    const mmu_unit_t *u;
    int idx = mmu_this_unit();

    if(idx < 0 || idx >= (int)g_mmu.unit_count) idx = 0;
    if(s_unit == idx && s_bound_scenario == g_mmu.scenario_name) return;

    s_unit = idx;
    s_bound_scenario = g_mmu.scenario_name;
    s_total_min = 0;
    s_preset = -1;

    u = &g_mmu.units[idx];
    if(!u->can_dry) return;

    s_target_c = (u->dry_target_c >= DRY_T_MIN) ? u->dry_target_c : 55;
    if(s_target_c > unit_t_max(u)) s_target_c = unit_t_max(u);
    if(s_target_c < DRY_T_MIN) s_target_c = DRY_T_MIN;

    s_hours = 12;
    if(u->dry_state != MMU_DRY_IDLE && u->dry_remaining_min > 0) {
        int h = (u->dry_remaining_min + 59) / 60;

        if(h < DRY_H_MIN) h = DRY_H_MIN;
        if(h > DRY_H_MAX) h = DRY_H_MAX;
        s_hours = h;
    }
}

/* ---------------------------------------------------------------- refresh */

#define BAR_W (UI_HOME_INNER_W - 24)

static void set_text(lv_obj_t *l, const char *t)
{
    if(l) lv_label_set_text(l, t);
}

static void env_refresh(void)
{
    const mmu_unit_t *u;
    char buf[64];
    bool running;
    bool cooling;
    int i;

    if(s_ui.scr == NULL) return;
    if(s_unit < 0 || s_unit >= (int)g_mmu.unit_count) return;
    u = &g_mmu.units[s_unit];

    if(s_ui.humidity) {
        if(u->has_humidity_pct && u->humidity_pct >= 0) snprintf(buf, sizeof buf, "%d", u->humidity_pct);
        else if(u->has_humidity_level && u->humidity_level >= 0)
            snprintf(buf, sizeof buf, "%c", (char)('A' + u->humidity_level));
        else snprintf(buf, sizeof buf, "-");
        set_text(s_ui.humidity, buf);
    }
    if(s_ui.temp) {
        snprintf(buf, sizeof buf, "%d", u->temp_c);
        set_text(s_ui.temp, buf);
    }

    if(!s_ui.has_controls) return;

    running = (u->dry_state == MMU_DRY_HEATING || u->dry_state == MMU_DRY_DEHUMIDIFYING);
    cooling = (u->dry_state == MMU_DRY_COOLING);

    if(s_ui.fill) {
        int pct = 0;

        if(running && u->dry_remaining_min > 0) {
            int total = (s_total_min > 0) ? s_total_min : DRY_TOTAL_DEFAULT;
            int elapsed;

            if(u->dry_remaining_min > total) total = u->dry_remaining_min;
            elapsed = total - u->dry_remaining_min;
            if(elapsed < 0) elapsed = 0;
            pct = (elapsed * 100 + total / 2) / total;
            if(pct < 1) pct = 1;    /* a started cycle always shows a sliver */
        }
        else if(cooling) {
            pct = cooling_pct(u);
        }
        lv_obj_set_width(s_ui.fill, BAR_W * pct / 100);
        lv_obj_set_style_bg_color(s_ui.fill, running ? UI_TOK_HEAT_TEXT : UI_TOK_TEXT_MUTED, 0);
    }

    if(s_ui.temp_val) {
        vs_format(VS_ENV_TARGET_TEMP, buf, sizeof buf, (vs_arg_t[]){ VS_ARG_N("c", s_target_c) }, 1);
        set_text(s_ui.temp_val, buf);
    }
    if(s_ui.hours_val) {
        vs_format(VS_ENV_TARGET_HOURS, buf, sizeof buf, (vs_arg_t[]){ VS_ARG_N("h", s_hours) }, 1);
        set_text(s_ui.hours_val, buf);
    }
    for(i = 0; i < PRESET_COUNT; i++)
        if(s_ui.chips[i] != NULL) ui_kit_chip_set_selected(s_ui.chips[i], i == s_preset);

    if(s_ui.info) {
        vs_format(VS_ENV_RANGE, buf, sizeof buf,
                  (vs_arg_t[]){ VS_ARG_T("name", s_preset >= 0 ? s_presets[s_preset].name : view_str(VS_ENV_CUSTOM)),
                                VS_ARG_N("tmin", DRY_T_MIN), VS_ARG_N("tmax", unit_t_max(u)),
                                VS_ARG_N("hmin", DRY_H_MIN), VS_ARG_N("hmax", DRY_H_MAX) }, 5);
        set_text(s_ui.info, buf);
    }
    if(s_ui.elapsed) {
        if(running && s_total_min > 0 && u->dry_remaining_min >= 0) {
            int elapsed = s_total_min - u->dry_remaining_min;

            if(elapsed < 0) elapsed = 0;
            vs_format(VS_ENV_ELAPSED, buf, sizeof buf, (vs_arg_t[]){ VS_ARG_N("min", elapsed) }, 1);
            set_text(s_ui.elapsed, buf);
        }
        else {
            set_text(s_ui.elapsed, "");
        }
    }
}

/* -------------------------------------------------------------- callbacks */

static void on_tick(lv_timer_t *t)
{
    (void)t;
    if(s_unit < 0 || s_unit >= (int)g_mmu.unit_count) return;
    if(g_mmu.units[s_unit].dry_state != s_last_state) {
        s_last_state = g_mmu.units[s_unit].dry_state;
        ui_nav_refresh_async();
        return;
    }
    env_refresh();
}

static void adjust(int field, int dir)
{
    const mmu_unit_t *u;

    if(s_unit < 0 || s_unit >= (int)g_mmu.unit_count) return;
    u = &g_mmu.units[s_unit];

    if(field == A_TEMP) {
        int tmax = unit_t_max(u);

        s_target_c += dir * 5;
        if(s_target_c < DRY_T_MIN) s_target_c = DRY_T_MIN;
        if(s_target_c > tmax) s_target_c = tmax;
        actions_log("dry %s target %dC", u->name, s_target_c);
    }
    else {
        s_hours += dir;
        if(s_hours < DRY_H_MIN) s_hours = DRY_H_MIN;
        if(s_hours > DRY_H_MAX) s_hours = DRY_H_MAX;
        actions_log("dry %s hours %dh", u->name, s_hours);
    }
    s_preset = preset_match(u, s_target_c, s_hours);
    env_refresh();
}

static void on_temp_minus(lv_event_t *e)  { (void)e; adjust(A_TEMP, -1); }
static void on_temp_plus(lv_event_t *e)   { (void)e; adjust(A_TEMP, +1); }
static void on_hours_minus(lv_event_t *e) { (void)e; adjust(A_HOURS, -1); }
static void on_hours_plus(lv_event_t *e)  { (void)e; adjust(A_HOURS, +1); }

static void on_preset(lv_event_t *e)
{
    const mmu_unit_t *u;
    int i = (int)(intptr_t)lv_event_get_user_data(e);

    if(s_unit < 0 || s_unit >= (int)g_mmu.unit_count) return;
    if(i < 0 || i >= PRESET_COUNT) return;
    u = &g_mmu.units[s_unit];
    if(!dry_available(u)) return;

    s_target_c = preset_target(u, i);
    if(s_target_c < DRY_T_MIN) s_target_c = DRY_T_MIN;
    if(s_target_c > unit_t_max(u)) s_target_c = unit_t_max(u);
    s_hours = s_presets[i].hours;
    if(s_hours < DRY_H_MIN) s_hours = DRY_H_MIN;
    if(s_hours > DRY_H_MAX) s_hours = DRY_H_MAX;
    s_preset = i;

    actions_log("dry %s preset %s %dC %dh",
                u->name, s_presets[i].name, s_target_c, s_hours);
    env_refresh();
}

static void on_scale(lv_event_t *e)
{
    (void)e;
    open_scale();
}

static void do_start(void)
{
    mmu_unit_t *u;

    if(s_unit < 0 || s_unit >= (int)g_mmu.unit_count) return;
    u = &g_mmu.units[s_unit];
    if(!dry_available(u)) return;

    if(s_target_c < DRY_T_MIN) s_target_c = DRY_T_MIN;
    if(s_target_c > unit_t_max(u)) s_target_c = unit_t_max(u);
    if(s_hours < DRY_H_MIN) s_hours = DRY_H_MIN;
    if(s_hours > DRY_H_MAX) s_hours = DRY_H_MAX;

    u->dry_state = MMU_DRY_HEATING;
    u->dry_target_c = (int16_t)s_target_c;
    u->dry_remaining_min = (int16_t)(s_hours * 60);
    s_total_min = s_hours * 60;
    actions_log("dry %s start %dC %dh", u->name, s_target_c, s_hours);
    ui_nav_refresh_async();
}

static void do_stop(void)
{
    mmu_unit_t *u;

    if(s_unit < 0 || s_unit >= (int)g_mmu.unit_count) return;
    u = &g_mmu.units[s_unit];

    u->dry_state = MMU_DRY_COOLING;
    u->dry_remaining_min = 0;
    actions_log("dry %s stop", u->name);
    ui_nav_refresh_async();
}

static void on_start_stop(lv_event_t *e)
{
    (void)e;
    if(s_unit < 0 || s_unit >= (int)g_mmu.unit_count) return;
    if(g_mmu.units[s_unit].dry_state != MMU_DRY_IDLE) {
        do_stop();
        return;
    }
    open_dry_confirm();
}

/* ----------------------------------------------------------------- sheets */

static void on_sheet_start(lv_event_t *e)
{
    (void)e;
    ui_sheet_close(NULL);
    do_start();
}

static void open_dry_confirm(void)
{
    const mmu_unit_t *u;
    lv_obj_t *sheet;
    char head[96];
    char detail[128];

    if(s_unit < 0 || s_unit >= (int)g_mmu.unit_count) return;
    u = &g_mmu.units[s_unit];
    if(!dry_available(u)) return;

    sheet = ui_sheet_open(view_str(VS_ENV_START), 176);
    if(sheet == NULL) return;

    vs_format(VS_ENV_DRY_AT, head, sizeof head,
              (vs_arg_t[]){ VS_ARG_N("c", s_target_c), VS_ARG_N("h", s_hours) }, 2);
    vs_format(VS_ENV_DRY_DETAIL, detail, sizeof detail,
              (vs_arg_t[]){ VS_ARG_T("unit", u->name), VS_ARG_T("variant", mmu_variant_name(u->variant)),
                            VS_ARG_N("tmin", DRY_T_MIN), VS_ARG_N("tmax", unit_t_max(u)),
                            VS_ARG_N("hmin", DRY_H_MIN), VS_ARG_N("hmax", DRY_H_MAX) }, 6);
    ui_sheet_row(sheet, UI_ICON_FIRE, head, detail, NULL, NULL, false);

    ui_sheet_button(sheet, view_str(VS_BTN_START), on_sheet_start, NULL, true);
}

static void open_dry_info(const char *reason)
{
    const mmu_unit_t *u;
    lv_obj_t *sheet;
    char head[96];

    if(s_unit < 0 || s_unit >= (int)g_mmu.unit_count) return;
    u = &g_mmu.units[s_unit];

    sheet = ui_sheet_open(view_str(VS_ENV_CANNOT), 176);
    if(sheet == NULL) return;

    vs_format(VS_ENV_HEAD_UNIT, head, sizeof head,
              (vs_arg_t[]){ VS_ARG_T("unit", u->name), VS_ARG_T("variant", mmu_variant_name(u->variant)) }, 2);
    ui_sheet_row(sheet, reason_icon(reason), head, reason, NULL, NULL, false);
}

/* A plain two-line informational row (no reason-disabled styling, no tap
 * target): the humidity-scale explainer, inset 16 px either side. */
static void scale_info_row(lv_obj_t *sheet, const char *title, const char *detail)
{
    lv_obj_t *body = ui_sheet_body(sheet);
    lv_obj_t *row;
    lv_obj_t *l;

    if(!body) return;

    row = lv_obj_create(body);
    lv_obj_remove_style_all(row);
    lv_obj_set_size(row, UI_SCREEN_W, 48);
    lv_obj_set_scrollable(row, false);
    lv_obj_set_clickable(row, false);

    l = ui_editenv_label(row, title, UI_STYLE_LABEL, 16, 6);
    lv_label_set_long_mode(l, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_set_width(l, UI_SCREEN_W - 32);

    l = ui_editenv_label(row, detail, UI_STYLE_LABEL_DIM, 16, 26);
    lv_label_set_long_mode(l, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_set_width(l, UI_SCREEN_W - 32);
}

static void open_scale(void)
{
    static const view_str_id_t levels[5][2] = {
        { VS_ENV_LEVEL_A, VS_ENV_LEVEL_A_NOTE }, { VS_ENV_LEVEL_B, VS_ENV_LEVEL_B_NOTE },
        { VS_ENV_LEVEL_C, VS_ENV_LEVEL_C_NOTE }, { VS_ENV_LEVEL_D, VS_ENV_LEVEL_D_NOTE },
        { VS_ENV_LEVEL_E, VS_ENV_LEVEL_E_NOTE },
    };
    const mmu_unit_t *u;
    lv_obj_t *sheet;
    int i;

    if(s_unit < 0 || s_unit >= (int)g_mmu.unit_count) return;
    u = &g_mmu.units[s_unit];

    sheet = ui_sheet_open(view_str(VS_ENV_SCALE_TITLE), 192);
    if(sheet == NULL) return;

    if(u->has_humidity_level && !u->has_humidity_pct) {
        for(i = 0; i < 5; i++)
            scale_info_row(sheet, view_str(levels[i][0]), view_str(levels[i][1]));
    }
    else {
        scale_info_row(sheet, view_str(VS_ENV_BELOW), view_str(VS_ENV_BELOW_NOTE));
        scale_info_row(sheet, view_str(VS_ENV_ABOVE), view_str(VS_ENV_ABOVE_NOTE));
    }
}

/* -------------------------------------------------------------- the screen */

static lv_obj_t *card(lv_obj_t *scr, int x, int y, int w, int h)
{
    lv_obj_t *c = lv_obj_create(scr);

    lv_obj_remove_style_all(c);
    lv_obj_set_pos(c, x, y);
    lv_obj_set_size(c, w, h);
    lv_obj_set_scrollable(c, false);
    lv_obj_set_style_radius(c, UI_TOK_RADIUS_TILE, 0);
    lv_obj_set_style_bg_color(c, UI_TOK_SURFACE_1, 0);
    lv_obj_set_style_bg_opa(c, LV_OPA_COVER, 0);
    return c;
}

static lv_color_t dryer_color(view_dryer_tone_t t)
{
    switch(t) {
    case VIEW_DRYER_HEAT:
    case VIEW_DRYER_HOLD:  return UI_TOK_HEAT_TEXT;
    case VIEW_DRYER_FAULT: return UI_TOK_ERROR;
    default:               return UI_TOK_TEXT_MUTED;
    }
}

/* One big number with its unit under it, in the readout card. */
static lv_obj_t *readout(lv_obj_t *c, int x, const char *unit)
{
    lv_obj_t *v = ui_home_label(c, "-", UI_HOME_FONT_TITLE, UI_TOK_TEXT, x, 6);

    lv_obj_set_width(v, 56);
    ui_home_label(c, unit, UI_HOME_FONT_META, UI_TOK_TEXT_MUTED, x, 6 + 26);
    return v;
}

/** The readout card, the same at every scenario: the humidity, the
 * temperature, the "?" for the humidity scale and (when @p show_status) the
 * dryer's state from the view, its time left, and the progress bar. */
static void build_readout(lv_obj_t *scr, const mmu_unit_t *u, bool show_status)
{
    lv_obj_t *c = card(scr, EN_PAD, EN_CARD_Y, UI_HOME_INNER_W, EN_CARD_H);
    bool has_hum = (u->has_humidity_pct && u->humidity_pct >= 0) ||
                   (u->has_humidity_level && u->humidity_level >= 0);

    s_ui.humidity = readout(c, 12, view_str((u->has_humidity_level && !u->has_humidity_pct) ? VS_ENV_LEVEL : VS_ENV_RH));
    if(u->has_temp && u->temp_c > -100) s_ui.temp = readout(c, 84, view_str(VS_ENV_DEGC));
    if(has_hum) {
        lv_obj_t *q = ui_kit_chip(c, "?", 150, 6, 30, false, on_scale, NULL);

        (void)q;
    }

    if(show_status) {
        view_unit_t vu;

        if(view_unit(&g_mmu, s_unit, &vu) && vu.dryer.present) {
            ui_home_label_fit(c, vu.dryer.text, UI_HOME_FONT_SECTION, dryer_color(vu.dryer.tone),
                              200, 6, UI_HOME_INNER_W - 200 - 12);
        }

        {
            lv_obj_t *track = lv_obj_create(c);

            lv_obj_remove_style_all(track);
            lv_obj_set_pos(track, 12, 60);
            lv_obj_set_size(track, BAR_W, 4);
            lv_obj_set_scrollable(track, false);
            lv_obj_set_style_radius(track, 2, 0);
            lv_obj_set_style_bg_color(track, UI_TOK_SURFACE_3, 0);
            lv_obj_set_style_bg_opa(track, LV_OPA_COVER, 0);
            s_ui.fill = lv_obj_create(track);
            lv_obj_remove_style_all(s_ui.fill);
            lv_obj_set_pos(s_ui.fill, 0, 0);
            lv_obj_set_size(s_ui.fill, 0, 4);
            lv_obj_set_scrollable(s_ui.fill, false);
            lv_obj_set_style_radius(s_ui.fill, 2, 0);
            lv_obj_set_style_bg_color(s_ui.fill, UI_TOK_HEAT_TEXT, 0);
            lv_obj_set_style_bg_opa(s_ui.fill, LV_OPA_COVER, 0);
        }
    }
}

static void build_drying(lv_obj_t *scr, const mmu_unit_t *u)
{
    int i;
    lv_obj_t *c;

    s_ui.has_controls = true;
    build_readout(scr, u, true);

    for(i = 0; i < PRESET_COUNT; i++)
        s_ui.chips[i] = ui_kit_chip(scr, s_presets[i].name, EN_PAD + i * (EN_CHIP_W + EN_CHIP_GAP),
                                    EN_CHIP_Y, EN_CHIP_W, i == s_preset, on_preset,
                                    (void *)(intptr_t)i);

    c = card(scr, EN_PAD, EN_STEP_Y, EN_STEP_W, EN_STEP_H);
    ui_home_label(c, view_str(VS_ENV_TEMPERATURE), UI_HOME_FONT_META, UI_TOK_TEXT_MUTED, 12, 6);
    s_ui.temp_val = ui_kit_stepper(c, 12, 24, EN_STEP_W - 24, "-", on_temp_minus, on_temp_plus);

    c = card(scr, EN_PAD + UI_HOME_INNER_W - EN_STEP_W, EN_STEP_Y, EN_STEP_W, EN_STEP_H);
    ui_home_label(c, view_str(VS_ENV_TIME), UI_HOME_FONT_META, UI_TOK_TEXT_MUTED, 12, 6);
    s_ui.hours_val = ui_kit_stepper(c, 12, 24, EN_STEP_W - 24, "-", on_hours_minus, on_hours_plus);

    s_ui.info = ui_home_label(scr, "", UI_HOME_FONT_META, UI_TOK_TEXT_FAINT, EN_PAD, EN_INFO_Y);
    lv_obj_set_width(s_ui.info, 300);
    s_ui.elapsed = ui_home_label(scr, "", UI_HOME_FONT_META, UI_TOK_TEXT_MUTED,
                                 EN_PAD + UI_HOME_INNER_W - 140, EN_INFO_Y);
    lv_obj_set_width(s_ui.elapsed, 140);
    lv_obj_set_style_text_align(s_ui.elapsed, LV_TEXT_ALIGN_RIGHT, 0);

    {
        bool active = (u->dry_state == MMU_DRY_HEATING || u->dry_state == MMU_DRY_DEHUMIDIFYING);
        bool cooling = (u->dry_state == MMU_DRY_COOLING);

        /* Cooling is automatic, not a running cycle the user can stop again;
         * Start stays disabled until the unit settles back to idle. */
        ui_kit_button(scr, view_str(active ? VS_ENV_STOP : VS_ENV_START),
                      active ? UI_KIT_BTN_NORMAL : UI_KIT_BTN_PRIMARY, EN_PAD, EN_BTN_Y,
                      UI_HOME_INNER_W, EN_BTN_H, active || !(cooling || !u->power_adapter),
                      on_start_stop, NULL);
    }
}

/** Owns only its own screen's timer and its own screen's s_ui: with the new
 * screen already built and loaded by the time this runs (auto_del in
 * ui_nav_goto deletes the old screen after the new one is live), @p e's
 * target is the OLD screen - deleting a global/current timer or s_ui here
 * would tear down the new screen's, not this one's. */
static void on_screen_delete(lv_event_t *e)
{
    lv_obj_t *scr = lv_event_get_target_obj(e);
    lv_timer_t *timer = (lv_timer_t *)lv_obj_get_user_data(scr);

    if(timer) lv_timer_delete(timer);
    if(scr == s_ui.scr) memset(&s_ui, 0, sizeof s_ui);
}

static lv_obj_t *build(void)
{
    lv_obj_t *scr;
    lv_timer_t *timer;
    const mmu_unit_t *u;

    scr = ui_kit_screen();

    memset(&s_ui, 0, sizeof s_ui);
    s_ui.scr = scr;
    lv_obj_add_event_cb(scr, on_screen_delete, LV_EVENT_DELETE, NULL);

    env_bind();
    u = &g_mmu.units[s_unit];

    (void)ui_kit_header(scr, view_str(VS_SCREEN_ENV), true);

    if(dry_available(u)) build_drying(scr, u);
    else                 build_readout(scr, u, false);

    s_last_state = u->dry_state;
    timer = lv_timer_create(on_tick, 1000, NULL);
    lv_obj_set_user_data(scr, timer);
    env_refresh();
    return scr;
}

static int modal_count(void)
{
    return 2;   /* 0 = start confirmation / cannot-dry reason, 1 = scale */
}

static void open_modal(int index)
{
    if(s_unit < 0 || s_unit >= (int)g_mmu.unit_count) env_bind();

    if(index == 1) {
        open_scale();
        return;
    }

    if(!dry_available(&g_mmu.units[s_unit])) {
        const char *reason = actions_dry_reason(&g_mmu.units[s_unit]);

        open_dry_info(reason[0] ? reason : view_str(VS_ENV_CANNOT_REASON));
        return;
    }
    open_dry_confirm();
}

const ui_screen_ops_t screen_environment_ops = { build, modal_count, open_modal, NULL };

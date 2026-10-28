/**
 * @file screen_load.c
 * Load / unload progress, in the design language (ui_kit): the screen home's
 * message row opens. The steps are the view's activity (the toolhead's stepper
 * data), the headline is the core's own message while a run is on, and the
 * filament line comes from the loaded bay's tile.
 *
 * Only the bound unit is shown. A load on this unit takes over the screen:
 * headline, filament line, the step list and the buttons (Stop; Retry and Stop
 * after a failed step; Done once it finished). A load or unload on another
 * unit is read-only here and says so. Nothing running is an empty state.
 */
#include "ui/ui_screens.h"

#include "app/actions.h"
#include "model/mmu_model.h"
#include "ui/ui_icons.h"
#include "ui/ui_kit.h"
#include "ui/ui_nav.h"
#include "ui/ui_print_common.h"
#include "ui/ui_update.h"
#include "view/strings.h"
#include "view/view.h"

#include <stdio.h>
#include <string.h>

#define STEP_ROW_H  UI_PX(34)  /* one step: a circle, its label, a rail to the next */
#define STEP_FIRST  UI_PX(94)  /* first row y */
#define STEP_VIEW_H UI_PX(170) /* five rows, the scrolling viewport when there are more */
#define STEP_DOT    UI_PX(18)  /* the circle's diameter */
#define BTN_Y       UI_PX(268)
#define BTN_H       UI_PX(40)

/* How the screen reads this time it is built. */
enum {
    LP_IDLE,       /* nothing is loading */
    LP_STARTING,   /* we sent a load or an unload; the host has not said yet */
    LP_REFUSED,    /* the host said no, and said why */
    LP_BUSY,       /* this unit is loading / unloading */
    LP_FAILED,     /* this unit's run has a failed step */
    LP_DONE,       /* this unit's run finished */
    LP_STOPPED,    /* the user stopped it */
    LP_CONFIRM,    /* external spool: claim whether filament came out */
    LP_ELSEWHERE,  /* another unit is loading */
};

/* How one step of a run stands: done (a tick in its dot), the one being run
 * (an empty dot with an accent ring), failed (red, and its label red too), or
 * still to come. */
typedef enum { ST_DONE, ST_ACTIVE, ST_FAILED, ST_PENDING } step_state_t;

/* How long a sent load or an unload may go unreported before the screen stops
 * waiting and says so plainly. Long enough for a printer to pick the request
 * up, short enough that a request the host never got does not look alive. */
#define LP_STARTING_MS 10000

static lv_obj_t *s_scr;
static lv_timer_t *s_timer;
static int s_sig = -1;

/* The run being shown: a slot of this unit, or the external spool. */
static int  s_ctx_unit;
static int  s_ctx_slot;
static int  s_ctx_ext;
static bool s_ctx_unload;

static bool s_ran_this;   /* this session ran a load/unload on this unit */
static bool s_ext_op;     /* ... or on the external spool */
static bool s_stopped;
static bool s_confirmed;
static int  s_stop_at;

/* The pending load or unload this screen is waiting on: the model's own seq
 * (so a new one is a new wait) and the tick it was first seen at, which is
 * the deadline. Only the display has a clock, so the wait lives here and the
 * model only says that something is pending. */
static uint8_t s_starting_seq;
static uint32_t s_starting_at;
static uint8_t s_refusal_seq;   /* the refusal this screen is showing */
static bool s_refusal_unload;

/* What the screen on screen is made of, so a run that is still going can be
 * followed without building it again. Only the objects and the shape are kept:
 * the view's strings are buffers this screen filled a moment ago, so a
 * comparison has to be against what the object itself holds. */
typedef struct {
    lv_obj_t *dot;
    lv_obj_t *tick;              /* the tick in the dot, hidden unless it is done */
    lv_obj_t *label;
    lv_obj_t *rail;              /* NULL on the last row */
    step_state_t state;
} step_parts_t;

static struct {
    lv_obj_t *scr;
    lv_obj_t *hdr;
    lv_obj_t *alert;             /* the header's slot, NULL when it said nothing */
    lv_obj_t *swatch;            /* the filament line, NULL when there is none */
    lv_obj_t *line;
    lv_obj_t *failed;            /* the failure's own line, NULL unless failed */
    lv_obj_t *body;              /* where the rows sit: the screen, or the scroller */
    step_parts_t row[MMU_MAX_STEPS];
    int rows;
    int phase;                   /* the phase this screen was built for */
    bool bound;                  /* a run on this unit, a shape update() can keep */
} s_shown;

static lv_obj_t *child_at(lv_obj_t *parent, int i)
{
    return (parent && i >= 0 && (uint32_t)i < lv_obj_get_child_count(parent))
               ? lv_obj_get_child(parent, (uint32_t)i) : NULL;
}

/** A label, and not merely whatever sits at that index: the rows below are
 * bound by position, so a row that does not have the shape step_row() gives it
 * is a screen this update() must not write to. */
static bool is_label(lv_obj_t *o)
{
    return o && lv_obj_check_type(o, &lv_label_class);
}

/* ------------------------------------------------------------------ pieces */

static lv_obj_t *plain(lv_obj_t *parent, int x, int y, int w, int h, bool scroll)
{
    lv_obj_t *o = lv_obj_create(parent);

    lv_obj_remove_style_all(o);
    lv_obj_set_pos(o, x, y);
    lv_obj_set_size(o, w, h);
    lv_obj_set_style_pad_all(o, 0, 0);
    if(!scroll) lv_obj_set_scrollable(o, false);
    else        lv_obj_set_scrollbar_mode(o, LV_SCROLLBAR_MODE_OFF);
    return o;
}


/* ------------------------------------------------------------------- steps */

/* One step at local y @p y: the circle (a check when done, filled when it is
 * the current one, red when it failed), its label, and the rail down to the
 * next circle in the color of the step it leaves. */
static void step_row(lv_obj_t *parent, int y, const char *text, step_state_t st, bool last)
{
    int cx = UI_HOME_HEADER_PAD;
    lv_obj_t *dot = plain(parent, cx, y + (STEP_ROW_H - STEP_DOT) / 2, STEP_DOT, STEP_DOT, false);
    lv_color_t label = (st == ST_PENDING) ? UI_TOK_TEXT_FAINT : UI_TOK_TEXT;
    lv_obj_t *tick;

    lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);
    /* Every dot carries its tick from the start, shown only once the step is
     * done: a step that finishes is a value to write, not an object to build,
     * which is what lets a run be followed in place. */
    tick = ui_home_label(dot, UI_ICON_CHECK, UI_HOME_FONT_ICON, UI_TOK_BG,
                         (STEP_DOT - ui_home_text_w(UI_ICON_CHECK, UI_HOME_FONT_ICON)) / 2 - UI_PX(1),
                         (STEP_DOT - lv_font_get_line_height(UI_HOME_FONT_ICON)) / 2 - UI_PX(1));
    switch(st) {
    case ST_DONE:
        lv_obj_set_style_bg_color(dot, UI_TOK_ACCENT, 0);
        lv_obj_set_style_bg_opa(dot, LV_OPA_COVER, 0);
        break;
    case ST_ACTIVE:
        lv_obj_set_style_bg_opa(dot, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(dot, UI_PX(3), 0);
        lv_obj_set_style_border_color(dot, UI_TOK_ACCENT, 0);
        break;
    case ST_FAILED:
        lv_obj_set_style_bg_color(dot, UI_TOK_ERROR, 0);
        lv_obj_set_style_bg_opa(dot, LV_OPA_COVER, 0);
        label = UI_TOK_ERROR;
        break;
    default:
        lv_obj_set_style_bg_opa(dot, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(dot, UI_PX(2), 0);
        lv_obj_set_style_border_color(dot, UI_TOK_TEXT_FAINT, 0);
        break;
    }
    if(st != ST_DONE) lv_obj_set_hidden(tick, true);

    ui_home_label_fit(parent, text, UI_HOME_FONT_SECTION, label, cx + STEP_DOT + UI_PX(12),
                      y + (STEP_ROW_H - lv_font_get_line_height(UI_HOME_FONT_SECTION)) / 2,
                      UI_HOME_W - 2 * UI_HOME_HEADER_PAD - STEP_DOT - UI_PX(12));

    if(!last) {
        lv_obj_t *rail = plain(parent, cx + (STEP_DOT - UI_PX(2)) / 2,
                               y + (STEP_ROW_H + STEP_DOT) / 2 + UI_PX(2), UI_PX(2),
                               STEP_ROW_H - STEP_DOT - UI_PX(4), false);

        lv_obj_set_style_bg_color(rail, st == ST_DONE ? UI_TOK_ACCENT : UI_TOK_SURFACE_1, 0);
        lv_obj_set_style_bg_opa(rail, LV_OPA_COVER, 0);
    }
}

/* Rows into @p parent, first row at local y @p base, from the view's activity. */
static void build_rows(lv_obj_t *parent, int base, int phase, const view_activity_t *a)
{
    int n = a->step_count;
    int i;

    s_shown.body = parent;
    s_shown.rows = 0;
    for(i = 0; i < n && i < MMU_MAX_STEPS; i++) {
        step_state_t st;
        step_parts_t *r = &s_shown.row[i];
        int c0 = (int)lv_obj_get_child_count(parent);
        int c1;

        if(phase == LP_DONE || phase == LP_CONFIRM) st = ST_DONE;
        else if(phase == LP_STOPPED) st = (i < s_stop_at) ? ST_DONE : ST_PENDING;
        else if(i == a->failed) st = ST_FAILED;
        else if(i == a->index) st = ST_ACTIVE;
        else if(a->index >= 0 && i < a->index) st = ST_DONE;
        else st = ST_PENDING;

        step_row(parent, base + i * STEP_ROW_H, a->steps[i], st, i == n - 1);
        c1 = (int)lv_obj_get_child_count(parent);
        /* The row is a dot, its label and (unless it is the last) a rail, in
         * that order; the tick is inside the dot. */
        r->dot = child_at(parent, c0);
        r->tick = is_label(child_at(r->dot, 0)) ? lv_obj_get_child(r->dot, 0) : NULL;
        r->label = child_at(parent, c0 + 1);
        r->rail = (c1 - c0 == 3) ? child_at(parent, c0 + 2) : NULL;
        r->state = st;
        if(!is_label(r->label) || !r->tick || (c1 - c0) < 2 || (c1 - c0) > 3) {
            s_shown.bound = false;
            return;
        }
        s_shown.rows = i + 1;
    }
}

/* The filament line, as text and as the color of the swatch beside it: the
 * run's bay (its tile in the view), or the external spool's own material. False
 * when there is nothing to say, which is the one case that needs a rebuild. */
static bool filament_text(uint32_t *rgb, bool *known, char *line, size_t n)
{
    *rgb = 0;
    *known = false;
    line[0] = '\0';

    if(s_ctx_ext >= 0 && s_ctx_ext < (int)g_mmu.ext_spool_count) {
        const mmu_ext_spool_t *e = &g_mmu.ext_spools[s_ctx_ext];

        *rgb = e->color;
        *known = true;
        vs_format(VS_LOAD_EXT_LINE, line, n,
                  (vs_arg_t[]){ VS_ARG_T("material", e->material[0] ? e->material : view_str(VS_MATERIAL_UNKNOWN)) }, 1);
    }
    else if(s_ctx_unit >= 0 && s_ctx_slot >= 0) {
        view_tile_t t;

        if(view_tile(&g_mmu, s_ctx_unit, s_ctx_slot, &t)) {
            *rgb = t.color;
            *known = !t.dim && t.color_known;
            snprintf(line, n, "%s \xC2\xB7 %s", t.label, t.sublabel);
            if(t.material[0])
                snprintf(line + strlen(line), n - strlen(line), " \xC2\xB7 %s", t.material);
            if(t.brand[0])
                snprintf(line + strlen(line), n - strlen(line), " \xC2\xB7 %s", t.brand);
        }
    }
    return line[0] != '\0';
}

/* The same line, on the objects build() left for it. */
static void draw_filament_line(lv_obj_t *scr, int y, const char *line, uint32_t rgb, bool known)
{
    lv_obj_t *sw;

    sw = plain(scr, UI_HOME_HEADER_PAD, y + UI_PX(3), UI_PX(12), UI_PX(12), false);
    lv_obj_set_style_radius(sw, UI_PX(3), 0);
    lv_obj_set_style_bg_color(sw, known ? lv_color_hex(rgb) : UI_TOK_SURFACE_1, 0);
    lv_obj_set_style_bg_opa(sw, LV_OPA_COVER, 0);
    s_shown.swatch = sw;
    s_shown.line = ui_home_label_fit(scr, line, UI_HOME_FONT_BODY, UI_TOK_TEXT_MUTED,
                                     UI_HOME_HEADER_PAD + UI_PX(20), y, UI_HOME_INNER_W - UI_PX(20));
}

static const char *title_text(int phase, const view_toolhead_t *th)
{
    if(phase == LP_STOPPED) return view_str(VS_LOAD_STOPPED);
    if(phase == LP_DONE)    return view_str(s_ctx_unload ? VS_LOAD_UNLOADED : VS_LOAD_LOADED);
    if(phase == LP_CONFIRM) return view_str(VS_LOAD_EXTERNAL);
    if(phase == LP_FAILED)  return view_str(s_ctx_unload ? VS_UNLOAD_FAILED : VS_LOAD_FAILED);
    return th->message.text[0] ? th->message.text : view_str(VS_SCREEN_LOAD);
}

/* --------------------------------------------------------------- callbacks */

static void on_stop(lv_event_t *e)
{
    (void)e;
    s_stop_at = (g_mmu.step_current >= 0) ? (int)g_mmu.step_current : 0;
    actions_log("stop");
    mmu_model_stop_busy();
    s_stopped = true;
}

static void on_back_home(lv_event_t *e)
{
    (void)e;
    actions_log("back home");
    ui_nav_back_async();
}

static void on_confirm(lv_event_t *e)
{
    (void)e;
    actions_log("filament extruded, continue");
    mmu_model_confirm_ext_load();
    s_confirmed = true;
}

static void on_retry_ext(lv_event_t *e)
{
    (void)e;
    actions_log("not extruded yet, retry");
    mmu_model_retry_ext_load();
    s_confirmed = false;
}

static void on_retry_failed(lv_event_t *e)
{
    (void)e;
    actions_log("retry (problem solved)");
    mmu_model_clear_error();
    s_stopped = false;
}

static void buttons(int phase)
{
    int w = UI_HOME_INNER_W;
    int half = (w - UI_PX(8)) / 2;

    if(phase == LP_FAILED) {
        ui_kit_button(s_scr, view_str(VS_ACTION_RETRY_LOAD), UI_KIT_BTN_PRIMARY, UI_HOME_HEADER_PAD, BTN_Y, half, BTN_H,
                      true, on_retry_failed, NULL);
        ui_kit_button(s_scr, view_str(VS_ACTION_STOP), UI_KIT_BTN_NORMAL, UI_HOME_HEADER_PAD + half + UI_PX(8), BTN_Y, half,
                      BTN_H, true, on_stop, NULL);
    }
    else if(phase == LP_BUSY) {
        ui_kit_button(s_scr, view_str(VS_ACTION_STOP), UI_KIT_BTN_NORMAL, UI_HOME_HEADER_PAD, BTN_Y, w, BTN_H, true,
                      on_stop, NULL);
    }
    else if(phase == LP_CONFIRM) {
        ui_kit_button(s_scr, view_str(VS_LOAD_EXTRUDED), UI_KIT_BTN_PRIMARY, UI_HOME_HEADER_PAD, BTN_Y,
                      half, BTN_H, true, on_confirm, NULL);
        ui_kit_button(s_scr, view_str(VS_LOAD_NOT_YET), UI_KIT_BTN_NORMAL, UI_HOME_HEADER_PAD + half + UI_PX(8),
                      BTN_Y, half, BTN_H, true, on_retry_ext, NULL);
    }
    else if(phase != LP_IDLE && phase != LP_ELSEWHERE) {
        ui_kit_button(s_scr, view_str(VS_BTN_DONE), UI_KIT_BTN_PRIMARY, UI_HOME_HEADER_PAD, BTN_Y, w, BTN_H, true,
                      on_back_home, NULL);
    }
}

/* ------------------------------------------------------------- this unit */

static void build_this(int phase, const view_toolhead_t *th)
{
    lv_obj_t *body;
    int base;
    int n = th->activity.step_count;
    uint32_t rgb = 0;
    bool known = false;
    char line[96];

    s_shown.hdr = ui_kit_header(s_scr, title_text(phase, th), true);
    s_shown.alert = child_at(s_shown.hdr, 2);
    if(filament_text(&rgb, &known, line, sizeof line))
        draw_filament_line(s_scr, UI_HOME_HEADER_H + UI_PX(4), line, rgb, known);
    /* A failed run says what the host reported, in its tone, under the filament. */
    if(phase == LP_FAILED && th->message.text[0])
        s_shown.failed = ui_home_label_fit(s_scr, th->message.text, UI_HOME_FONT_BODY,
                                            ui_home_tone_color(th->message.tone), UI_HOME_HEADER_PAD,
                                            UI_HOME_HEADER_H + UI_PX(4) + UI_PX(22), UI_HOME_INNER_W);

    /* Five rows fit above the buttons; a longer run scrolls inside that zone
     * instead of pushing the buttons down. */
    if(n > 5) {
        body = plain(s_scr, 0, STEP_FIRST, UI_HOME_W, STEP_VIEW_H, true);
        base = 0;
    }
    else {
        body = s_scr;
        base = STEP_FIRST;
    }
    s_shown.bound = true;
    build_rows(body, base, phase, &th->activity);
    buttons(phase);
    s_shown.phase = phase;
    /* Only a run on this unit is a shape update() knows how to keep; the
     * finished and the external-spool variants have nothing moving in them.
     * build_rows() clears this again if a row did not come out the way it
     * expects to find it. */
    s_shown.bound = (phase == LP_BUSY || phase == LP_FAILED);
}

static void build_elsewhere(const char *why, const view_toolhead_t *th)
{
    lv_obj_t *body;

    ui_kit_header(s_scr, view_str(VS_SCREEN_LOAD), true);
    ui_kit_text(s_scr, why, UI_HOME_FONT_BODY, UI_TOK_TEXT, UI_HOME_HEADER_PAD,
                UI_HOME_HEADER_H + UI_PX(4), UI_HOME_INNER_W);
    body = plain(s_scr, 0, STEP_FIRST, UI_HOME_W, STEP_VIEW_H, true);
    build_rows(body, 0, LP_BUSY, &th->activity);
}

static void build_idle(void)
{
    uint32_t rgb = 0;
    bool known = false;
    char line[96];
    int y = UI_HOME_HEADER_H + UI_PX(20) + UI_PX(30);

    /* Waiting on a load or an unload the host has not reported, and the wait
     * is over: say that plainly instead of "Nothing is loading", which would
     * read as if nothing had ever been asked for. */
    if(s_starting_seq && lv_tick_elaps(s_starting_at) >= LP_STARTING_MS) {
        ui_kit_header(s_scr, view_str(VS_SCREEN_LOAD), true);
        ui_home_label_fit(s_scr, view_str(VS_LOAD_NO_REPORT), UI_HOME_FONT_SECTION, UI_TOK_TEXT_MUTED,
                          UI_HOME_HEADER_PAD, UI_HOME_HEADER_H + UI_PX(20), UI_HOME_INNER_W);
        buttons(LP_DONE);
        return;
    }

    ui_kit_header(s_scr, view_str(VS_SCREEN_LOAD), true);
    ui_home_label(s_scr, UI_ICON_TRAY_ARROW_DOWN, UI_HOME_FONT_ICON, UI_TOK_TEXT_FAINT,
                  UI_HOME_HEADER_PAD, UI_HOME_HEADER_H + UI_PX(20));
    ui_home_label(s_scr, view_str(VS_LOAD_NOTHING), UI_HOME_FONT_SECTION, UI_TOK_TEXT_MUTED,
                  UI_HOME_HEADER_PAD + UI_PX(28), UI_HOME_HEADER_H + UI_PX(20));

    /* At rest the screen also says what is in the toolhead now, and the next
     * step (PRINCIPLES.md 2). The context is the loaded filament; the next run
     * captures its own target. */
    s_ctx_unit = g_mmu.loaded ? g_mmu.loaded_unit : -1;
    s_ctx_slot = g_mmu.loaded ? g_mmu.loaded_slot : -1;
    s_ctx_ext = (g_mmu.loaded && g_mmu.loaded_unit < 0) ? g_mmu.loaded_ext : -1;
    if(filament_text(&rgb, &known, line, sizeof line)) {
        draw_filament_line(s_scr, y, line, rgb, known);
        y += UI_PX(26);
    }
    ui_home_label_fit(s_scr, view_str(VS_LOAD_IDLE_NEXT), UI_HOME_FONT_BODY, UI_TOK_TEXT_MUTED,
                      UI_HOME_HEADER_PAD, y, UI_HOME_INNER_W);
}

/* A load or an unload is on its way: the host has not reported the operation
 * yet, so this says so rather than claiming nothing is loading. */
static void build_starting(void)
{
    ui_kit_header(s_scr, view_str(VS_SCREEN_LOAD), true);
    ui_home_label(s_scr, UI_ICON_TRAY_ARROW_DOWN, UI_HOME_FONT_ICON, UI_TOK_TEXT_FAINT,
                  UI_HOME_HEADER_PAD, UI_HOME_HEADER_H + UI_PX(20));
    ui_home_label(s_scr, view_str(VS_LOAD_STARTING), UI_HOME_FONT_SECTION, UI_TOK_TEXT_MUTED,
                  UI_HOME_HEADER_PAD + UI_PX(28), UI_HOME_HEADER_H + UI_PX(20));
    ui_home_label_fit(s_scr, view_str(VS_LOAD_STARTING_HINT), UI_HOME_FONT_BODY, UI_TOK_TEXT_MUTED,
                      UI_HOME_HEADER_PAD, UI_HOME_HEADER_H + UI_PX(20) + UI_PX(26), UI_HOME_INNER_W);
}

/* The host refused the load or the unload, in its own words, after the
 * display's lead - the one answer that must not be hidden, because it is the
 * reason the filament did not move. */
static void build_refused(void)
{
    char text[320];
    const char *lead = s_refusal_unload ? view_str(VS_UNLOAD_REFUSED) : view_str(VS_LOAD_REFUSED);

    ui_kit_header(s_scr, view_str(VS_SCREEN_LOAD), true);
    snprintf(text, sizeof text, "%s %s", lead, g_mmu.action_refusal);
    /* Wrapped, never cut to one line: the host's words are the reason, and a
     * refusal such as "Heat the extruder before an OpenAMS toolchange..." is
     * longer than one line of this screen. */
    ui_kit_text(s_scr, text, UI_HOME_FONT_BODY, UI_TOK_TEXT, UI_HOME_HEADER_PAD,
                UI_HOME_HEADER_H + UI_PX(20), UI_HOME_INNER_W);
    buttons(LP_REFUSED);
}

/* --------------------------------------------------------------- populate */

static int signature_of(void)
{
    /* what is loaded now is on the idle screen, so a change of it is a change
     * of the screen; unsigned, so the mix may wrap */
    unsigned loaded = g_mmu.loaded
                    ? (unsigned)((g_mmu.loaded_unit + 2) * 64 + g_mmu.loaded_slot + 1) : 0u;

    return (int)((unsigned)((int)g_mmu.busy * 1000000
         + ((int)g_mmu.busy_unit + 1) * 100000
         + ((int)g_mmu.busy_slot + 1) * 10000
         + ((int)g_mmu.step_current + 1) * 1000
         + ((int)g_mmu.step_failed + 1) * 100
         + (int)g_mmu.step_count * 10
         + ((int)g_mmu.loaded_ext + 1)
         + (int)g_mmu.action_seq * 100
         + (int)g_mmu.action_pending_seq) + loaded * 2654435761u);
}

/* Whether a stage plan is running with the lane not busy yet: the heat, home
 * and cut steps come before (and between) the AMS's own moves, and Stop must
 * be there for all of them (UNIFIED_UI 4g). Same rule as the stop action's. */
static bool plan_running(void)
{
    return g_mmu.step_count > 0 && g_mmu.step_current >= 0 &&
           g_mmu.step_current < (int)g_mmu.step_count && g_mmu.step_failed < 0;
}

static bool s_plan_was;

static void capture_target(void)
{
    s_ctx_unload = (g_mmu.busy == MMU_BUSY_UNLOAD);

    if(g_mmu.busy_unit < 0) {
        int t = mmu_model_ext_target();

        s_ctx_ext = (t >= 0) ? t : 0;
        s_ctx_unit = -1;
        s_ctx_slot = -1;
        s_ext_op = true;
        return;
    }
    s_ctx_unit = g_mmu.busy_unit;
    s_ctx_slot = g_mmu.busy_slot;
    s_ctx_ext = -1;
    s_ran_this = true;
}

static void populate(void)
{
    char why[64];
    view_toolhead_t th;
    int unit = mmu_this_unit();
    int thi = (unit >= 0 && unit < (int)g_mmu.unit_count) ? g_mmu.units[unit].toolhead : -1;
    bool pending = g_mmu.action_pending_kind != MMU_ACTION_OTHER;
    bool plan;

    memset(&th, 0, sizeof th);
    if(thi >= 0) view_toolhead(&g_mmu, thi, &th);
    /* Every object the last build owned is about to be deleted. */
    memset(&s_shown, 0, sizeof s_shown);
    s_shown.scr = s_scr;
    lv_obj_clean(s_scr);

    /* A refusal of a load or an unload is the one answer this screen exists to
     * pass on: the host's words, once, until the next one. */
    if(g_mmu.action_refusal[0] &&
       (g_mmu.action_refusal_kind == MMU_ACTION_LOAD ||
        g_mmu.action_refusal_kind == MMU_ACTION_UNLOAD)) {
        s_refusal_seq = g_mmu.action_seq;
        s_refusal_unload = (g_mmu.action_refusal_kind == MMU_ACTION_UNLOAD);
        build_refused();
        s_sig = signature_of();
        return;
    }
    s_refusal_seq = 0;

    /* The host reports the operation by its own status; until it does, the
     * screen waits, and the wait is timed here (the model has no clock). */
    if(pending && g_mmu.busy == MMU_BUSY_NONE) {
        if(g_mmu.action_pending_seq != s_starting_seq) {
            s_starting_seq = g_mmu.action_pending_seq;
            s_starting_at = lv_tick_get();
        }
        if(lv_tick_elaps(s_starting_at) < LP_STARTING_MS) {
            build_starting();
            s_sig = signature_of();
            return;
        }
    }
    else {
        s_starting_seq = 0;
    }

    /* a plan that starts is a new sequence: an earlier Stop is over */
    plan = plan_running();
    if(plan && !s_plan_was) s_stopped = false;
    s_plan_was = plan;

    if(g_mmu.busy != MMU_BUSY_NONE) {
        s_stopped = false;
        if(g_mmu.busy_unit < 0) {
            capture_target();
            build_this(LP_BUSY, &th);
        }
        else if(mmu_busy_elsewhere(why, sizeof(why))) {
            build_elsewhere(why, &th);
        }
        else {
            capture_target();
            build_this(g_mmu.step_failed >= 0 ? LP_FAILED : LP_BUSY, &th);
        }
    }
    else if(plan && !s_stopped) {
        /* a heat or home step: the sequence runs, the lane is not busy yet */
        s_ctx_unload = g_mmu.loaded;
        s_ctx_unit = g_mmu.loaded ? g_mmu.loaded_unit : -1;
        s_ctx_slot = g_mmu.loaded ? g_mmu.loaded_slot : -1;
        s_ctx_ext = (g_mmu.loaded && g_mmu.loaded_unit < 0) ? g_mmu.loaded_ext : -1;
        build_this(LP_BUSY, &th);
    }
    else if(s_stopped) {
        build_this(LP_STOPPED, &th);
    }
    else if(s_ran_this) {
        build_this(LP_DONE, &th);
    }
    else if(s_ext_op) {
        build_this(s_confirmed ? LP_DONE : LP_CONFIRM, &th);
    }
    else if(g_mmu.step_count > 0 &&
            g_mmu.step_failed < 0) {
        /* A run that finished while this screen was not open: the model keeps
         * its steps, so show it as done rather than "nothing is loading". */
        s_ctx_unload = !g_mmu.loaded;
        s_ctx_unit = g_mmu.loaded ? g_mmu.loaded_unit : -1;
        s_ctx_slot = g_mmu.loaded ? g_mmu.loaded_slot : -1;
        s_ctx_ext = (g_mmu.loaded && g_mmu.loaded_unit < 0) ? g_mmu.loaded_ext : -1;
        build_this(LP_DONE, &th);
    }
    else {
        build_idle();
    }

    s_sig = signature_of();
}

/* ------------------------------------------------------------------- timer */

/* ------------------------------------------------------- the live update */

/* One row's dot and rail, in the state step_row() would have built for @p st:
 * the fill or the ring it puts on the dot, whether the tick shows, the rail's
 * color and the label's own. */
static void write_row(step_parts_t *r, const char *text, step_state_t st)
{
    if(st == ST_ACTIVE) {
        if(lv_obj_get_style_bg_opa(r->dot, LV_PART_MAIN) != LV_OPA_TRANSP)
            lv_obj_set_style_bg_opa(r->dot, LV_OPA_TRANSP, LV_PART_MAIN);
        if(lv_obj_get_style_border_width(r->dot, LV_PART_MAIN) != UI_PX(3))
            lv_obj_set_style_border_width(r->dot, UI_PX(3), LV_PART_MAIN);
        lv_obj_set_style_border_color(r->dot, UI_TOK_ACCENT, LV_PART_MAIN);
    }
    else if(st == ST_PENDING) {
        if(lv_obj_get_style_bg_opa(r->dot, LV_PART_MAIN) != LV_OPA_TRANSP)
            lv_obj_set_style_bg_opa(r->dot, LV_OPA_TRANSP, LV_PART_MAIN);
        if(lv_obj_get_style_border_width(r->dot, LV_PART_MAIN) != UI_PX(2))
            lv_obj_set_style_border_width(r->dot, UI_PX(2), LV_PART_MAIN);
        lv_obj_set_style_border_color(r->dot, UI_TOK_TEXT_FAINT, LV_PART_MAIN);
    }
    else {
        ui_update_bg(r->dot, st == ST_FAILED ? UI_TOK_ERROR : UI_TOK_ACCENT);
        if(lv_obj_get_style_bg_opa(r->dot, LV_PART_MAIN) != LV_OPA_COVER)
            lv_obj_set_style_bg_opa(r->dot, LV_OPA_COVER, LV_PART_MAIN);
        if(lv_obj_get_style_border_width(r->dot, LV_PART_MAIN) != 0)
            lv_obj_set_style_border_width(r->dot, 0, LV_PART_MAIN);
    }
    /* A hidden flag is read back, so a tick that is already showing (or already
     * hidden) costs nothing. */
    if(lv_obj_is_hidden(r->tick) != (st != ST_DONE)) lv_obj_set_hidden(r->tick, st != ST_DONE);

    if(r->rail) {
        if(lv_obj_get_style_bg_opa(r->rail, LV_PART_MAIN) != LV_OPA_COVER)
            lv_obj_set_style_bg_opa(r->rail, LV_OPA_COVER, LV_PART_MAIN);
        ui_update_bg(r->rail, st == ST_DONE ? UI_TOK_ACCENT : UI_TOK_SURFACE_1);
    }
    ui_update_text_fit(r->label, text, UI_HOME_FONT_SECTION,
                       UI_HOME_W - 2 * UI_HOME_HEADER_PAD - STEP_DOT - UI_PX(12));
    ui_update_text_color(r->label, st == ST_FAILED ? UI_TOK_ERROR
                                     : (st == ST_PENDING ? UI_TOK_TEXT_FAINT : UI_TOK_TEXT));
    r->state = st;
}

/* How the row @p i of a run in @p a stands, the way build_rows() decides it. */
static step_state_t state_of(int i, int phase, const view_activity_t *a, int stop_at)
{
    if(phase == LP_DONE || phase == LP_CONFIRM) return ST_DONE;
    if(phase == LP_STOPPED) return (i < stop_at) ? ST_DONE : ST_PENDING;
    if(i == a->failed) return ST_FAILED;
    if(i == a->index) return ST_ACTIVE;
    if(a->index >= 0 && i < a->index) return ST_DONE;
    return ST_PENDING;
}

/** This screen's own toolhead, or false when the display is bound to nothing. */
static bool this_toolhead(view_toolhead_t *th)
{
    int unit = mmu_this_unit();
    int thi = (unit >= 0 && unit < (int)g_mmu.unit_count) ? g_mmu.units[unit].toolhead : -1;

    memset(th, 0, sizeof *th);
    if(thi < 0) return false;
    return view_toolhead(&g_mmu, thi, th);
}

/* Follow a run that is on this unit, in place: the step that advances, the
 * fault that arrives, the spool that is edited while the filament moves, the
 * alerts that pile up. False - having written nothing - whenever the screen a
 * fresh build would draw has a different shape: the run starting or ending, a
 * different number of steps, a step failing where no line was there to fail,
 * the run moving to another unit, a refusal. */
static bool update(void)
{
    view_toolhead_t th;
    char why[64];
    char line[96];
    uint32_t rgb = 0;
    bool known = false;
    int count = 0;
    bool clear = false;
    int tone = VIEW_TONE_INFO;
    int i;

    if(!s_shown.bound || s_shown.scr != lv_screen_active()) return false;

    /* --- what would have to be built again, decided before anything is written */

    /* a refusal, or a run on another unit, or no run at all: another screen */
    if(g_mmu.action_refusal[0] &&
       (g_mmu.action_refusal_kind == MMU_ACTION_LOAD ||
        g_mmu.action_refusal_kind == MMU_ACTION_UNLOAD))
        return false;
    if(g_mmu.busy == MMU_BUSY_NONE) return false;
    if(mmu_busy_elsewhere(why, sizeof why)) return false;
    if(!this_toolhead(&th)) return false;
    /* the same run, at the same length: rows appearing is a new screen */
    if((int)th.activity.step_count != s_shown.rows) return false;
    /* a step failing puts a line under the filament that was not there */
    if((th.activity.failed >= 0) != (s_shown.phase == LP_FAILED)) return false;
    if(filament_text(&rgb, &known, line, sizeof line) != (s_shown.line != NULL)) return false;
    ui_update_alert_totals(&count, &tone, &clear);
    if(ui_update_alert_slot_objects(count, clear) !=
       (s_shown.alert ? (int)lv_obj_get_child_count(s_shown.alert) : 0)) return false;

    /* --- from here on the screen on screen is only being written to */

    ui_update_header_title(s_shown.hdr, title_text(s_shown.phase, &th));
    if(count > 0 && s_shown.alert) ui_update_alert_mark(s_shown.alert, tone, count);

    if(s_shown.line) {
        ui_update_text_fit(s_shown.line, line, UI_HOME_FONT_BODY, UI_HOME_INNER_W - UI_PX(20));
        /* the swatch is a plain box, not a kit one: its fill is all it has */
        ui_update_bg(s_shown.swatch, known ? lv_color_hex(rgb) : UI_TOK_SURFACE_1);
    }
    if(s_shown.failed) ui_update_text_fit(s_shown.failed, th.message.text, UI_HOME_FONT_BODY,
                                          UI_HOME_INNER_W);

    for(i = 0; i < s_shown.rows; i++) {
        step_state_t st = state_of(i, s_shown.phase, &th.activity, s_stop_at);

        if(st == s_shown.row[i].state &&
           strcmp(lv_label_get_text(s_shown.row[i].label), th.activity.steps[i]) == 0)
            continue;
        write_row(&s_shown.row[i], th.activity.steps[i], st);
    }
    s_sig = signature_of();     /* the timer must not ask again for this */
    return true;
}

/* ------------------------------------------------------------------- timer */

static void on_tick(lv_timer_t *t)
{
    (void)t;
    if(!s_scr) return;
    /* The wait for the host's report ends on a clock, which no signature
     * carries, so an expired wait rebuilds too. */
    if(signature_of() != s_sig ||
       (s_starting_seq && lv_tick_elaps(s_starting_at) >= LP_STARTING_MS)) {
        /* A step advancing is the one change this screen sees every few
         * seconds: it is brought to the screen in place, and only what in
         * place cannot answer for is built again. */
        if(!update()) populate();
    }
}

static void on_delete(lv_event_t *e)
{
    if(s_timer) {
        lv_timer_delete(s_timer);
        s_timer = NULL;
    }
    /* A goto builds the next screen before this one is handed to LVGL, so
     * only the screen this screen's own state is about may be cleared:
     * clearing it for the screen that replaced it would leave that one with
     * no timer and no update(). */
    if(lv_event_get_target(e) == s_scr) s_scr = NULL;
}

/* ------------------------------------------------------------------- build */

static lv_obj_t *build(void)
{
    s_ctx_unit = -1;
    s_ctx_slot = -1;
    s_ctx_ext = -1;
    s_ctx_unload = false;
    s_plan_was = false;
    s_ran_this = false;
    s_ext_op = false;
    s_stopped = false;
    s_confirmed = false;
    s_stop_at = 0;
    s_starting_seq = 0;
    s_starting_at = 0;
    s_refusal_seq = 0;
    s_sig = -1;

    s_scr = ui_kit_screen();
    lv_obj_add_event_cb(s_scr, on_delete, LV_EVENT_DELETE, NULL);

    populate();

    s_timer = lv_timer_create(on_tick, 400, NULL);
    return s_scr;
}

static int modal_count(void)
{
    return 0;
}

static void open_modal(int index)
{
    (void)index;
}

const ui_screen_ops_t screen_load_ops = { build, modal_count, open_modal, update };

/**
 * @file ui_sheet.c
 * Bottom sheets. One frame - a clickable scrim over the whole screen with the
 * sheet hanging off the bottom edge - carries every dialog, the Menu and the
 * toast. Sheets live on lv_layer_top(), so they outlive the screen underneath.
 *
 * See docs/VISUAL_SPEC_480.md, "Slot sheet" (the frame, the rows, the buttons)
 * and "Motion" (the scrim fade and the sheet's rise).
 */

#include "ui/ui_sheet.h"

#include "app/actions.h"
#include "ui/ui_fx.h"
#include "ui/ui_icons.h"
#include "ui/ui_nav.h"
#include "ui/ui_theme.h"
#include "view/strings.h"

/* Every sheet is built from the same three children, in this order. */
#define SHEET_HANDLE   0
#define SHEET_TITLE    1

#define SHEET_TITLE_W  360 /* stops short of a header icon at the right (x 413) */
#define SHEET_TITLE_Y  20
#define SHEET_BODY_Y   48
#define SHEET_TEXT_X   44  /* where a row's icon and text sit */
#define SHEET_LINE_X2  (UI_SCREEN_W - 16) /* the divider stops level with the header text */
#define SHEET_CHEVRON_X (UI_SCREEN_W - 34) /* right inset 16, chevron glyph 18 wide */
#define SHEET_BTN_H    36

static void cover_apply(int opaque_top);
static void cover_restore(void);
#define SHEET_PAD_X    16  /* the strip's side padding */
#define SHEET_BODY_PAD_B 12 /* bottom padding below the last row/button */

#define TOAST_TEXT_MAX_W 360 /* text wraps past this; the pill grows around it */
#define TOAST_PAD_H    12  /* pad x = 12 */
#define TOAST_ICON_W   18
#define TOAST_GAP      8

/* ------------------------------------------------------------------ motion */

static void anim_opa_cb(void *var, int32_t v)
{
    lv_obj_set_style_opa((lv_obj_t *)var, (lv_opa_t)v, 0);
}

/* Fade a whole object in from nothing (the toast; the scrim fades its
 * background instead, see ui_fx_fade_in). */
static void anim_fade_in(lv_obj_t *obj, uint32_t ms)
{
    lv_anim_t a;

    if(!UI_FX_MOTION) return;   /* fully shown from its first frame */

    lv_obj_set_style_opa(obj, LV_OPA_TRANSP, 0);

    lv_anim_init(&a);
    lv_anim_set_var(&a, obj);
    lv_anim_set_exec_cb(&a, anim_opa_cb);
    lv_anim_set_values(&a, LV_OPA_TRANSP, LV_OPA_COVER);
    lv_anim_set_duration(&a, ms);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_out);
    lv_anim_start(&a);
}

/* ------------------------------------------------------------- small parts */

/* A plain rectangle: no theme, no scroll, exactly where and how big we say. */
static lv_obj_t *mk_box(lv_obj_t *parent, int x, int y, int w, int h)
{
    lv_obj_t *o = lv_obj_create(parent);

    lv_obj_remove_style_all(o);
    lv_obj_set_pos(o, x, y);
    lv_obj_set_size(o, w, h);
    lv_obj_set_scrollable(o, false);
    return o;
}

static lv_obj_t *mk_text(lv_obj_t *parent, const char *text, const lv_font_t *font,
                         lv_color_t color)
{
    lv_obj_t *l = lv_label_create(parent);

    lv_obj_remove_style_all(l);
    lv_label_set_text(l, text ? text : "");
    lv_obj_set_style_text_font(l, font, 0);
    lv_obj_set_style_text_color(l, color, 0);
    return l;
}

/* A dark spool would melt into the panel, so its swatch gets a hairline. */
static bool is_dark_filament(uint32_t rgb)
{
    return lv_color_to_u32(ui_readable_text(rgb)) == lv_color_to_u32(UI_COL_TEXT);
}

/* The button strip is not a row, so the row divider can tell them apart. */
static char s_strip_tag;

static lv_obj_t *find_strip(lv_obj_t *body)
{
    uint32_t i, n = lv_obj_get_child_count(body);

    for(i = 0; i < n; i++) {
        lv_obj_t *c = lv_obj_get_child(body, (int32_t)i);

        if(lv_obj_get_user_data(c) == &s_strip_tag) return c;
    }
    return NULL;
}

/* How many rows have been added so far: only the first one goes without a
 * divider, so the strip must not be counted. */
static int body_rows(lv_obj_t *body)
{
    uint32_t i, n = lv_obj_get_child_count(body);
    int rows = 0;

    for(i = 0; i < n; i++) {
        lv_obj_t *c = lv_obj_get_child(body, (int32_t)i);

        if(lv_obj_get_user_data(c) != &s_strip_tag) rows++;
    }
    return rows;
}

/* ------------------------------------------------------------------- frame */

static void on_scrim_click(lv_event_t *e)
{
    /* Clicks on the sheet bubble here too; only the scrim itself dismisses. */
    if(lv_event_get_target_obj(e) != (lv_obj_t *)lv_event_get_current_target(e)) return;
    ui_sheet_close(NULL);
}

/* --------------------------------------------------------- swipe to dismiss */

#define SHEET_DISMISS_DRAG_PX  50 /* a drag past this far closes on release */
#define SHEET_DISMISS_FLICK_VY 8  /* a release this fast closes regardless of dy */
#define SHEET_DISMISS_MS       120
#define SHEET_SCRIM_OPA_MIN    60 /* the scrim's dimmest point, dragged a full sheet-height down */

/* Drag state. One sheet is ever open, so one drag is ever in flight; reset
 * whenever a sheet opens so a gesture cut short by ui_nav_modal_close_all()
 * (below) never leaks into the next sheet's first drag. */
static bool s_drag_active;
static int  s_drag_rest_y;   /* the sheet's y before this drag started */
static int  s_drag_dy;       /* accumulated downward offset, >= 0 */
static int  s_drag_last_vy;  /* the most recent per-frame vect.y, for a flick */

static void anim_y_cb(void *var, int32_t v)
{
    lv_obj_set_y((lv_obj_t *)var, v);
}

static void anim_bg_opa_cb(void *var, int32_t v)
{
    lv_obj_set_style_bg_opa((lv_obj_t *)var, (lv_opa_t)v, 0);
}

/* Closing for real happens only once the slide-down finishes playing, so the
 * user sees the sheet leave before it is gone. If the sheet is deleted from
 * under this anim first (ui_nav_modal_close_all() elsewhere), LVGL deletes
 * the anim along with its var and this callback never runs - one close, not
 * two. */
static void dismiss_done_cb(lv_anim_t *a)
{
    (void)a;
    ui_sheet_close(NULL);
}

/* The scrim's opacity at drag offset @p dy of a sheet @p sheet_h tall: full
 * at rest, fading to SHEET_SCRIM_OPA_MIN by the time the sheet has moved its
 * own height down. */
static lv_opa_t drag_scrim_opa(int dy, int sheet_h)
{
    int span = UI_SCRIM_OPA - SHEET_SCRIM_OPA_MIN;
    int opa;

    if(sheet_h <= 0 || dy <= 0) return (lv_opa_t)UI_SCRIM_OPA;
    if(dy >= sheet_h) return (lv_opa_t)SHEET_SCRIM_OPA_MIN;
    opa = UI_SCRIM_OPA - span * dy / sheet_h;
    return (lv_opa_t)opa;
}

static void close_async_cb(void *user)
{
    (void)user;
    ui_sheet_close(NULL);
}

static void start_move_anim(lv_obj_t *sheet, int from_y, int to_y, lv_anim_path_cb_t path,
                            lv_anim_completed_cb_t completed)
{
    lv_anim_t a;

    if(!UI_FX_MOTION) {
        /* Straight to the end: back at rest, or closed - on the next pass,
         * since this runs inside one of the sheet's own event handlers and
         * the close deletes the sheet. */
        if(completed) lv_async_call(close_async_cb, NULL);
        else          anim_y_cb(sheet, to_y);
        return;
    }

    lv_anim_init(&a);
    lv_anim_set_var(&a, sheet);
    lv_anim_set_exec_cb(&a, anim_y_cb);
    lv_anim_set_values(&a, from_y, to_y);
    lv_anim_set_duration(&a, SHEET_DISMISS_MS);
    lv_anim_set_path_cb(&a, path);
    if(completed) lv_anim_set_completed_cb(&a, completed);
    lv_anim_start(&a);
}

/* Whether a released drag of @p dy px, last moving at @p last_vy px/frame,
 * counts as a dismiss: a definite drag, or a quick flick regardless of how
 * far it got. */
static bool sheet_should_dismiss(int dy, int last_vy)
{
    return dy > SHEET_DISMISS_DRAG_PX || last_vy >= SHEET_DISMISS_FLICK_VY;
}

/* The drag zone: the top 48 px (the handle and title), transparent and
 * clickable, sitting under the (non-clickable) handle/title so a press there
 * still reaches it. LV_EVENT_PRESSING accumulates the drag and moves the
 * sheet and fades the scrim live; RELEASED/PRESS_LOST decides dismiss vs.
 * snap back. */
static void on_drag_zone(lv_event_t *e)
{
    lv_obj_t *zone = lv_event_get_current_target_obj(e);
    lv_obj_t *sheet = lv_obj_get_parent(zone);
    lv_obj_t *scrim = lv_obj_get_parent(sheet);
    lv_event_code_t code = lv_event_get_code(e);
    lv_indev_t *indev = lv_indev_active();

    if(!indev) return;

    if(code == LV_EVENT_PRESSING) {
        lv_point_t vect;

        lv_indev_get_vect(indev, &vect);
        if(!s_drag_active) {
            s_drag_active = true;
            s_drag_rest_y = lv_obj_get_y(sheet);
            s_drag_dy = 0;
            cover_restore();   /* the card is about to move off what it hides */
        }
        s_drag_dy += vect.y;
        if(s_drag_dy < 0) s_drag_dy = 0;
        s_drag_last_vy = vect.y;

        lv_obj_set_y(sheet, s_drag_rest_y + s_drag_dy);
        lv_obj_set_style_bg_opa(scrim, drag_scrim_opa(s_drag_dy, lv_obj_get_height(sheet)), 0);
    }
    else if(code == LV_EVENT_RELEASED || code == LV_EVENT_PRESS_LOST) {
        int rest_y, dy;

        if(!s_drag_active) return;
        rest_y = s_drag_rest_y;
        dy = s_drag_dy;
        s_drag_active = false;

        if(sheet_should_dismiss(dy, s_drag_last_vy)) {
            start_move_anim(sheet, rest_y + dy, UI_SCREEN_H, lv_anim_path_ease_in,
                            dismiss_done_cb);
        }
        else {
            start_move_anim(sheet, rest_y + dy, rest_y, lv_anim_path_ease_out, NULL);

            if(!UI_FX_MOTION) {
                lv_obj_set_style_bg_opa(scrim, UI_SCRIM_OPA, 0);
                cover_apply(rest_y + UI_RADIUS_SHEET);   /* back at rest */
            }
            else {
                lv_anim_t a;

                lv_anim_init(&a);
                lv_anim_set_var(&a, scrim);
                lv_anim_set_exec_cb(&a, anim_bg_opa_cb);
                lv_anim_set_values(&a, drag_scrim_opa(dy, lv_obj_get_height(sheet)),
                                  UI_SCRIM_OPA);
                lv_anim_set_duration(&a, SHEET_DISMISS_MS);
                lv_anim_set_path_cb(&a, lv_anim_path_ease_out);
                lv_anim_start(&a);
            }
        }
    }
}

/* A swipe-down starting lower in the body bubbles here as a gesture once the
 * body's own scroll runs out of room upward (LVGL's normal overscroll ->
 * gesture handoff); only dismiss when the body has nothing left to scroll up
 * into, so the same swipe still scrolls a long list first. */
static void on_sheet_gesture(lv_event_t *e)
{
    lv_obj_t *sheet = lv_event_get_current_target_obj(e);
    lv_obj_t *body = ui_sheet_body(sheet);
    lv_indev_t *indev = lv_indev_active();

    if(!indev || !body) return;
    if(lv_indev_get_gesture_dir(indev) != LV_DIR_BOTTOM) return;
    if(lv_obj_get_scroll_y(body) > 0) return;

    start_move_anim(sheet, lv_obj_get_y(sheet), UI_SCREEN_H, lv_anim_path_ease_in,
                    dismiss_done_cb);
}

/* ------------------------------------------------------------ the cover */

/* LVGL draws the whole active screen and then the top layer over it: it does
 * not see that the sheet's opaque card hides most of the screen, so every
 * frame of a sheet redrew the screen beneath it (about 300 ms on the ESP32).
 * While a sheet is open, the screen's objects that lie wholly behind the
 * card's opaque part are hidden, and shown again when the sheet's scrim is
 * deleted (however it closes). They were invisible anyway: no pixel changes.
 * Only without motion: an entrance animation moves the card below its rest
 * place for a moment, and would show the gap. */
#define COVER_MAX 32
static lv_obj_t *s_covered[COVER_MAX];
static int s_covered_n;

static void cover_restore(void)
{
    int i;

    for(i = 0; i < s_covered_n; i++)
        if(lv_obj_is_valid(s_covered[i])) lv_obj_set_hidden(s_covered[i], false);
    s_covered_n = 0;
}

static void on_scrim_delete(lv_event_t *e)
{
    (void)e;
    cover_restore();
}

/* Hide what lies wholly below @p opaque_top (screen y), across the full width
 * the card spans. */
static void cover_apply(int opaque_top)
{
    lv_obj_t *scr = lv_screen_active();
    uint32_t i, n;

    cover_restore();
    if(UI_FX_MOTION || !scr) return;
    n = lv_obj_get_child_count(scr);
    for(i = 0; i < n && s_covered_n < COVER_MAX; i++) {
        lv_obj_t *c = lv_obj_get_child(scr, (int32_t)i);
        lv_area_t r;

        if(lv_obj_is_hidden(c)) continue;   /* not ours to show again */
        lv_obj_get_coords(c, &r);
        if(r.y1 < opaque_top || r.x1 < 0 || r.x2 >= UI_SCREEN_W) continue;
        lv_obj_set_hidden(c, true);
        s_covered[s_covered_n++] = c;
    }
}

lv_obj_t *ui_sheet_open(const char *title, int h)
{
    lv_obj_t *scrim, *sheet, *body, *drag;
    int vis = h, y;

    if(vis < UI_SHEET_MIN_H) vis = UI_SHEET_MIN_H;
    if(vis > UI_SHEET_MAX_H) vis = UI_SHEET_MAX_H;
    y = UI_SCREEN_H - vis;

    ui_nav_modal_close_all();
    if(!ui_nav_mem_ok(UI_NAV_SHEET_NEED)) return NULL;   /* the callers take a NULL sheet as "no sheet" */
    s_drag_active = false;
    s_drag_dy = 0;

    scrim = mk_box(lv_layer_top(), 0, 0, UI_SCREEN_W, UI_SCREEN_H);
    lv_obj_add_style(scrim, ui_style(UI_STYLE_MODAL_SCRIM), 0);
    lv_obj_set_clickable(scrim, true);
    lv_obj_add_event_cb(scrim, on_scrim_click, LV_EVENT_CLICKED, NULL);
    lv_obj_add_event_cb(scrim, on_scrim_delete, LV_EVENT_DELETE, NULL);
    /* The sheet hangs 16 px below the bottom edge so its lower corners are
     * off-screen; that overhang is deliberate, so the scrim shows overflow. */
    lv_obj_set_overflow_visible(scrim, true);

    sheet = mk_box(scrim, 0, y, UI_SCREEN_W, vis + 16);
    lv_obj_add_style(sheet, ui_style(UI_STYLE_MODAL_CARD), 0);
    lv_obj_set_clickable(sheet, true); /* a tap here does nothing, only bubbles */
    lv_obj_add_event_cb(sheet, on_sheet_gesture, LV_EVENT_GESTURE, NULL);

    {
        lv_obj_t *handle = mk_box(sheet, (UI_SCREEN_W - 32) / 2, 8, 32, 4);
        lv_obj_set_style_radius(handle, 2, 0);
        lv_obj_set_style_bg_color(handle, UI_COL_TEXT_FAINT, 0);
        lv_obj_set_style_bg_opa(handle, LV_OPA_COVER, 0);
    }

    {
        lv_obj_t *t = mk_text(sheet, title, UI_FONT_L, UI_COL_TEXT);
        lv_label_set_long_mode(t, LV_LABEL_LONG_MODE_DOTS);
        lv_obj_set_width(t, SHEET_TITLE_W);
        lv_obj_set_pos(t, UI_PAD_XXL, SHEET_TITLE_Y);
    }

    body = mk_box(sheet, 0, SHEET_BODY_Y, UI_SCREEN_W, vis - SHEET_BODY_Y);
    lv_obj_set_style_bg_opa(body, LV_OPA_TRANSP, 0);
    lv_obj_set_flex_flow(body, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(body, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_row(body, 0, 0);
    lv_obj_set_scrollable(body, true); /* more rows than fit are scrollable */
    lv_obj_set_scroll_dir(body, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(body, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_user_data(sheet, body);

    /* The drag zone is transparent, so its place in the paint order next to
     * the (also non-clickable) handle and title does not matter. */
    drag = mk_box(sheet, 0, 0, UI_SCREEN_W, SHEET_BODY_Y);
    lv_obj_set_style_bg_opa(drag, LV_OPA_TRANSP, 0);
    lv_obj_set_clickable(drag, true);
    lv_obj_add_event_cb(drag, on_drag_zone, LV_EVENT_PRESSING, NULL);
    lv_obj_add_event_cb(drag, on_drag_zone, LV_EVENT_RELEASED, NULL);
    lv_obj_add_event_cb(drag, on_drag_zone, LV_EVENT_PRESS_LOST, NULL);

    ui_nav_modal_add(scrim);

    ui_fx_fade_in(scrim, UI_SCRIM_OPA, 120);
    ui_fx_slide_up_in(sheet, 12, 140);
    return sheet;
}

lv_obj_t *ui_sheet_body(lv_obj_t *sheet)
{
    if(!sheet) return NULL;
    return lv_obj_get_user_data(sheet);
}

void ui_sheet_fit(lv_obj_t *sheet)
{
    lv_obj_t *body = ui_sheet_body(sheet);
    uint32_t i, n;
    int content = 0;
    int vis;

    if(!body) return;

    /* Row/button heights are set explicitly, but a plain label or a card
     * added straight into the body may still be LV_SIZE_CONTENT; force the
     * pending layout so every child reports its real position and height. */
    lv_obj_update_layout(body);

    /* The furthest child bottom edge, not a plain sum: a caller (the color
     * palette) sets its own pad_row between children, and only the flex
     * layout above knows how that turned into y positions. */
    n = lv_obj_get_child_count(body);
    for(i = 0; i < n; i++) {
        lv_obj_t *c = lv_obj_get_child(body, (int32_t)i);
        int extent = lv_obj_get_y(c) + lv_obj_get_height(c);

        if(extent > content) content = extent;
    }

    vis = SHEET_BODY_Y + content + SHEET_BODY_PAD_B;
    if(vis < UI_SHEET_MIN_H) vis = UI_SHEET_MIN_H;
    if(vis > UI_SHEET_MAX_H) vis = UI_SHEET_MAX_H;

    lv_obj_set_height(sheet, vis + 16);
    lv_obj_set_y(sheet, UI_SCREEN_H - vis);
    lv_obj_set_height(body, vis - SHEET_BODY_Y);
    cover_apply(UI_SCREEN_H - vis + UI_RADIUS_SHEET);
}

void ui_sheet_close(lv_obj_t *sheet)
{
    (void)sheet; /* there is only ever one, and the registry owns it */
    /* Closing the registered modal deletes the scrim, the sheet's parent. */
    ui_nav_modal_close_all();
}

/* ---------------------------------------------------------------- header */

void ui_sheet_header(lv_obj_t *sheet, int32_t swatch_rgb, const char *right_text, bool tag_icon)
{
    lv_obj_t *title, *l;
    int rw = 0;

    if(!sheet) return;
    title = lv_obj_get_child(sheet, SHEET_TITLE);

    if(swatch_rgb >= 0) {
        lv_color_t top, bottom;

        ui_filament_gradient((uint32_t)swatch_rgb, &top, &bottom);

        l = mk_box(sheet, UI_PAD_XXL, 24, 10, 10);
        lv_obj_set_style_radius(l, 2, 0);
        lv_obj_set_style_bg_color(l, top, 0);
        lv_obj_set_style_bg_opa(l, LV_OPA_COVER, 0);
        if(is_dark_filament((uint32_t)swatch_rgb)) {
            lv_obj_set_style_border_width(l, 1, 0);
            lv_obj_set_style_border_color(l, UI_COL_TEXT_FAINT, 0);
        }
        if(title) lv_obj_set_x(title, 32); /* the swatch takes the title's spot */
    }

    if(right_text == NULL || right_text[0] == '\0') return;

    {
        lv_point_t sz;

        lv_text_get_size(&sz, right_text, UI_FONT_M, 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
        rw = (int)sz.x;
    }

    l = mk_text(sheet, right_text, UI_FONT_M, UI_COL_TEXT_DIM);
    lv_obj_align(l, LV_ALIGN_TOP_RIGHT, -UI_PAD_XXL, 22);

    if(tag_icon) {
        l = mk_text(sheet, UI_ICON_NFC_VARIANT, UI_FONT_ICON, UI_COL_TEXT_DIM);
        lv_obj_align(l, LV_ALIGN_TOP_RIGHT, -(UI_PAD_XXL + rw + 6), 22);
    }
}

/* ------------------------------------------------------------------- rows */

lv_obj_t *ui_sheet_row(lv_obj_t *sheet, const char *icon, const char *label, const char *reason,
                       lv_event_cb_t cb, void *user, bool primary)
{
    lv_obj_t *body = ui_sheet_body(sheet);
    lv_obj_t *row, *l;
    bool enabled = (reason == NULL);

    if(!body) return NULL;

    row = mk_box(body, 0, 0, UI_SCREEN_W, enabled ? 44 : 56);

    if(body_rows(body) > 0) {
        l = mk_box(row, SHEET_TEXT_X, 0, SHEET_LINE_X2 - SHEET_TEXT_X, 1);
        lv_obj_set_style_bg_color(l, UI_COL_LINE, 0);
        lv_obj_set_style_bg_opa(l, LV_OPA_COVER, 0);
    }

    if(icon) {
        lv_color_t ic = UI_COL_TEXT_FAINT;

        if(enabled) ic = primary ? UI_COL_ACCENT : UI_COL_TEXT_DIM;
        l = mk_text(row, icon, UI_FONT_ICON, ic);
        lv_obj_align(l, LV_ALIGN_LEFT_MID, UI_PAD_XXL, 0);
    }

    if(enabled) {
        l = mk_text(row, label, UI_FONT_M, UI_COL_TEXT);
        lv_obj_align(l, LV_ALIGN_LEFT_MID, SHEET_TEXT_X, 0);

        l = mk_text(row, UI_ICON_CHEVRON_RIGHT, UI_FONT_ICON, UI_COL_TEXT_FAINT);
        lv_obj_align(l, LV_ALIGN_LEFT_MID, SHEET_CHEVRON_X, 0);

        lv_obj_set_clickable(row, true);
        lv_obj_set_style_bg_color(row, UI_COL_PANEL, LV_STATE_PRESSED);
        lv_obj_set_style_bg_opa(row, LV_OPA_COVER, LV_STATE_PRESSED);
        if(cb) lv_obj_add_event_cb(row, cb, LV_EVENT_CLICKED, user);
    }
    else {
        l = mk_text(row, label, UI_FONT_M, UI_COL_TEXT_FAINT);
        lv_obj_set_pos(l, SHEET_TEXT_X, 8);

        l = mk_text(row, reason, UI_FONT_S, UI_COL_TEXT_DIM);
        lv_label_set_long_mode(l, LV_LABEL_LONG_MODE_DOTS);
        lv_obj_set_width(l, SHEET_LINE_X2 - SHEET_TEXT_X);
        lv_obj_set_pos(l, SHEET_TEXT_X, 28);

        lv_obj_set_clickable(row, false);
    }
    ui_sheet_fit(sheet);
    return row;
}

/* ---------------------------------------------------------------- buttons */

lv_obj_t *ui_sheet_button(lv_obj_t *sheet, const char *label, lv_event_cb_t cb, void *user,
                          bool primary)
{
    lv_obj_t *body = ui_sheet_body(sheet);
    lv_obj_t *strip, *btn, *l;
    lv_color_t fill;

    if(!body) return NULL;

    strip = find_strip(body);
    if(strip == NULL) {
        strip = mk_box(body, 0, 0, UI_SCREEN_W, SHEET_BTN_H + UI_PAD_L);
        lv_obj_set_user_data(strip, &s_strip_tag);
        lv_obj_set_style_bg_opa(strip, LV_OPA_TRANSP, 0);
        lv_obj_set_flex_flow(strip, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(strip, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER,
                              LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_top(strip, UI_PAD_L, 0); /* the gap above the buttons */
        lv_obj_set_style_pad_hor(strip, SHEET_PAD_X, 0);
        lv_obj_set_style_pad_column(strip, UI_PAD_L, 0);
    }

    fill = primary ? UI_COL_ACCENT : UI_COL_PANEL;
    btn = lv_button_create(strip);
    lv_obj_remove_style_all(btn);
    lv_obj_set_style_bg_color(btn, fill, 0);
    lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(btn, UI_RADIUS, 0);
    lv_obj_set_style_bg_color(btn, lv_color_mix(lv_color_white(), fill, LV_OPA_10),
                              LV_STATE_PRESSED);
    lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, LV_STATE_PRESSED);
    lv_obj_set_height(btn, SHEET_BTN_H);
    lv_obj_set_flex_grow(btn, 1);
    lv_obj_set_scrollable(btn, false);

    l = mk_text(btn, label, UI_FONT_L, primary ? UI_COL_BG : UI_COL_TEXT);
    lv_obj_center(l);

    if(cb) lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, user);
    ui_sheet_fit(sheet);
    return btn;
}

/* ------------------------------------------------------------------- menu */

static void on_menu_pick(lv_event_t *e)
{
    int id = (int)(intptr_t)lv_event_get_user_data(e) - 1;

    if(id < 0 || id >= UI_SCREEN_COUNT) {
        ui_sheet_close(NULL);
        return;
    }
    actions_log("open %s", ui_nav_screen_name((ui_screen_id_t)id));
    ui_sheet_close(NULL);
    ui_nav_goto_async((ui_screen_id_t)id);
}

void ui_sheet_menu(void)
{
    static const struct {
        const char *icon;
        ui_screen_id_t id;
        view_str_id_t label;
    } entries[] = {
        { UI_ICON_WATER_PERCENT,     UI_SCREEN_ENVIRONMENT, VS_MENU_ENV },
        { UI_ICON_BELL_OUTLINE,      UI_SCREEN_ALERTS,      VS_MENU_ALERTS },
        { UI_ICON_PRINTER_3D_NOZZLE, UI_SCREEN_PRINT,       VS_MENU_PRINT },
        { UI_ICON_SYNC,              UI_SCREEN_REFILL,      VS_MENU_GROUPS },
        { UI_ICON_SWAP_HORIZONTAL,   UI_SCREEN_MAPPING,     VS_MENU_MAP },
        { UI_ICON_COG,               UI_SCREEN_SETTINGS,    VS_MENU_SETUP },
        { UI_ICON_LINK_VARIANT,      UI_SCREEN_ORDER,       VS_MENU_UNIT },
        { UI_ICON_INFORMATION_OUTLINE, UI_SCREEN_UNIT_INFO, VS_MENU_INFO },
    };
    const int cols = 4;
    const int tw = 104;
    const int th = 64;
    const int gap = 8;
    const int x0 = 20;
    lv_obj_t *sheet = ui_sheet_open(view_str(VS_SCREEN_MENU), 200);
    if(!sheet) return;   /* memory is short: no sheet, nothing to fill */
    lv_obj_t *body = ui_sheet_body(sheet);
    lv_obj_t *grid;
    size_t i;

    if(!body) return;

    /* The body is a flex column (it carries rows elsewhere), so the grid of
     * tiles is a plain box inside it: only there do explicit positions hold.
     * y = 4 puts the grid's origin at sheet-local y = 52 (SHEET_BODY_Y + 4). */
    grid = mk_box(body, 0, 4, x0 + cols * tw + (cols - 1) * gap, 2 * th + gap);

    for(i = 0; i < sizeof entries / sizeof entries[0]; i++) {
        int col = (int)i % cols;
        int row = (int)i / cols;
        lv_obj_t *t = lv_button_create(grid);
        lv_obj_t *l;

        lv_obj_remove_style_all(t);
        lv_obj_set_style_bg_color(t, UI_COL_PANEL, 0);
        lv_obj_set_style_bg_opa(t, LV_OPA_COVER, 0);
        lv_obj_set_style_radius(t, UI_RADIUS, 0);
        lv_obj_set_style_bg_color(t, UI_COL_LINE, LV_STATE_PRESSED);
        lv_obj_set_style_bg_opa(t, LV_OPA_COVER, LV_STATE_PRESSED);
        lv_obj_set_pos(t, x0 + col * (tw + gap), row * (th + gap));
        lv_obj_set_size(t, tw, th);
        lv_obj_set_scrollable(t, false);

        l = mk_text(t, entries[i].icon, UI_FONT_ICON, UI_COL_TEXT);
        lv_obj_align(l, LV_ALIGN_CENTER, 0, -10);

        l = mk_text(t, view_str(entries[i].label), UI_FONT_S, UI_COL_TEXT);
        lv_obj_align(l, LV_ALIGN_CENTER, 0, 14);

        lv_obj_add_event_cb(t, on_menu_pick, LV_EVENT_CLICKED,
                            (void *)(intptr_t)(entries[i].id + 1));
    }
    ui_sheet_fit(sheet);
}

/* ------------------------------------------------------------------ toast */

static lv_obj_t *s_toast;
static lv_timer_t *s_toast_timer;

static void toast_done_cb(lv_timer_t *t)
{
    (void)t;
    s_toast_timer = NULL; /* one-shot timers delete themselves */
    if(s_toast) {
        lv_obj_delete(s_toast);
        s_toast = NULL;
    }
}

void ui_sheet_toast_close(void)
{
    if(s_toast_timer) {
        lv_timer_delete(s_toast_timer);
        s_toast_timer = NULL;
    }
    if(s_toast) {
        lv_obj_delete(s_toast);
        s_toast = NULL;
    }
}

void ui_sheet_toast(const char *text)
{
    lv_obj_t *pill, *l;
    lv_point_t sz;
    int w, h;

    if(text == NULL || text[0] == '\0') return;

    ui_sheet_toast_close();

    lv_text_get_size(&sz, text, UI_FONT_M, 0, 0, TOAST_TEXT_MAX_W, LV_TEXT_FLAG_NONE);

    w = 2 * TOAST_PAD_H + TOAST_ICON_W + TOAST_GAP + sz.x;
    if(w < 60) w = 60;
    h = sz.y + 2 * UI_PAD_L;

    pill = mk_box(lv_layer_top(), 0, 0, w, h);
    lv_obj_set_style_radius(pill, UI_RADIUS_PILL, 0);
    lv_obj_set_style_bg_color(pill, UI_COL_PANEL_HI, 0);
    lv_obj_set_style_bg_opa(pill, LV_OPA_COVER, 0);
    /* Bottom edge 8 px above the nav's top edge (the nav-showing case; a
     * band screen sits 12 px lower still, per VISUAL_SPEC_480 "Toast"). */
    lv_obj_align(pill, LV_ALIGN_BOTTOM_MID, 0, -(UI_BOTTOMNAV_H + 8));

    l = mk_text(pill, UI_ICON_CHECK, UI_FONT_ICON, UI_COL_ACCENT);
    lv_obj_align(l, LV_ALIGN_LEFT_MID, TOAST_PAD_H, 0);

    l = mk_text(pill, text, UI_FONT_M, UI_COL_TEXT);
    lv_label_set_long_mode(l, LV_LABEL_LONG_MODE_WRAP);
    lv_obj_set_width(l, w - (2 * TOAST_PAD_H + TOAST_ICON_W + TOAST_GAP));
    lv_obj_align(l, LV_ALIGN_LEFT_MID, TOAST_PAD_H + TOAST_ICON_W + TOAST_GAP, 0);

    s_toast = pill;
    s_toast_timer = lv_timer_create(toast_done_cb, 1500, NULL);
    lv_timer_set_repeat_count(s_toast_timer, 1);

    anim_fade_in(pill, 120);
    ui_fx_slide_up_in(pill, 8, 120);
}

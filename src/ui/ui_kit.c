/**
 * @file ui_kit.c
 * The device's shared widgets (see ui_kit.h).
 */

#include "ui/ui_kit.h"

#include "app/actions.h"
#include "model/mmu_model.h"
#include "ui/ui_nav.h"
#include "ui/ui_sheet.h"
#include "ui/ui_update.h"
#include "view/strings.h"
#include "view/view.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define BACK_W    40
#define BACK_X    (UI_HOME_HEADER_PAD - 6)
#define TITLE_X   (BACK_X + BACK_W + 6)
#define ROW_R     UI_TOK_RADIUS_TILE
#define GAP       UI_TOK_SPACE_CARD_GAP

static lv_obj_t *box(lv_obj_t *parent, int x, int y, int w, int h)
{
    lv_obj_t *o = lv_obj_create(parent);

    lv_obj_remove_style_all(o);
    lv_obj_set_clickable(o, false);
    lv_obj_set_scrollable(o, false);
    lv_obj_set_pos(o, x, y);
    lv_obj_set_size(o, w, h);
    return o;
}

lv_obj_t *ui_kit_screen(void)
{
    lv_obj_t *scr = lv_obj_create(NULL);

    lv_obj_remove_style_all(scr);
    lv_obj_set_size(scr, UI_HOME_W, UI_HOME_H);
    lv_obj_set_style_bg_color(scr, UI_TOK_BG, 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
    lv_obj_set_scrollable(scr, false);
    return scr;
}

/* ---------------------------------------------------------------- header */

static void on_back(lv_event_t *e)
{
    (void)e;
    ui_nav_back_async();
}

static void on_alerts(lv_event_t *e)
{
    (void)e;
    actions_log("alerts");
    ui_nav_goto_async(UI_SCREEN_ALERTS);
}

static void on_debug(lv_event_t *e)
{
    (void)e;
    ui_nav_goto_async(UI_SCREEN_DEBUG);
}

lv_obj_t *ui_kit_header(lv_obj_t *scr, const char *title, bool alert_slot)
{
    lv_obj_t *hdr = box(scr, 0, 0, UI_HOME_W, UI_HOME_HEADER_H);
    lv_obj_t *back = box(hdr, BACK_X, (UI_HOME_HEADER_H - BACK_W) / 2, BACK_W, BACK_W);
    lv_obj_t *l;
    int lh = lv_font_get_line_height(UI_HOME_FONT_TITLE);

    lv_obj_set_clickable(back, true);
    lv_obj_add_event_cb(back, on_back, LV_EVENT_CLICKED, NULL);
    lv_obj_set_style_radius(back, 8, 0);
    lv_obj_set_style_bg_color(back, UI_TOK_TEXT, LV_STATE_PRESSED);
    lv_obj_set_style_bg_opa(back, LV_OPA_20, LV_STATE_PRESSED);
    ui_home_label(back, UI_ICON_CHEVRON_LEFT, UI_HOME_FONT_ICON, UI_TOK_TEXT_MUTED,
                  (BACK_W - ui_home_text_w(UI_ICON_CHEVRON_LEFT, UI_HOME_FONT_ICON)) / 2,
                  (BACK_W - lv_font_get_line_height(UI_HOME_FONT_ICON)) / 2);

    l = ui_home_label_fit(hdr, title, UI_HOME_FONT_TITLE, UI_TOK_TEXT, TITLE_X,
                          (UI_HOME_HEADER_H - lh) / 2,
                          UI_HOME_W - TITLE_X - UI_HOME_HEADER_PAD - UI_HOME_SLOT_W - 8);
    lv_obj_set_clickable(l, true);
    lv_obj_add_event_cb(l, on_debug, LV_EVENT_LONG_PRESSED, NULL);

    if(alert_slot) {
        int count, tone;
        bool clear;

        ui_update_alert_totals(&count, &tone, &clear);
        /* A mark when something alerts here, the resting check when nothing
         * alerts anywhere, nothing at all when the alert is shown elsewhere
         * (the message row says it, right beside the slot). */
        if(count > 0 || clear) {
            lv_obj_t *slot = box(hdr, UI_HOME_W - UI_HOME_HEADER_PAD - UI_HOME_SLOT_W,
                                 (UI_HOME_HEADER_H - UI_HOME_SLOT_H) / 2,
                                 UI_HOME_SLOT_W, UI_HOME_SLOT_H);

            lv_obj_set_clickable(slot, true);
            lv_obj_add_event_cb(slot, on_alerts, LV_EVENT_CLICKED, NULL);
            if(count > 0) ui_home_alert_mark(slot, tone, count);
            else          ui_home_alert_none(slot);
        }
    }
    return hdr;
}

lv_obj_t *ui_kit_header_action(lv_obj_t *scr, const char *label, lv_event_cb_t cb, void *user)
{
    int w = ui_home_text_w(label, UI_HOME_FONT_BODY) + 24;
    int h = 30;
    lv_obj_t *b = box(scr, UI_HOME_W - UI_HOME_HEADER_PAD - w, (UI_HOME_HEADER_H - h) / 2, w, h);

    lv_obj_set_style_radius(b, h / 2, 0);
    lv_obj_set_style_bg_color(b, UI_TOK_PILL_BG, 0);
    lv_obj_set_style_bg_opa(b, UI_TOK_PILL_BG_OPA, 0);
    lv_obj_set_style_bg_color(b, UI_TOK_TEXT, LV_STATE_PRESSED);
    lv_obj_set_style_bg_opa(b, LV_OPA_30, LV_STATE_PRESSED);
    if(cb) {
        lv_obj_set_clickable(b, true);
        lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, user);
    }
    ui_home_label(b, label, UI_HOME_FONT_BODY, UI_TOK_TEXT, 12,
                  (h - lv_font_get_line_height(UI_HOME_FONT_BODY)) / 2);
    return b;
}

/* ------------------------------------------------------------------ list */

lv_obj_t *ui_kit_column(lv_obj_t *scr, int y, int h)
{
    lv_obj_t *c = lv_obj_create(scr);

    lv_obj_remove_style_all(c);
    lv_obj_set_pos(c, UI_HOME_HEADER_PAD, y);
    lv_obj_set_size(c, UI_HOME_INNER_W, h);
    lv_obj_set_flex_flow(c, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(c, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_row(c, GAP - 2, 0);
    lv_obj_set_style_pad_bottom(c, 8, 0);
    lv_obj_set_scroll_dir(c, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(c, LV_SCROLLBAR_MODE_OFF);
    return c;
}

lv_obj_t *ui_kit_section(lv_obj_t *col, const char *text)
{
    lv_obj_t *l = lv_label_create(col);

    lv_obj_remove_style_all(l);
    lv_label_set_text(l, text);
    lv_label_set_long_mode(l, LV_LABEL_LONG_MODE_CLIP);
    lv_obj_set_style_text_font(l, UI_HOME_FONT_SECTION, 0);
    lv_obj_set_style_text_color(l, UI_TOK_TEXT_MUTED, 0);
    lv_obj_set_style_pad_top(l, 6, 0);
    lv_obj_set_width(l, LV_PCT(100));
    return l;
}

lv_obj_t *ui_kit_row(lv_obj_t *col, int h, lv_event_cb_t cb, void *user)
{
    lv_obj_t *r = lv_obj_create(col);

    lv_obj_remove_style_all(r);
    lv_obj_set_width(r, LV_PCT(100));
    if(h > 0) lv_obj_set_height(r, h);
    else      lv_obj_set_height(r, LV_SIZE_CONTENT);
    lv_obj_set_scrollable(r, false);
    lv_obj_set_style_radius(r, ROW_R, 0);
    lv_obj_set_style_bg_color(r, UI_TOK_SURFACE_1, 0);
    lv_obj_set_style_bg_opa(r, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(r, 12, 0);
    if(cb) {
        lv_obj_set_clickable(r, true);
        lv_obj_set_style_bg_color(r, UI_TOK_TEXT, LV_STATE_PRESSED);
        lv_obj_set_style_bg_opa(r, LV_OPA_20, LV_STATE_PRESSED);
        lv_obj_add_event_cb(r, cb, LV_EVENT_CLICKED, user);
    }
    return r;
}

lv_obj_t *ui_kit_text_row(lv_obj_t *col, const char *title, const char *note,
                          const char *trailing, lv_color_t trailing_color,
                          lv_event_cb_t cb, void *user)
{
    lv_obj_t *r = ui_kit_row(col, 0, cb, user);
    int tw = 0;
    int text_w = UI_HOME_INNER_W - 24;
    lv_obj_t *t;

    if(trailing && trailing[0]) {
        tw = ui_home_text_w(trailing, UI_HOME_FONT_ICON);
        text_w -= tw + 10;
    }
    t = ui_kit_text(r, title, UI_HOME_FONT_BODY, UI_TOK_TEXT, 0, 0, text_w);
    if(note && note[0]) {
        lv_obj_t *n = ui_kit_text(r, note, UI_HOME_FONT_META, UI_TOK_TEXT_MUTED, 0, 0, text_w);

        lv_obj_align_to(n, t, LV_ALIGN_OUT_BOTTOM_LEFT, 0, 2);
    }
    if(tw > 0) {
        lv_obj_t *g = ui_home_label(r, trailing, UI_HOME_FONT_ICON, trailing_color, 0, 0);

        lv_obj_align(g, LV_ALIGN_RIGHT_MID, 0, 0);
    }
    /* A title-only row keeps a touch-sized height. */
    lv_obj_set_style_min_height(r, 44, 0);
    return r;
}

/* -------------------------------------------------------------- controls */

#define SW_W 40
#define SW_H 22

static void on_switch(lv_event_t *e)
{
    lv_obj_t *sw = lv_event_get_target_obj(e);
    lv_obj_t *knob = lv_obj_get_child(sw, 0);
    bool on;

    if(lv_obj_has_state(sw, LV_STATE_CHECKED)) lv_obj_remove_state(sw, LV_STATE_CHECKED);
    else                                       lv_obj_add_state(sw, LV_STATE_CHECKED);
    on = lv_obj_has_state(sw, LV_STATE_CHECKED);
    lv_obj_set_x(knob, on ? SW_W - SW_H + 2 : 2);
    lv_obj_send_event(sw, LV_EVENT_VALUE_CHANGED, NULL);
}

lv_obj_t *ui_kit_switch(lv_obj_t *parent, int x, int y, bool on, lv_event_cb_t cb, void *user)
{
    lv_obj_t *sw = box(parent, x, y, SW_W, SW_H);
    lv_obj_t *knob = box(sw, on ? SW_W - SW_H + 2 : 2, 2, SW_H - 4, SW_H - 4);

    lv_obj_set_clickable(sw, true);
    lv_obj_set_style_radius(sw, SW_H / 2, 0);
    lv_obj_set_style_bg_color(sw, UI_TOK_SURFACE_3, 0);
    lv_obj_set_style_bg_opa(sw, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(sw, 1, 0);
    lv_obj_set_style_border_color(sw, UI_TOK_LINE, 0);
    lv_obj_set_style_border_opa(sw, UI_TOK_LINE_OPA, 0);
    lv_obj_set_style_bg_color(sw, UI_TOK_ACCENT, LV_STATE_CHECKED);
    lv_obj_set_style_border_width(sw, 0, LV_STATE_CHECKED);

    lv_obj_set_style_radius(knob, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(knob, UI_TOK_TEXT, 0);
    lv_obj_set_style_bg_opa(knob, LV_OPA_COVER, 0);

    if(on) lv_obj_add_state(sw, LV_STATE_CHECKED);
    lv_obj_add_event_cb(sw, on_switch, LV_EVENT_CLICKED, NULL);
    if(cb) lv_obj_add_event_cb(sw, cb, LV_EVENT_VALUE_CHANGED, user);
    return sw;
}

bool ui_kit_switch_get(lv_obj_t *sw)
{
    return sw && lv_obj_has_state(sw, LV_STATE_CHECKED);
}

#define CHIP_H 30

void ui_kit_chip_set_selected(lv_obj_t *chip, bool selected)
{
    lv_obj_t *l = lv_obj_get_child(chip, 0);

    if(selected) lv_obj_add_state(chip, LV_STATE_CHECKED);
    else         lv_obj_remove_state(chip, LV_STATE_CHECKED);
    if(l) lv_obj_set_style_text_color(l, selected ? UI_TOK_BG : UI_TOK_TEXT_MUTED, 0);
}

lv_obj_t *ui_kit_chip(lv_obj_t *parent, const char *label, int x, int y, int w,
                      bool selected, lv_event_cb_t cb, void *user)
{
    int tw = ui_home_text_w(label, UI_HOME_FONT_BODY);
    lv_obj_t *c;
    lv_obj_t *l;

    if(w <= 0) w = tw + 24;
    c = box(parent, x, y, w, CHIP_H);
    lv_obj_set_style_radius(c, CHIP_H / 2, 0);
    lv_obj_set_style_bg_color(c, UI_TOK_SURFACE_3, 0);
    lv_obj_set_style_bg_opa(c, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(c, 1, 0);
    lv_obj_set_style_border_color(c, UI_TOK_LINE, 0);
    lv_obj_set_style_border_opa(c, UI_TOK_LINE_OPA, 0);
    lv_obj_set_style_bg_color(c, UI_TOK_ACCENT, LV_STATE_CHECKED);
    lv_obj_set_style_border_width(c, 0, LV_STATE_CHECKED);
    l = ui_home_label(c, label, UI_HOME_FONT_BODY, UI_TOK_TEXT_MUTED, (w - tw) / 2 - 1,
                      (CHIP_H - lv_font_get_line_height(UI_HOME_FONT_BODY)) / 2 - 1);
    (void)l;
    if(cb) {
        lv_obj_set_clickable(c, true);
        lv_obj_add_event_cb(c, cb, LV_EVENT_CLICKED, user);
    }
    ui_kit_chip_set_selected(c, selected);
    return c;
}

lv_obj_t *ui_kit_button(lv_obj_t *parent, const char *label, ui_kit_btn_t kind,
                        int x, int y, int w, int h, bool enabled,
                        lv_event_cb_t cb, void *user)
{
    lv_obj_t *b = box(parent, x, y, w, h);
    bool primary = (kind == UI_KIT_BTN_PRIMARY);
    int tw = ui_home_text_w(label, UI_HOME_FONT_SECTION);

    lv_obj_set_style_radius(b, ROW_R, 0);
    lv_obj_set_style_bg_color(b, (primary && enabled) ? UI_TOK_ACCENT : UI_TOK_SURFACE_1, 0);
    lv_obj_set_style_bg_opa(b, LV_OPA_COVER, 0);
    if(enabled) {
        lv_obj_set_style_bg_color(b, UI_TOK_TEXT, LV_STATE_PRESSED);
        lv_obj_set_style_bg_opa(b, LV_OPA_50, LV_STATE_PRESSED);
    }
    if(!enabled) ui_home_hatch(b, w, h);
    ui_home_label(b, label, UI_HOME_FONT_SECTION,
                  !enabled ? UI_TOK_TEXT_FAINT : (primary ? UI_TOK_BG : UI_TOK_TEXT),
                  (w - tw) / 2, (h - lv_font_get_line_height(UI_HOME_FONT_SECTION)) / 2);
    if(enabled && cb) {
        lv_obj_set_clickable(b, true);
        lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, user);
    }
    return b;
}

static void reason_free(lv_event_t *e)
{
    lv_free(lv_event_get_user_data(e));
}

static void on_reason(lv_event_t *e)
{
    ui_sheet_toast((const char *)lv_event_get_user_data(e));
}

lv_obj_t *ui_kit_button_r(lv_obj_t *parent, const char *label, ui_kit_btn_t kind,
                          int x, int y, int w, int h, const char *reason,
                          lv_event_cb_t cb, void *user)
{
    bool dim = reason && reason[0];
    lv_obj_t *b = ui_kit_button(parent, label, kind, x, y, w, h, !dim, cb, user);

    if(dim) {
        size_t n = strlen(reason) + 1;
        char *copy = lv_malloc(n);

        if(copy) {
            memcpy(copy, reason, n);
            lv_obj_set_clickable(b, true);
            lv_obj_add_event_cb(b, on_reason, LV_EVENT_CLICKED, copy);
            lv_obj_add_event_cb(b, reason_free, LV_EVENT_DELETE, copy);
        }
    }
    return b;
}

lv_obj_t *ui_kit_card(lv_obj_t *parent, int x, int y, int w, int h)
{
    lv_obj_t *c = box(parent, x, y, w, h);

    lv_obj_set_style_radius(c, ROW_R, 0);
    lv_obj_set_style_bg_color(c, UI_TOK_SURFACE_1, 0);
    lv_obj_set_style_bg_opa(c, LV_OPA_COVER, 0);
    return c;
}

lv_obj_t *ui_kit_swatch(lv_obj_t *parent, int x, int y, int size, uint32_t rgb, bool empty)
{
    lv_obj_t *s = box(parent, x, y, size, size);

    lv_obj_set_style_radius(s, size / 4, 0);
    if(empty) {
        lv_obj_set_style_bg_opa(s, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(s, 1, 0);
        lv_obj_set_style_border_color(s, UI_TOK_TEXT_FAINT, 0);
    }
    else {
        lv_obj_set_style_bg_color(s, lv_color_hex(rgb), 0);
        lv_obj_set_style_bg_opa(s, LV_OPA_COVER, 0);
        /* a hairline, so a black or white filament still reads on a card */
        lv_obj_set_style_border_width(s, 1, 0);
        lv_obj_set_style_border_color(s, UI_TOK_LINE, 0);
        lv_obj_set_style_border_opa(s, 60, 0);
    }
    return s;
}

lv_obj_t *ui_kit_ring(lv_obj_t *parent, int x, int y, int size, int pct, lv_color_t color,
                      const char *text)
{
    lv_obj_t *arc = lv_arc_create(parent);
    int sweep = pct <= 0 ? 0 : (pct >= 100 ? 360 : (pct * 360 + 50) / 100);

    if(pct > 0 && sweep < 8) sweep = 8;
    lv_obj_remove_style_all(arc);
    lv_obj_set_clickable(arc, false);
    lv_obj_set_scrollable(arc, false);
    lv_obj_set_pos(arc, x, y);
    lv_obj_set_size(arc, size, size);
    lv_arc_set_rotation(arc, 270);
    lv_arc_set_bg_angles(arc, 0, 360);
    lv_arc_set_angles(arc, 0, sweep);
    lv_obj_set_style_arc_width(arc, 4, 0);
    lv_obj_set_style_arc_color(arc, UI_TOK_SURFACE_3, 0);
    lv_obj_set_style_arc_opa(arc, LV_OPA_COVER, 0);
    lv_obj_set_style_arc_width(arc, 4, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(arc, color, LV_PART_INDICATOR);
    lv_obj_set_style_arc_opa(arc, LV_OPA_COVER, LV_PART_INDICATOR);
    lv_obj_set_style_arc_rounded(arc, true, LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(arc, LV_OPA_TRANSP, LV_PART_KNOB);
    lv_obj_set_style_pad_all(arc, 0, LV_PART_KNOB);
    if(text && text[0]) {
        int tw = ui_home_text_w(text, UI_HOME_FONT_META);

        ui_home_label(arc, text, UI_HOME_FONT_META, UI_TOK_TEXT, (size - tw) / 2,
                      (size - lv_font_get_line_height(UI_HOME_FONT_META)) / 2);
    }
    return arc;
}

#define TAG_H   20
#define TAG_PAD 8

int ui_kit_tag_w(const char *text)
{
    return ui_home_text_w(text, UI_HOME_FONT_META) + 2 * TAG_PAD;
}

lv_obj_t *ui_kit_tag(lv_obj_t *parent, const char *text, ui_kit_tone_t tone, int x, int y)
{
    lv_obj_t *c = box(parent, x, y, ui_kit_tag_w(text), TAG_H);
    lv_color_t bg, fg;

    switch(tone) {
    case UI_KIT_TONE_INFO:   bg = lv_color_hex(0xF5B942); fg = lv_color_hex(0x2A1C00); break;
    case UI_KIT_TONE_ERROR:  bg = lv_color_hex(0xB3263A); fg = lv_color_hex(0xFFFFFF); break;
    case UI_KIT_TONE_ACCENT: bg = UI_TOK_ACCENT;          fg = UI_TOK_BG;              break;
    default:                 bg = lv_color_hex(0x3A4048); fg = lv_color_hex(0xE6E9ED); break;
    }
    lv_obj_set_style_radius(c, UI_TOK_RADIUS_CHIP, 0);
    lv_obj_set_style_bg_color(c, bg, 0);
    lv_obj_set_style_bg_opa(c, LV_OPA_COVER, 0);
    ui_home_label(c, text, UI_HOME_FONT_META, fg, TAG_PAD,
                  (TAG_H - lv_font_get_line_height(UI_HOME_FONT_META)) / 2);
    return c;
}

static lv_obj_t *stepper(lv_obj_t *parent, int x, int y, int w, const char *value,
                         lv_event_cb_t on_minus, lv_event_cb_t on_plus, bool hold, void *user)
{
    const int h = 34;
    const int bw = 40;
    lv_obj_t *s = box(parent, x, y, w, h);
    lv_obj_t *m = box(s, 0, 0, bw, h);
    lv_obj_t *p = box(s, w - bw, 0, bw, h);
    lv_obj_t *v;
    int lh = lv_font_get_line_height(UI_HOME_FONT_SECTION);
    lv_obj_t *btn[2] = { m, p };
    lv_event_cb_t cb[2] = { on_minus, on_plus };
    const char *glyph[2] = { "-", "+" };
    int i;

    lv_obj_set_style_radius(s, h / 2, 0);
    lv_obj_set_style_bg_color(s, UI_TOK_SURFACE_3, 0);
    lv_obj_set_style_bg_opa(s, LV_OPA_COVER, 0);
    for(i = 0; i < 2; i++) {
        int gw = ui_home_text_w(glyph[i], UI_HOME_FONT_SECTION);

        lv_obj_set_clickable(btn[i], true);
        lv_obj_set_style_radius(btn[i], h / 2, 0);
        lv_obj_set_style_bg_color(btn[i], UI_TOK_TEXT, LV_STATE_PRESSED);
        lv_obj_set_style_bg_opa(btn[i], LV_OPA_30, LV_STATE_PRESSED);
        if(cb[i]) {
            lv_obj_add_event_cb(btn[i], cb[i], LV_EVENT_CLICKED, user);
            if(hold) lv_obj_add_event_cb(btn[i], cb[i], LV_EVENT_LONG_PRESSED_REPEAT, user);
        }
        ui_home_label(btn[i], glyph[i], UI_HOME_FONT_SECTION, UI_TOK_TEXT, (bw - gw) / 2,
                      (h - lh) / 2);
    }
    v = ui_home_label(s, value, UI_HOME_FONT_SECTION, UI_TOK_TEXT, bw, (h - lh) / 2);
    lv_obj_set_width(v, w - 2 * bw);
    lv_obj_set_style_text_align(v, LV_TEXT_ALIGN_CENTER, 0);
    return v;
}

lv_obj_t *ui_kit_stepper(lv_obj_t *parent, int x, int y, int w, const char *value,
                         lv_event_cb_t on_minus, lv_event_cb_t on_plus)
{
    return stepper(parent, x, y, w, value, on_minus, on_plus, false, NULL);
}

lv_obj_t *ui_kit_stepper_hold(lv_obj_t *parent, int x, int y, int w, const char *value,
                              lv_event_cb_t on_minus, lv_event_cb_t on_plus, void *user)
{
    return stepper(parent, x, y, w, value, on_minus, on_plus, true, user);
}

lv_obj_t *ui_kit_text(lv_obj_t *parent, const char *text, const lv_font_t *font,
                      lv_color_t color, int x, int y, int w)
{
    lv_obj_t *l = lv_label_create(parent);

    lv_obj_remove_style_all(l);
    lv_obj_set_clickable(l, false);
    lv_label_set_text(l, text ? text : "");
    lv_label_set_long_mode(l, LV_LABEL_LONG_MODE_WRAP);
    lv_obj_set_width(l, w);
    lv_obj_set_style_text_font(l, font, 0);
    lv_obj_set_style_text_color(l, color, 0);
    lv_obj_set_pos(l, x, y);
    return l;
}

const char *ui_kit_spool(int unit, int slot)
{
    static char buf[32];

    if(mmu_is_this_unit(unit)) vs_spool_label(slot, buf, sizeof buf);
    else                       vs_unit_spool(mmu_unit_name(unit), slot, buf, sizeof buf);
    return buf;
}

void ui_kit_tool(int unit, int slot, char *out, size_t n)
{
    view_tile_t *t = malloc(sizeof *t);

    if(n == 0) return;
    out[0] = '\0';
    if(t && view_tile(&g_mmu, unit, slot, t)) snprintf(out, n, "%s", t->label);
    free(t);
}

/* LVGL's default font (lv_conf.h: LV_FONT_DEFAULT) is ui_font_inter_14. No
 * screen draws with Inter any more, but liblvgl still names it, and the ESP32
 * link only pulls an archive member that something already linked refers to -
 * so this keeps the reference (the linker drops the unused pointer itself). */
const lv_font_t *const ui_kit_default_font_keep = &ui_font_inter_14;

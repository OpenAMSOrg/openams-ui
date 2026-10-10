/**
 * @file ui_unit_header.c
 * The home screen's top band (see ui_unit_header.h).
 *
 * Three zones on one row: the one-line title (family, then config name) on the left, the
 * environment pill, and the alert slot at the right end. The alert slot is
 * always 24x24 of reserved space, so a unit that is fine looks the same as one
 * that is not, just without the icon.
 */

#include "ui/ui_unit_header.h"

#include "ui/ui_home.h"
#include "ui/ui_icons.h"
#include "view/strings.h"

#include <string.h>

/* The alert slot: 24x24, the icon 18 inside it. */
#define SLOT_W   UI_HOME_SLOT_W
#define SLOT_H   UI_HOME_SLOT_H
#define SLOT_Y   ((UI_HOME_HEADER_H - SLOT_H) / 2)

#define MENU_W   40
#define MENU_H   40
#define MENU_X   (UI_HOME_HEADER_PAD - 6)
#define MENU_Y   ((UI_HOME_HEADER_H - MENU_H) / 2)

#define PILL_H   22
#define PILL_Y   ((UI_HOME_HEADER_H - PILL_H) / 2)
#define PILL_PAD 9

/* One line: the family title (20/600), then the config name (12/400, muted)
 * inline after it, on the same baseline. */
#define TITLE_X  (MENU_X + MENU_W + 6)

/* A plain box: no style of its own, so its area is exactly what is asked. */
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

/* The text a pill-sized label needs, measured with the font it will use. */
#define text_w(s, f) ui_home_text_w((s), (f))

/* The environment reading, or - offline, where a reading means nothing - the
 * unit's own status. The core decides which (view_unit_t). */
static void pill_text(const view_unit_t *u, const char **text, lv_color_t *color)
{
    if(!u->online) {
        *text = u->status_text[0] ? u->status_text : view_str(VS_UNIT_OFFLINE);
        *color = UI_TOK_ERROR;
        return;
    }
    *text = u->env.present ? u->env.text : "";
    *color = UI_TOK_PILL_TEXT;
}

static void build_pill(lv_obj_t *parent, const view_unit_t *u,
                       lv_event_cb_t cb, void *user)
{
    const char *text;
    lv_color_t color;
    int w;
    int x;
    lv_obj_t *pill, *l;

    pill_text(u, &text, &color);
    if(!text[0]) return;

    w = text_w(text, UI_HOME_FONT_META) + 2 * PILL_PAD;

    /* The alert slot keeps its place, so the pill ends before it. */
    x = UI_HOME_W - UI_HOME_HEADER_PAD - SLOT_W - 8 - w;

    pill = box(parent, x, PILL_Y, w, PILL_H);
    lv_obj_set_style_bg_color(pill, UI_TOK_PILL_BG, 0);
    lv_obj_set_style_bg_opa(pill, UI_TOK_PILL_BG_OPA, 0);
    lv_obj_set_style_radius(pill, PILL_H / 2, 0);
    /* Tappable like the web's: the reading opens the drying and heating screen. */
    lv_obj_set_style_bg_color(pill, UI_TOK_TEXT, LV_STATE_PRESSED);
    lv_obj_set_style_bg_opa(pill, LV_OPA_30, LV_STATE_PRESSED);
    if(cb) {
        lv_obj_set_clickable(pill, true);
        lv_obj_add_event_cb(pill, cb, LV_EVENT_CLICKED, user);
    }

    l = ui_home_label(pill, text, UI_HOME_FONT_META, color, PILL_PAD, 4);
    lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
}

/* The unit alert slot. Severity is carried by the icon's shape as well as its
 * color (PRINCIPLES.md 6, ruling 2): an octagon for a fault, a triangle for
 * information. The count rides beside it when there is more than one. With
 * nothing alerting in the unit at all (clear) the slot rests on a muted check,
 * and still opens the alerts; when the alert is shown elsewhere (the message
 * row says it) the slot stays empty. */
static void build_alert_slot(lv_obj_t *parent, const view_unit_t *u,
                             lv_event_cb_t cb, void *user)
{
    int x = UI_HOME_W - UI_HOME_HEADER_PAD - SLOT_W;
    lv_obj_t *slot = box(parent, x, SLOT_Y, SLOT_W, SLOT_H);

    if(cb && (u->alert.present || u->alert.clear)) {
        lv_obj_set_clickable(slot, true);
        lv_obj_add_event_cb(slot, cb, LV_EVENT_CLICKED, user);
    }
    if(u->alert.present)    ui_home_alert_mark(slot, u->alert.tone, u->alert.count);
    else if(u->alert.clear) ui_home_alert_none(slot);
}

/* The way to the rest of the screens (the menu): the display has no bottom bar
 * on this screen, so the usual hamburger sits at the top left (UNIFIED_UI 4d);
 * the right end stays the unit alert's slot, as on the web. */
static void build_menu(lv_obj_t *parent, lv_event_cb_t cb, void *user)
{
    lv_obj_t *b = box(parent, MENU_X, MENU_Y, MENU_W, MENU_H);
    lv_obj_t *l;

    if(cb) {
        lv_obj_set_clickable(b, true);
        lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, user);
    }
    lv_obj_set_style_radius(b, 8, 0);
    lv_obj_set_style_bg_color(b, UI_TOK_TEXT, LV_STATE_PRESSED);
    lv_obj_set_style_bg_opa(b, LV_OPA_20, LV_STATE_PRESSED);
    l = ui_home_label(b, UI_ICON_MENU, UI_HOME_FONT_ICON, UI_TOK_TEXT_MUTED,
                      (MENU_W - text_w(UI_ICON_MENU, UI_HOME_FONT_ICON)) / 2,
                      (MENU_H - lv_font_get_line_height(UI_HOME_FONT_ICON)) / 2);
    lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
}

lv_obj_t *ui_unit_header(lv_obj_t *parent, const view_unit_t *u,
                         lv_event_cb_t env_cb, void *env_user,
                         lv_event_cb_t alert_cb, void *alert_user,
                         lv_event_cb_t menu_cb, void *menu_user)
{
    lv_color_t title_col = u->online ? UI_TOK_TEXT : UI_TOK_TEXT_MUTED;

    int lh_t = lv_font_get_line_height(UI_HOME_FONT_TITLE);
    int lh_s = lv_font_get_line_height(UI_HOME_FONT_META);
    int ty = (UI_HOME_HEADER_H - lh_t) / 2;
    int tw = text_w(u->title, UI_HOME_FONT_TITLE);

    ui_home_label(parent, u->title, UI_HOME_FONT_TITLE, title_col, TITLE_X, ty);
    ui_home_label(parent, u->subtitle, UI_HOME_FONT_META, UI_TOK_TEXT_MUTED,
                  TITLE_X + tw + 6, ty + lh_t - lh_s - 3);

    build_pill(parent, u, env_cb, env_user);
    build_alert_slot(parent, u, alert_cb, alert_user);
    build_menu(parent, menu_cb, menu_user);
    return parent;
}

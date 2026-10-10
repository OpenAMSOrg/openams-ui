/**
 * @file ui_toolhead_strip.c
 * The home screen's filament path and message row (see ui_toolhead_strip.h).
 *
 * The message row sits under the header. The strip under the tiles draws the
 * original filament path: a short feed stem under every tile, and from the
 * loaded tile a line in the loaded filament's color, down and left to a small
 * extruder icon with the tool overlaid on it. There is no stepper here: the
 * message row names the current step, and the load screen shows them all.
 *
 * All offsets are fixed (PRINCIPLES.md 2).
 */

#include "ui/ui_toolhead_strip.h"

#include "ui/ui_home.h"
#include "ui/ui_icons.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define ICON_X   UI_HOME_HEADER_PAD
#define ICON_S   38
#define ICON_Y   (PATH_Y + 14)
#define GLYPH_Y  4
#define CHIP_Y   17
#define CHIP_W   24
#define CHIP_H   16

#define STEM_H   8
#define STEM_W   2
#define PATH_Y   (STEM_H + 8)                 /* the horizontal run, from the strip top */
#define LINE_W   3

#define SLOT_W   UI_HOME_SLOT_W
#define SLOT_H   UI_HOME_SLOT_H

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

/* --------------------------------------------------- the hotend and tool */

/* The frame is highlighted in the loaded filament's own color, and the loaded
 * tool sits on the icon as an overlay - never as separate text next to it
 * (UNIFIED_UI.md 4a, "Naming"). */
static void build_hotend(lv_obj_t *parent, const view_toolhead_t *th)
{
    bool loaded = th->tool.present;
    lv_color_t col = (loaded && th->tool.color_known) ? lv_color_hex(th->tool.color)
                                                      : UI_TOK_TEXT_FAINT;
    lv_obj_t *frame = box(parent, ICON_X, ICON_Y, ICON_S, ICON_S);
    lv_obj_t *glyph;
    lv_obj_t *chip;

    lv_obj_set_style_bg_opa(frame, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(frame, 2, 0);
    lv_obj_set_style_border_color(frame, col, 0);
    lv_obj_set_style_border_opa(frame, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(frame, 10, 0);
    /* a dark filament gets a light 1 px outline, outside the frame (4g) */
    lv_obj_set_style_outline_width(frame, 1, 0);
    lv_obj_set_style_outline_pad(frame, 0, 0);
    lv_obj_set_style_outline_color(frame, UI_TOK_TEXT_FAINT, 0);
    lv_obj_set_style_outline_opa(frame, (loaded && th->tool.outline) ? LV_OPA_COVER : LV_OPA_TRANSP, 0);

    glyph = ui_home_label(frame, UI_ICON_PRINTER_3D_NOZZLE, UI_HOME_FONT_ICON,
                          loaded ? UI_TOK_TEXT : UI_TOK_TEXT_FAINT, 10, GLYPH_Y);
    lv_obj_set_style_text_align(glyph, LV_TEXT_ALIGN_CENTER, 0);

    if(!loaded) return;

    /* The tool chip overlays the glyph, so it is the glyph's child (the glyph's
     * box shows overflow): the two are then nested rather than partly covering
     * each other. Frame content is ICON_S - 4 wide; the chip is centered in it. */
    lv_obj_set_overflow_visible(glyph, true);
    chip = box(glyph, (ICON_S - 4 - CHIP_W) / 2 - 10, CHIP_Y - GLYPH_Y - 2, CHIP_W, CHIP_H);
    lv_obj_set_style_bg_color(chip, col, 0);
    lv_obj_set_style_bg_opa(chip, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(chip, UI_TOK_RADIUS_CHIP, 0);
    lv_obj_set_style_outline_width(chip, 1, 0);
    lv_obj_set_style_outline_pad(chip, 0, 0);
    lv_obj_set_style_outline_color(chip, UI_TOK_TEXT_FAINT, 0);
    lv_obj_set_style_outline_opa(chip, th->tool.outline ? LV_OPA_COVER : LV_OPA_TRANSP, 0);

    {
        lv_obj_t *l = ui_home_label(chip, th->tool.label, UI_HOME_FONT_META,
                                    th->tool.ink == VIEW_INK_LIGHT ? UI_TOK_BG : UI_TOK_TEXT,
                                    0, 0);
        lv_obj_set_width(l, CHIP_W);
        lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
    }
}

/* -------------------------------------------------------- the message row */

/* The one line, merged by the core out of an error, a runout, a notice and
 * the plain status. Red and yellow also carry an icon, so the tone is never
 * color alone (PRINCIPLES.md 6, ruling 2). The toolhead's own alert slot sits
 * at its right end. */
lv_obj_t *ui_toolhead_message(lv_obj_t *parent, const view_toolhead_t *th,
                              lv_event_cb_t msg_cb, void *msg_user,
                              lv_event_cb_t alert_cb, void *alert_user)
{
    const view_message_t *m = &th->message;
    const view_activity_t *act = &th->activity;
    const char *text = m->text;
    int x = 0;
    int slot_x = UI_HOME_INNER_W - SLOT_W;
    lv_obj_t *row = box(parent, UI_HOME_HEADER_PAD, UI_HOME_MSG_Y, UI_HOME_INNER_W, UI_HOME_MSG_H);
    lv_obj_t *msg = box(row, 0, 0, slot_x - 4, UI_HOME_MSG_H);
    lv_obj_t *slot = box(row, slot_x, 0, SLOT_W, SLOT_H);

    /* During a load or unload the row names the current step (the view's
     * activity has it); the web shows the steps in its own stepper. */
    if(m->tone == VIEW_TONE_NEUTRAL &&
       (act->kind == VIEW_ACTIVITY_LOADING || act->kind == VIEW_ACTIVITY_UNLOADING) &&
       act->index >= 0 && act->index < act->step_count && act->steps[act->index][0])
        text = act->steps[act->index];

    if(text[0]) {
        lv_obj_t *l;

        if(msg_cb) {
            lv_obj_set_clickable(msg, true);
            lv_obj_add_event_cb(msg, msg_cb, LV_EVENT_CLICKED, msg_user);
        }
        if(m->tone != VIEW_TONE_NEUTRAL) {
            ui_home_label(msg, m->tone == VIEW_TONE_ERROR ? UI_HOME_ICON_ERROR
                                                          : UI_HOME_ICON_INFO,
                          UI_HOME_FONT_SEV, ui_home_tone_color(m->tone), 0,
                          (UI_HOME_MSG_H - 15) / 2);
            x = 22;
        }
        l = ui_home_label_fit(msg, text, UI_HOME_FONT_BODY, ui_home_tone_color(m->tone),
                              x, (UI_HOME_MSG_H - lv_font_get_line_height(UI_HOME_FONT_BODY)) / 2,
                              slot_x - 4 - x);
        lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_LEFT, 0);
    }

    if(alert_cb && (th->alert.present || th->alert.clear)) {
        lv_obj_set_clickable(slot, true);
        lv_obj_add_event_cb(slot, alert_cb, LV_EVENT_CLICKED, alert_user);
    }
    /* The check only when nothing in the lane alerts at all: an alert the row
     * already says leaves the slot empty, not "No alerts". */
    if(th->alert.present)    ui_home_alert_mark(slot, th->alert.tone, th->alert.count);
    else if(th->alert.clear) ui_home_alert_none(slot);
    return row;
}

/* -------------------------------------------------------------- the path */

static lv_obj_t *bar(lv_obj_t *parent, int x, int y, int w, int h, lv_color_t c)
{
    lv_obj_t *o = box(parent, x, y, w, h);

    lv_obj_set_style_bg_color(o, c, 0);
    lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
    return o;
}

static int tile_cx(int col)
{
    return UI_HOME_TILE_X(col) + UI_HOME_TILE_W / 2;
}

/* A stem under each tile; the loaded tile's stem, and the run from it to the
 * extruder, are drawn in the loaded filament's color. The pieces butt against
 * each other rather than overlap. */
static void build_path(lv_obj_t *strip, const view_toolhead_t *th, int bays, int loaded_col)
{
    lv_color_t col = (th->tool.present && th->tool.color_known) ? lv_color_hex(th->tool.color)
                                                                : UI_TOK_TEXT_FAINT;
    int i;
    int fx = ICON_X + ICON_S / 2;              /* the extruder's center line */

    for(i = 0; i < bays; i++) {
        if(i == loaded_col) continue;
        bar(strip, tile_cx(i) - STEM_W / 2, 0, STEM_W, STEM_H, UI_TOK_SURFACE_3);
    }

    if(loaded_col < 0 || loaded_col >= bays) return;
    {
        int cx = tile_cx(loaded_col);
        lv_color_t oc = UI_TOK_TEXT_FAINT;
        bool ol = th->tool.present && th->tool.color_known && th->tool.outline;
        lv_obj_t *o;

        /* The outline of a dark filament: the same three runs, one pixel
         * larger all round, under them (so no line crosses a joint). Always
         * built, transparent when the color needs none, so a color change
         * never changes the shape. The top edge meets the tile and stays. */
        o = bar(strip, cx - LINE_W / 2 - 1, 0, LINE_W + 2, PATH_Y + 1, oc);
        lv_obj_set_style_bg_opa(o, ol ? LV_OPA_COVER : LV_OPA_TRANSP, 0);
        o = bar(strip, fx - LINE_W / 2 - 1, PATH_Y - 1, cx - fx + LINE_W + 2, LINE_W + 2, oc);
        lv_obj_set_style_bg_opa(o, ol ? LV_OPA_COVER : LV_OPA_TRANSP, 0);
        o = bar(strip, fx - LINE_W / 2 - 1, PATH_Y + LINE_W - 1, LINE_W + 2,
                ICON_Y - PATH_Y - LINE_W + 2, oc);
        lv_obj_set_style_bg_opa(o, ol ? LV_OPA_COVER : LV_OPA_TRANSP, 0);

        bar(strip, cx - LINE_W / 2, 0, LINE_W, PATH_Y, col);
        bar(strip, fx - LINE_W / 2, PATH_Y, cx - fx + LINE_W, LINE_W, col);
        bar(strip, fx - LINE_W / 2, PATH_Y + LINE_W, LINE_W, ICON_Y - PATH_Y - LINE_W, col);
    }
}

int ui_toolhead_rest_x(void)
{
    return ICON_X + ICON_S + 12;
}

int ui_toolhead_rest_w(int pages)
{
    /* up to the paging chevrons when there are some, else to the screen's margin */
    int end = (pages > 1) ? UI_HOME_CHEVRON_X(0) - 8 : UI_HOME_W - UI_HOME_HEADER_PAD;

    return end - ui_toolhead_rest_x();
}

/* The toolhead's path in words, beside the extruder: the same line the panel's
 * stepper slot rests on (activity.rest), one line, muted. Always built, empty
 * while a plan runs (the message row names the step then), so the strip's
 * shape never depends on it and the home screen updates it in place. */
static void build_rest(lv_obj_t *strip, const view_toolhead_t *th, int pages)
{
    const lv_font_t *font = UI_HOME_FONT_META;

    ui_home_label_fit(strip, th->activity.rest.present ? th->activity.rest.label : "", font,
                      UI_TOK_TEXT_MUTED, ui_toolhead_rest_x(),
                      ICON_Y + (ICON_S - lv_font_get_line_height(font)) / 2,
                      ui_toolhead_rest_w(pages));
}

lv_obj_t *ui_toolhead_strip(lv_obj_t *parent, const view_toolhead_t *th, int bays, int loaded_col,
                            int pages)
{
    lv_obj_t *strip = box(parent, 0, UI_HOME_STRIP_Y, UI_HOME_W, UI_HOME_STRIP_H);

    build_path(strip, th, bays, loaded_col);
    build_hotend(strip, th);
    build_rest(strip, th, pages);
    return strip;
}

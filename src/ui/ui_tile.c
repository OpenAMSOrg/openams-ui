/**
 * @file ui_tile.c
 * The lane tile (see ui_tile.h).
 *
 * The anatomy is fixed and the fill is data: a two-stop vertical gradient of
 * the core's own gradient_top/gradient_bottom (UNIFIED_UI.md 5, "The tile
 * fill is data, not theme"), a flat dimmed surface for the one disabled look,
 * and the 8x8 hatch over the two states that mark "no spool" (4c).
 *
 * The status tag is a child of the ring, not a sibling: it is drawn on top of
 * the ring and hides the percentage (4b), and nesting it keeps the two from
 * being two siblings covering each other.
 */

#include "ui/ui_tile.h"

#include "model/mmu_model.h"
#include "ui/ui_home.h"
#include "ui/ui_icons.h"

#include <stdio.h>
#include <string.h>

/* Tile-local geometry. The design size is 108x150; the ring is anchored to the
 * bottom, and the rows above it stack from the measured line heights, so the
 * anatomy survives a caller asking for a different height. */
#define PAD_X      10
#define PAD_TOP    6
#define PAD_BOT    6
#define BORDER     2
#define ROW_GAP    2

#define RING_S     52
#define RING_STROKE 4

#define BADGE_H    22
#define BADGE_PAD  6

#define TAG_H      20
#define TAG_PAD    7

/* U+221E, as a byte string so the source stays plain ASCII. */
#define TILE_INF_MARK "\xE2\x88\x9E"

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

/* The ink the tile's own text draws in: the core's choice, made from the
 * filament color, or the muted gray of the disabled look. */
static lv_color_t ink_of(const view_tile_t *t, bool dim)
{
    if(dim) return UI_TOK_TEXT_MUTED;
    return t->ink == VIEW_INK_LIGHT ? UI_TOK_TEXT : UI_TOK_BG;
}

/* ------------------------------------------------------------- the frame */

static lv_obj_t *tile_frame(lv_obj_t *parent, const view_tile_t *t, int x, int y,
                            int w, int h, lv_event_cb_t cb, void *user)
{
    lv_obj_t *tile = lv_button_create(parent);

    lv_obj_remove_style_all(tile);
    lv_obj_set_scrollable(tile, false);
    lv_obj_set_pos(tile, x, y);
    lv_obj_set_size(tile, w, h);
    lv_obj_set_style_radius(tile, UI_TOK_RADIUS_TILE, 0);
    lv_obj_set_style_pad_all(tile, 0, 0);
    lv_obj_set_style_border_width(tile, BORDER, 0);
    lv_obj_set_style_border_opa(tile, LV_OPA_TRANSP, 0);

    if(t->dim || !t->color_known) {
        /* the one disabled look, and a spool whose color nobody told us: the
         * neutral surface, never a black fill that reads as black filament */
        lv_obj_set_style_bg_color(tile, UI_TOK_SURFACE_2, 0);
        lv_obj_set_style_bg_opa(tile, LV_OPA_COVER, 0);
    }
    else {
        /* Two stops, top to bottom; the core has already mixed them. */
        lv_obj_set_style_bg_color(tile, lv_color_hex(t->gradient_top), 0);
        lv_obj_set_style_bg_grad_color(tile, lv_color_hex(t->gradient_bottom), 0);
        lv_obj_set_style_bg_grad_dir(tile, LV_GRAD_DIR_VER, 0);
        lv_obj_set_style_bg_opa(tile, LV_OPA_COVER, 0);
    }

    /* Loaded is a 2 px accent ring plus one static rounded rect at opa 40
     * outside it; a fault is the same ring in red (7, LVGL rules). */
    if(t->state == VIEW_TILE_LOADED) {
        lv_obj_set_style_border_color(tile, UI_TOK_ACCENT, 0);
        lv_obj_set_style_border_opa(tile, LV_OPA_COVER, 0);
    }
    else if(t->state == VIEW_TILE_ERROR) {
        lv_obj_set_style_border_color(tile, UI_TOK_ERROR, 0);
        lv_obj_set_style_border_opa(tile, LV_OPA_COVER, 0);
    }
    else if(cb) {
        lv_obj_set_style_border_color(tile, UI_TOK_TEXT_MUTED, LV_STATE_PRESSED);
        lv_obj_set_style_border_opa(tile, LV_OPA_COVER, LV_STATE_PRESSED);
    }

    if(cb) lv_obj_add_event_cb(tile, cb, LV_EVENT_CLICKED, user);
    return tile;
}

/* The one rounded rect that sits outside a loaded tile's ring. It is a
 * sibling that fully contains the tile, created before it so it draws behind. */
static void loaded_glow(lv_obj_t *parent, int x, int y, int w, int h)
{
    lv_obj_t *glow = box(parent, x - BORDER, y - BORDER, w + 2 * BORDER, h + 2 * BORDER);

    lv_obj_set_style_bg_color(glow, UI_TOK_ACCENT, 0);
    lv_obj_set_style_bg_opa(glow, LV_OPA_40, 0);
    lv_obj_set_style_radius(glow, UI_TOK_RADIUS_TILE + BORDER, 0);
}

/* ------------------------------------------------------ the top two rows */

/* The spare badge: the group's fallback rank, or a bare mark on the loaded
 * member (4, "Spare order"). Mono on a dark chip (7, SpareBadge). */
static void build_spare(lv_obj_t *tile, const view_tile_t *t, int x)
{
    char text[8];
    int bw;
    lv_obj_t *chip, *l;

    if(!t->spare.present) return;

    if(t->spare.bare) snprintf(text, sizeof text, "%s", TILE_INF_MARK);
    else              snprintf(text, sizeof text, "%s%d", TILE_INF_MARK, t->spare.position);

    bw = ui_home_text_w(text, UI_HOME_FONT_MONO) + 2 * BADGE_PAD;

    chip = box(tile, x, PAD_TOP, bw, BADGE_H);
    lv_obj_set_style_bg_color(chip, UI_TOK_BG, 0);
    lv_obj_set_style_bg_opa(chip, LV_OPA_60, 0);
    lv_obj_set_style_radius(chip, UI_TOK_RADIUS_CHIP, 0);

    l = ui_home_label(chip, text, UI_HOME_FONT_MONO,
                      t->dim ? UI_TOK_TEXT_MUTED : UI_TOK_TEXT, 0,
                      (BADGE_H - lv_font_get_line_height(UI_HOME_FONT_MONO)) / 2);
    lv_obj_set_width(l, bw);                    /* the label spans the chip, so CENTER centers it */
    lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
}

/* The RFID mark, in the tile's upper-right corner. */
static void build_rfid(lv_obj_t *tile, const view_tile_t *t, int w)
{
    int gw = ui_home_text_w(UI_ICON_NFC_VARIANT, UI_HOME_FONT_ICON);
    lv_obj_t *l;

    if(!t->rfid) return;

    l = ui_home_label(tile, UI_ICON_NFC_VARIANT, UI_HOME_FONT_ICON,
                      t->dim ? UI_TOK_TEXT_FAINT : ink_of(t, t->dim), w - PAD_X - gw,
                      PAD_TOP + 2);
    lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_RIGHT, 0);
}

/* ------------------------------------------------- the name and its meta */

/* "PLA Basic" over "Bambu · 1.2 kg". The core has already cut both to fit;
 * this only keeps a long one inside the tile. The meta row is the first thing
 * to go when the box is short (4a, "content drops in this order"). */
static void build_name_rows(lv_obj_t *tile, const view_tile_t *t, int w, int y, int avail)
{
    int content_w = w - 2 * PAD_X;
    int lh_name = lv_font_get_line_height(UI_HOME_FONT_BODY);
    int lh_meta = lv_font_get_line_height(UI_HOME_FONT_META);
    const char *name = t->name[0] ? t->name : t->material;
    char meta[VIEW_MATERIAL_LEN * 2 + 4];
    lv_color_t col = ink_of(t, t->dim);

    if(name[0]) {
        ui_home_label_fit(tile, name, UI_HOME_FONT_BODY, col, PAD_X, y, content_w);
        y += lh_name + 1;
    }

    meta[0] = '\0';
    if(t->brand[0]) snprintf(meta, sizeof meta, "%s", t->brand);
    if(t->grams_text[0]) {
        size_t n = strlen(meta);

        snprintf(meta + n, sizeof meta - n, "%s%s", n ? " · " : "", t->grams_text);
    }
    if(!meta[0] || y + lh_meta > avail) return;

    /* The meta is support text: the same ink, held back. */
    {
        lv_obj_t *l = ui_home_label_fit(tile, meta, UI_HOME_FONT_META, col, PAD_X, y,
                                         content_w);
        lv_obj_set_style_opa(l, LV_OPA_70, 0);
    }
}

/* The tag chips, as on the web (StatusTag.vue): solid, and every pair is
 * above 4.5:1 - white on #B3263A 6.4, #2A1C00 on #F5B942 9.0, #E6E9ED on
 * #3A4048 8.1 (PRINCIPLES.md 12). */
static lv_color_t tag_bg(int tone)
{
    switch(tone) {
    case VIEW_TONE_ERROR: return lv_color_hex(0xB3263A);
    case VIEW_TONE_INFO:  return lv_color_hex(0xF5B942);
    default:              return lv_color_hex(0x3A4048);
    }
}

static lv_color_t tag_fg(int tone)
{
    switch(tone) {
    case VIEW_TONE_ERROR: return lv_color_hex(0xFFFFFF);
    case VIEW_TONE_INFO:  return lv_color_hex(0x2A1C00);
    default:              return lv_color_hex(0xE6E9ED);
    }
}

/* --------------------------------------------------------- the ring + tag */

/* The 52 px ring at the tile's bottom center, with either the percentage or
 * the status tag in its middle. The tag wins: 4b has it hiding the
 * percentage, and the ring's own arc still shows what is left. */
static void build_ring(lv_obj_t *tile, const view_tile_t *t, int w, int h)
{
    int cw = w - 2 * BORDER;                /* the content width inside the border */
    int rx = (cw - RING_S) / 2;             /* where the ring's circle starts */
    int ry = h - PAD_BOT - RING_S;
    bool dim = t->dim;
    lv_color_t on = ink_of(t, dim);
    lv_color_t track = dim ? UI_TOK_SURFACE_3 : on;
    lv_color_t ind = (t->pct >= 0 && t->low) ? UI_TOK_WARN
                                              : (dim ? UI_TOK_TEXT_MUTED : on);
    int sweep = 0;
    /* A tile-wide slot holds the ring; the tag chip (wider than the ring) is the
     * slot's sibling and lies inside it, so no two boxes cover each other partly. */
    lv_obj_t *slot = box(tile, 0, ry, cw, RING_S);
    lv_obj_t *arc = lv_arc_create(slot);

    lv_obj_remove_style_all(arc);
    lv_obj_set_clickable(arc, false);
    lv_obj_set_scrollable(arc, false);
    lv_obj_set_pos(arc, rx, 0);
    lv_obj_set_size(arc, RING_S, RING_S);

    lv_arc_set_rotation(arc, 270);        /* 0 deg at 12 o'clock */
    lv_arc_set_bg_angles(arc, 0, 360);
    lv_arc_set_angles(arc, 0, 0);

    lv_obj_set_style_arc_width(arc, RING_STROKE, 0);
    lv_obj_set_style_arc_color(arc, track, 0);
    lv_obj_set_style_arc_opa(arc, dim ? LV_OPA_COVER : LV_OPA_70, 0);

    lv_obj_set_style_arc_width(arc, RING_STROKE, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(arc, ind, LV_PART_INDICATOR);
    lv_obj_set_style_arc_opa(arc, LV_OPA_COVER, LV_PART_INDICATOR);
    lv_obj_set_style_arc_rounded(arc, true, LV_PART_INDICATOR);

    lv_obj_set_style_bg_opa(arc, LV_OPA_TRANSP, LV_PART_KNOB);
    lv_obj_set_style_pad_all(arc, 0, LV_PART_KNOB);

    if(t->pct > 0) {
        sweep = (t->pct * 360 + 50) / 100;
        if(sweep < 8) sweep = 8;
        if(sweep > 360) sweep = 360;
    }
    lv_arc_set_angles(arc, 0, sweep);

    if(!t->has_tag) {
        lv_obj_t *l = ui_home_label(arc, t->pct_text, UI_HOME_FONT_MONO,
                                    t->pct >= 0 ? ind : UI_TOK_TEXT_FAINT,
                                    (RING_S - ui_home_text_w(t->pct_text, UI_HOME_FONT_MONO)) / 2,
                                    (RING_S - lv_font_get_line_height(UI_HOME_FONT_MONO)) / 2);
        lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
        return;
    }

    {
        /* The tag is a solid chip in the web's tag tones (a fill of the tile's
         * own color failed the contrast floor on a filament-colored tile). The chip is
         * the tile's child, not the arc's: it may be wider than the ring, but
         * never wider than the tile (its box is inside the ring slot's). */
        int tw = ui_home_text_w(t->tag, UI_HOME_FONT_BODY) + 2 * TAG_PAD;
        lv_obj_t *chip;
        lv_obj_t *l;

        if(tw > cw - 4) tw = cw - 4;
        chip = box(tile, (cw - tw) / 2, ry + (RING_S - TAG_H) / 2, tw, TAG_H);
        lv_obj_set_style_bg_color(chip, tag_bg(t->tag_tone), 0);
        lv_obj_set_style_bg_opa(chip, LV_OPA_COVER, 0);
        lv_obj_set_style_radius(chip, UI_TOK_RADIUS_CHIP, 0);

        l = ui_home_label(chip, t->tag, UI_HOME_FONT_BODY, tag_fg(t->tag_tone),
                          0, (TAG_H - lv_font_get_line_height(UI_HOME_FONT_BODY)) / 2);
        lv_obj_set_width(l, tw);
        lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
    }
}

/* ------------------------------------------------------------------ tile */

lv_obj_t *ui_tile_bay(lv_obj_t *parent, const view_tile_t *t, int x, int y, int w, int h,
                      lv_event_cb_t cb, void *user)
{
    lv_obj_t *tile;
    lv_color_t on = ink_of(t, t->dim);
    int lh_label = lv_font_get_line_height(UI_HOME_FONT_LABEL);
    int lh_sub = lv_font_get_line_height(UI_HOME_FONT_META);
    int y_row = PAD_TOP;
    int content_w = w - 2 * PAD_X;
    int ring_top = h - PAD_BOT - RING_S;

    if(t->state == VIEW_TILE_LOADED) loaded_glow(parent, x, y, w, h);

    tile = tile_frame(parent, t, x, y, w, h, cb, user);

    /* The hatch goes down first, so it is behind everything else in the tile. */
    if(t->hatched) ui_home_hatch(tile, w - 2 * BORDER, h - 2 * BORDER);

    /* The tool, or "?" for a bay no group has claimed (4, "Tile label"). */
    ui_home_label_fit(tile, t->label, UI_HOME_FONT_LABEL, on, PAD_X, y_row, content_w);
    /* The spare badge sits next to the tool label, as on the web. */
    build_spare(tile, t, PAD_X + ui_home_text_w(t->label, UI_HOME_FONT_LABEL) + 6);
    y_row += lh_label + 1;

    if(t->has_sublabel) {
        ui_home_label_fit(tile, t->sublabel, UI_HOME_FONT_META,
                          t->dim ? UI_TOK_TEXT_FAINT : UI_TOK_TEXT_MUTED,
                          PAD_X, y_row, content_w);
    }
    y_row += lh_sub + ROW_GAP;

    build_name_rows(tile, t, w, y_row, ring_top - 2);
    build_rfid(tile, t, w);
    build_ring(tile, t, w, h);
    return tile;
}

lv_obj_t *ui_tile_slot(lv_obj_t *parent, int unit, int slot,
                       int x, int y, int w, int h,
                       lv_event_cb_t cb, void *user)
{
    view_tile_t t;

    if(!view_tile(&g_mmu, unit, slot, &t)) return NULL;
    return ui_tile_bay(parent, &t, x, y, w, h, cb, user);
}

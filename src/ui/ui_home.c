/**
 * @file ui_home.c
 * The restyled home screen's shared drawing helpers (see ui_home.h).
 */

#include "ui/ui_home.h"

#include "view/view.h"

#include <stdio.h>
#include <string.h>

/* ----------------------------------------------------------------- label */

/** How wide @p s draws in @p font. */
int ui_home_text_w(const char *s, const lv_font_t *font)
{
    lv_point_t p;

    lv_text_get_size(&p, s, font, 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
    return (int)p.x;
}

lv_obj_t *ui_home_label(lv_obj_t *parent, const char *text, const lv_font_t *font,
                        lv_color_t color, int x, int y)
{
    lv_obj_t *l = lv_label_create(parent);

    lv_obj_remove_style_all(l);
    lv_obj_set_clickable(l, false);
    lv_obj_set_scrollable(l, false);
    lv_label_set_text(l, text ? text : "");
    lv_label_set_long_mode(l, LV_LABEL_LONG_MODE_CLIP);
    lv_obj_set_style_text_font(l, font, 0);
    lv_obj_set_style_text_color(l, color, 0);
    lv_obj_set_pos(l, x, y);
    return l;
}

lv_obj_t *ui_home_label_fit(lv_obj_t *parent, const char *text, const lv_font_t *font,
                            lv_color_t color, int x, int y, int max_w)
{
    char buf[128];
    size_t n;

    snprintf(buf, sizeof buf, "%s", text ? text : "");
    n = strlen(buf);

    if(ui_home_text_w(buf, font) > max_w) {
        /* Too long: drop characters until the text and an ellipsis fit. */
        while(n > 1) {
            n--;
            /* Never cut a UTF-8 sequence in half: a dropped lead byte would
             * leave a stray continuation byte for the font to draw as a blank. */
            while(n > 0 && ((unsigned char)buf[n] & 0xC0) == 0x80) n--;
            buf[n] = '\0';
            if(ui_home_text_w(buf, font) + ui_home_text_w("\xE2\x80\xA6", font) <= max_w) break;
        }
        if(n + 4 < sizeof buf) memcpy(buf + n, "\xE2\x80\xA6", 4);
    }
    return ui_home_label(parent, buf, font, color, x, y);
}

/* ------------------------------------------------------------------ tone */
lv_color_t ui_home_tone_color(int tone)
{
    switch(tone) {
    case VIEW_TONE_ERROR: return UI_TOK_ERROR;
    case VIEW_TONE_INFO:  return UI_TOK_WARN;
    default:              return UI_TOK_TEXT_MUTED;
    }
}

/* ----------------------------------------------------------------- hatch */

/* The diagonal stripe as an A8 bitmap one screen wide and one period tall:
 * the stripe alpha where the stripe is, zero elsewhere. 3.8 KB of const
 * flash, drawn as a tiled lv_image rather than as lines (docs/design/
 * UNIFIED_UI.md 7, LVGL rules). LVGL draws a tiled image one tile at a time,
 * and an 8x8 tile cost a hatched card a few hundred tile draws per frame on
 * the ESP32 (about three quarters of home's redraw); a screen-wide tile is one
 * per row. Tiling starts at the box's left edge either way, so the pixels are
 * the same. The rows follow design/tokens.json's hatch block - period 8,
 * stripe 2, alpha 16% - and have to be redrawn by hand if that block changes. */
#define HATCH_X4(...)  __VA_ARGS__, __VA_ARGS__, __VA_ARGS__, __VA_ARGS__
#define HATCH_X20(...) HATCH_X4(__VA_ARGS__), HATCH_X4(__VA_ARGS__), HATCH_X4(__VA_ARGS__), \
                       HATCH_X4(__VA_ARGS__), HATCH_X4(__VA_ARGS__)
#define HATCH_X60(...) HATCH_X20(__VA_ARGS__), HATCH_X20(__VA_ARGS__), HATCH_X20(__VA_ARGS__)
#if defined(MMU_UI_800X480)
/* The same stripe on the 1.5x grid (ui_scale.h): period 12, stripe 3, three
 * pixels right per row; 67 periods cover the 800 px screen. */
#if UI_TOK_HATCH_PERIOD != 12 || UI_TOK_HATCH_STRIPE != 3
#error "the hatch rows below are drawn for period 12, stripe 3" /* lit-ok */
#endif
#define HATCH_ROW0 41, 41, 41,  0,  0,  0,  0,  0,  0,  0,  0,  0
#define HATCH_ROW1  0,  0,  0, 41, 41, 41,  0,  0,  0,  0,  0,  0
#define HATCH_ROW2  0,  0,  0,  0,  0,  0, 41, 41, 41,  0,  0,  0
#define HATCH_ROW3  0,  0,  0,  0,  0,  0,  0,  0,  0, 41, 41, 41
#define HATCH_ACROSS(...) HATCH_X60(__VA_ARGS__), HATCH_X4(__VA_ARGS__), \
                          __VA_ARGS__, __VA_ARGS__, __VA_ARGS__
#define HATCH_W (67 * UI_TOK_HATCH_PERIOD)
#else
#if UI_TOK_HATCH_PERIOD != 8 || UI_TOK_HATCH_STRIPE != 2
#error "the hatch rows below are drawn for period 8, stripe 2" /* lit-ok */
#endif
#define HATCH_ROW0 41, 41,  0,  0,  0,  0,  0,  0
#define HATCH_ROW1  0,  0, 41, 41,  0,  0,  0,  0
#define HATCH_ROW2  0,  0,  0,  0, 41, 41,  0,  0
#define HATCH_ROW3  0,  0,  0,  0,  0,  0, 41, 41
#define HATCH_ACROSS(...) HATCH_X60(__VA_ARGS__)
#define HATCH_W (60 * UI_TOK_HATCH_PERIOD)
#endif
#define HATCH_FOUR_ROWS HATCH_ACROSS(HATCH_ROW0), HATCH_ACROSS(HATCH_ROW1), \
                        HATCH_ACROSS(HATCH_ROW2), HATCH_ACROSS(HATCH_ROW3)

static const uint8_t s_hatch_buf[HATCH_W * UI_TOK_HATCH_PERIOD] = {
    /* the stripe steps one stripe width right per row, so it is seamless on
     * both axes: four rows, repeated down to the period */
    HATCH_FOUR_ROWS, HATCH_FOUR_ROWS,
#if defined(MMU_UI_800X480)
    HATCH_FOUR_ROWS,
#endif
};

static const lv_image_dsc_t s_hatch_dsc = {
    .header = {
        .magic = LV_IMAGE_HEADER_MAGIC,
        .cf = LV_COLOR_FORMAT_A8,
        .w = HATCH_W,
        .h = UI_TOK_HATCH_PERIOD,
        .stride = HATCH_W,
    },
    .data_size = sizeof(s_hatch_buf),
    .data = s_hatch_buf,
};

/* Draws one band of the hatch: the image task is limited to the band, while
 * image_area keeps the whole box, so the tiles and the rounded clip line up
 * as if the box were drawn in one piece. */
static void hatch_draw_band(lv_layer_t *layer, lv_draw_image_dsc_t *dsc, const lv_area_t *clip,
                            int32_t x1, int32_t y1, int32_t x2, int32_t y2, int32_t radius)
{
    lv_area_t a = { LV_MAX(x1, clip->x1), LV_MAX(y1, clip->y1),
                    LV_MIN(x2, clip->x2), LV_MIN(y2, clip->y2) };

    if(a.x1 > a.x2 || a.y1 > a.y2) return;
    dsc->clip_radius = radius;
    layer->_clip_area = a;
    lv_draw_image(layer, dsc, &a);
}

/* The hatch is drawn here rather than as the object's bg_image. LVGL clips a
 * tiled image with a rounded box pixel by pixel through its slowest path, and
 * on the ESP32 that was most of home's and the edit screen's redraw. Only the
 * four r x r corners need the rounded clip; the rest of the box goes through
 * the plain A8 blend, which paints the same pixels many times faster. The
 * descriptor is the one lv_draw_rect builds for a tiled bg_image. */
static void on_hatch_draw(lv_event_t *e)
{
    lv_obj_t *o = lv_event_get_current_target(e);
    lv_layer_t *layer = lv_event_get_layer(e);
    const lv_area_t clip = layer->_clip_area;
    lv_draw_image_dsc_t dsc;
    lv_area_t box;
    int32_t r;

    if(layer->opa <= LV_OPA_MIN) return;
    lv_obj_get_coords(o, &box);
    r = lv_obj_get_style_radius(o, LV_PART_MAIN);
    r = LV_MIN(r, LV_MIN(lv_area_get_width(&box), lv_area_get_height(&box)) >> 1);

    lv_draw_image_dsc_init(&dsc);
    dsc.base.obj = o;
    dsc.base.part = LV_PART_MAIN;
    dsc.base.layer = layer;
    dsc.src = &s_hatch_dsc;
    dsc.opa = layer->opa;
    /* A8 carries no color of its own, so the stripe takes the text color. */
    dsc.recolor = UI_TOK_TEXT;
    dsc.recolor_opa = LV_OPA_COVER;
    dsc.tile = 1;
    dsc.image_area = box;

    /* the middle rows, full width */
    hatch_draw_band(layer, &dsc, &clip, box.x1, box.y1 + r, box.x2, box.y2 - r, 0);
    /* the top and bottom rows between the corners */
    hatch_draw_band(layer, &dsc, &clip, box.x1 + r, box.y1, box.x2 - r, box.y1 + r - 1, 0);
    hatch_draw_band(layer, &dsc, &clip, box.x1 + r, box.y2 - r + 1, box.x2 - r, box.y2, 0);
    /* the four corners, clipped round */
    hatch_draw_band(layer, &dsc, &clip, box.x1, box.y1, box.x1 + r - 1, box.y1 + r - 1, r);
    hatch_draw_band(layer, &dsc, &clip, box.x2 - r + 1, box.y1, box.x2, box.y1 + r - 1, r);
    hatch_draw_band(layer, &dsc, &clip, box.x1, box.y2 - r + 1, box.x1 + r - 1, box.y2, r);
    hatch_draw_band(layer, &dsc, &clip, box.x2 - r + 1, box.y2 - r + 1, box.x2, box.y2, r);

    layer->_clip_area = clip;
}

lv_obj_t *ui_home_hatch(lv_obj_t *parent, int w, int h)
{
    /* The stripe tiled over the whole box and clipped to the tile's rounded
     * corners (the object's own radius), drawn by on_hatch_draw. */
    lv_obj_t *o = lv_obj_create(parent);

    lv_obj_remove_style_all(o);
    lv_obj_set_clickable(o, false);
    lv_obj_set_scrollable(o, false);
    lv_obj_set_pos(o, 0, 0);
    lv_obj_set_size(o, w, h);
    lv_obj_set_style_radius(o, UI_TOK_RADIUS_TILE, 0);
    lv_obj_set_style_bg_opa(o, LV_OPA_TRANSP, 0);
    lv_obj_add_event_cb(o, on_hatch_draw, LV_EVENT_DRAW_MAIN, NULL);
    return o;
}

void ui_home_alert_none(lv_obj_t *slot)
{
    ui_home_label(slot, UI_ICON_CHECK, UI_HOME_FONT_ICON, UI_TOK_TEXT_FAINT, UI_PX(3),
                  (UI_HOME_SLOT_H - lv_font_get_line_height(UI_HOME_FONT_ICON)) / 2);
}

void ui_home_alert_mark(lv_obj_t *slot, int tone, int count)
{
    char text[16];
    lv_color_t c = ui_home_tone_color(tone);

    ui_home_label(slot, tone == VIEW_TONE_ERROR ? UI_HOME_ICON_ERROR : UI_HOME_ICON_INFO,
                  UI_HOME_FONT_SEV, c, UI_PX(3), UI_PX(3));
    snprintf(text, sizeof text, "%d", count > 99 ? 99 : count);
    ui_home_label(slot, text, UI_HOME_FONT_BODY, c, UI_PX(24),
                  (UI_HOME_SLOT_H - lv_font_get_line_height(UI_HOME_FONT_BODY)) / 2);
}

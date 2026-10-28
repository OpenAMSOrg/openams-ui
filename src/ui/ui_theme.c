/**
 * @file ui_theme.c
 * Builds the shared styles once. See docs/DESIGN.md for the intent behind them.
 *
 * Note on padding: LVGL interprets a child's position relative to its parent's
 * *content* area, so a padding on a card moves every absolutely placed child.
 * The screens position their children explicitly, so the card styles keep
 * pad_all 0 and each layout owns its own insets through the UI_PAD_* tokens.
 */

#include "ui/ui_theme.h"

#include "model/mmu_model.h"

static lv_style_t s_styles[UI_STYLE_COUNT];
static lv_style_t s_pressed[UI_STYLE_COUNT];
static lv_style_t s_disabled[UI_STYLE_COUNT];
static bool s_state_ready[UI_STYLE_COUNT];
static bool s_ready;

/* Background color of a button changes on press, so both the color and the
 * opacity animate over 100 ms: instant state changes look cheap. */
static const lv_style_prop_t s_trans_props[] = { LV_STYLE_BG_COLOR, LV_STYLE_BG_OPA, LV_STYLE_OPA,
                                                 0 };
static lv_style_transition_dsc_t s_trans_btn;

lv_style_t *ui_style(ui_style_id_t id)
{
    if(id < 0 || id >= UI_STYLE_COUNT) return &s_styles[UI_STYLE_LABEL];
    return &s_styles[id];
}

lv_style_t *ui_style_state(ui_style_id_t id, ui_state_style_t which)
{
    if(id < 0 || id >= UI_STYLE_COUNT || !s_state_ready[id]) return NULL;
    return which == UI_STATE_STYLE_PRESSED ? &s_pressed[id] : &s_disabled[id];
}

lv_color_t ui_readable_text(uint32_t rgb)
{
    /* Rec. 601 luma, integer only. */
    uint32_t r = (rgb >> 16) & 0xFF;
    uint32_t g = (rgb >> 8) & 0xFF;
    uint32_t b = rgb & 0xFF;
    uint32_t luma = (r * 299 + g * 587 + b * 114) / 1000;

    return luma > 150 ? UI_COL_DARK_TEXT : UI_COL_TEXT;
}

/* ------------------------------------------------------- on-filament color */

/* sRGB linearisation, round(lin(c/255) * 10000), one entry per 8-bit channel.
 * A table keeps the per-tile maths integer-only with no libm and no pow(). */
static const uint16_t s_lin[256] = {
       0,     3,     6,     9,    12,    15,    18,    21,    24,    27,    30,    33,    37,    40,    44,    48,
      52,    56,    60,    65,    70,    75,    80,    86,    91,    97,   103,   110,   116,   123,   130,   137,
     144,   152,   160,   168,   176,   185,   194,   203,   212,   222,   232,   242,   252,   262,   273,   284,
     296,   307,   319,   331,   343,   356,   369,   382,   395,   409,   423,   437,   452,   467,   482,   497,
     513,   529,   545,   561,   578,   595,   612,   630,   648,   666,   685,   704,   723,   742,   762,   782,
     802,   823,   844,   865,   887,   908,   931,   953,   976,   999,  1022,  1046,  1070,  1095,  1119,  1144,
    1170,  1195,  1221,  1248,  1274,  1301,  1329,  1356,  1384,  1413,  1441,  1470,  1500,  1529,  1559,  1590,
    1620,  1651,  1683,  1714,  1746,  1779,  1812,  1845,  1878,  1912,  1946,  1981,  2016,  2051,  2086,  2122,
    2159,  2195,  2232,  2270,  2307,  2346,  2384,  2423,  2462,  2502,  2542,  2582,  2623,  2664,  2705,  2747,
    2789,  2831,  2874,  2918,  2961,  3005,  3050,  3095,  3140,  3185,  3231,  3278,  3325,  3372,  3419,  3467,
    3515,  3564,  3613,  3663,  3712,  3763,  3813,  3864,  3916,  3968,  4020,  4072,  4125,  4179,  4233,  4287,
    4342,  4397,  4452,  4508,  4564,  4621,  4678,  4735,  4793,  4851,  4910,  4969,  5029,  5089,  5149,  5210,
    5271,  5333,  5395,  5457,  5520,  5583,  5647,  5711,  5776,  5841,  5906,  5972,  6038,  6105,  6172,  6240,
    6308,  6376,  6445,  6514,  6584,  6654,  6724,  6795,  6867,  6939,  7011,  7084,  7157,  7231,  7305,  7379,
    7454,  7529,  7605,  7682,  7758,  7835,  7913,  7991,  8070,  8148,  8228,  8308,  8388,  8469,  8550,  8632,
    8714,  8796,  8879,  8963,  9047,  9131,  9216,  9301,  9387,  9473,  9560,  9647,  9734,  9823,  9911, 10000,
};

/* Relative luminance x 1e8: 0.2126*lin(R) + 0.7152*lin(G) + 0.0722*lin(B). */
static int32_t filament_luminance(uint32_t rgb)
{
    uint32_t r = (rgb >> 16) & 0xFF;
    uint32_t g = (rgb >> 8) & 0xFF;
    uint32_t b = rgb & 0xFF;

    return (int32_t)(2126u * s_lin[r] + 7152u * s_lin[g] + 722u * s_lin[b]);
}

static uint8_t mix_channel(uint32_t a, uint32_t b, uint32_t t)
{
    return (uint8_t)((a * (255u - t) + b * t + 127u) / 255u);
}

static lv_color_t mix_color(uint32_t a, uint32_t b, uint32_t t)
{
    return lv_color_make(mix_channel((a >> 16) & 0xFF, (b >> 16) & 0xFF, t),
                         mix_channel((a >> 8) & 0xFF, (b >> 8) & 0xFF, t),
                         mix_channel(a & 0xFF, b & 0xFF, t));
}

/* How far a color sits from gray, 0..255: the gap between its brightest and
 * dimmest channel. A near-black that is ALSO near-neutral (soot, #16181C)
 * reads as charcoal; a dark color someone actually picked (navy, maroon,
 * forest green) has a wide gap here even at the same luminance. */
static uint32_t filament_chroma(uint32_t rgb)
{
    uint32_t r = (rgb >> 16) & 0xFF;
    uint32_t g = (rgb >> 8) & 0xFF;
    uint32_t b = rgb & 0xFF;
    uint32_t mx = (r > g) ? (r > b ? r : b) : (g > b ? g : b);
    uint32_t mn = (r < g) ? (r < b ? r : b) : (g < b ? g : b);

    return mx - mn;
}

lv_color_t ui_on_filament(uint32_t rgb)
{
    return filament_luminance(rgb) >= 42000000 ? UI_COL_ON_DARK : UI_COL_ON_LIGHT;
}

/* Dark-saturated cutoff raised from the old 0.08 to 0.12: a color like
 * forest green (0x1F5C3A, L=0.083) sits just above 0.08, so it used to fall
 * through to the ordinary light-color gradient below - which for an
 * already-dark green mixes its bottom half 40% toward the near-black
 * background and reads as flat black on the panel. */
static bool filament_is_dark_saturated(int32_t lum, uint32_t chroma)
{
    return lum <= 12000000 && chroma >= 40;
}

static bool filament_is_dark_neutral(int32_t lum, uint32_t chroma)
{
    return lum <= 8000000 && chroma < 40;
}

bool ui_filament_needs_outline(uint32_t rgb)
{
    int32_t lum = filament_luminance(rgb);
    uint32_t chroma = filament_chroma(rgb);

    /* Anything the gradient below renders dark - saturated or neutral -
     * needs the hairline that keeps its edge visible against the panel. */
    return filament_is_dark_saturated(lum, chroma) || filament_is_dark_neutral(lum, chroma);
}

/* Scales channel c so the color's brightest channel reaches `peak`,
 * preserving the ratio between channels (so the hue) exactly. mx is that
 * brightest channel's own value. */
static uint8_t scale_channel(uint32_t c, uint32_t peak, uint32_t mx)
{
    uint32_t v = (c * peak + mx / 2) / mx; /* rounded */
    return (uint8_t)(v > 255 ? 255 : v);
}

void ui_filament_gradient(uint32_t rgb, lv_color_t *top, lv_color_t *bottom)
{
    int32_t lum;
    uint32_t chroma;

    if(top == NULL || bottom == NULL) return;

    lum = filament_luminance(rgb);
    chroma = filament_chroma(rgb);

    if(filament_is_dark_saturated(lum, chroma)) {
        /* Dark but saturated (navy, maroon, forest green): SCALE the
         * channels so the brightest one reaches a fixed peak, which
         * preserves hue exactly. Mixing toward white here (the old
         * approach) desaturates navy into lavender, which the panel then
         * renders as purple. */
        uint32_t r = (rgb >> 16) & 0xFF;
        uint32_t g = (rgb >> 8) & 0xFF;
        uint32_t b = rgb & 0xFF;
        uint32_t mx = (r > g) ? (r > b ? r : b) : (g > b ? g : b);
        if(mx == 0) mx = 1; /* chroma>=40 already implies mx>=40; guard anyway */

        *top = lv_color_make(scale_channel(r, 0xB0, mx), scale_channel(g, 0xB0, mx),
                             scale_channel(b, 0xB0, mx));
        *bottom = lv_color_make(scale_channel(r, 0x70, mx), scale_channel(g, 0x70, mx),
                                scale_channel(b, 0x70, mx));
    }
    else if(filament_is_dark_neutral(lum, chroma)) {
        /* A near-black, near-neutral spool (soot, charcoal): keep the fixed
         * charcoal pair so it never draws as flat #000000. */
        *top = lv_color_hex(0x3A3E45);
        *bottom = lv_color_hex(0x16181C);
    }
    else {
        *top = mix_color(rgb, 0xFFFFFFu, 31u);    /* F toward white, 12% */
        *bottom = mix_color(rgb, 0x0B0E12u, 102u); /* F toward bg, 40% */
    }
}

lv_color_t ui_severity_color(int severity)
{
    switch(severity) {
        case MMU_SEV_STOP:  return UI_COL_ERR;
        case MMU_SEV_PAUSE: return UI_COL_WARN;
        default:            return UI_COL_INFO;
    }
}

ui_style_id_t ui_severity_pill(int severity)
{
    switch(severity) {
        case MMU_SEV_STOP:  return UI_STYLE_PILL_ERR;
        case MMU_SEV_PAUSE: return UI_STYLE_PILL_WARN;
        default:            return UI_STYLE_PILL_INFO;
    }
}

/* A fill that reads as "the same surface, pressed". */
static lv_color_t lighten(lv_color_t c)
{
    return lv_color_mix(lv_color_white(), c, LV_OPA_10);
}

static void style_base(lv_style_t *s)
{
    lv_style_init(s);
    lv_style_set_pad_all(s, 0);
    lv_style_set_border_width(s, 0);
    lv_style_set_radius(s, 0);
    lv_style_set_bg_opa(s, LV_OPA_COVER);
    lv_style_set_text_color(s, UI_COL_TEXT);
    lv_style_set_text_font(s, UI_FONT_M);
}

/* Shared geometry of the three button kinds. */
static void style_button_base(lv_style_t *s)
{
    style_base(s);
    lv_style_set_radius(s, UI_RADIUS);
    lv_style_set_pad_hor(s, UI_PAD_L);
    lv_style_set_pad_ver(s, UI_PAD_XS);
    lv_style_set_text_font(s, UI_FONT_L);
    lv_style_set_transition(s, &s_trans_btn);
}

/* Pressed + disabled overlays for a button kind; added to the object with the
 * matching state selector by ui_fx_button_style(). */
static void style_button_states(ui_style_id_t id, lv_color_t fill, lv_color_t text)
{
    lv_style_t *p = &s_pressed[id];
    lv_style_t *d = &s_disabled[id];

    lv_style_init(p);
    lv_style_set_bg_opa(p, LV_OPA_COVER);
    lv_style_set_bg_color(p, lighten(fill));
    lv_style_set_text_color(p, text);

    lv_style_init(d);
    lv_style_set_opa(d, LV_OPA_40);

    s_state_ready[id] = true;
}

static void style_pill(lv_style_t *s, lv_color_t bg, lv_opa_t bg_opa, lv_color_t text)
{
    style_base(s);
    lv_style_set_radius(s, UI_RADIUS_PILL);
    lv_style_set_bg_color(s, bg);
    lv_style_set_bg_opa(s, bg_opa);
    lv_style_set_text_color(s, text);
    lv_style_set_text_font(s, UI_FONT_S);
    lv_style_set_pad_hor(s, UI_PAD_L);
    lv_style_set_pad_ver(s, UI_PAD_XS);
}

void ui_theme_init(void)
{
    lv_style_t *s;

    if(s_ready) return;
    s_ready = true;

    lv_style_transition_dsc_init(&s_trans_btn, s_trans_props, NULL, 100, 0, NULL);

    /* screen: flat, as the design language has it (UI_COL_BG_GRAD is
     * UI_COL_BG). A vertical gradient between equal stops paints the same
     * pixels but costs LVGL a color map and a row-by-row fill per stripe. */
    s = ui_style(UI_STYLE_SCREEN);
    style_base(s);
    lv_style_set_bg_color(s, UI_COL_BG);

    /* card: elevation 1 */
    s = ui_style(UI_STYLE_PANEL);
    style_base(s);
    lv_style_set_bg_color(s, UI_COL_PANEL);
    lv_style_set_radius(s, UI_RADIUS);

    /* card: elevation 2, told apart by a whisper of an outline, not a border */
    s = ui_style(UI_STYLE_PANEL_HI);
    style_base(s);
    lv_style_set_bg_color(s, UI_COL_PANEL_HI);
    lv_style_set_radius(s, UI_RADIUS + UI_PX(2));
    lv_style_set_border_width(s, UI_PX(1));
    lv_style_set_border_color(s, UI_COL_LINE);
    lv_style_set_border_opa(s, LV_OPA_60);

    /* top bar: no surface of its own, the gradient shows through */
    s = ui_style(UI_STYLE_TOPBAR);
    style_base(s);
    lv_style_set_bg_opa(s, LV_OPA_TRANSP);

    /* buttons: secondary / primary / disabled */
    s = ui_style(UI_STYLE_BTN);
    style_button_base(s);
    lv_style_set_bg_color(s, UI_COL_PANEL_HI);
    lv_style_set_text_color(s, UI_COL_TEXT);
    style_button_states(UI_STYLE_BTN, UI_COL_PANEL_HI, UI_COL_TEXT);

    s = ui_style(UI_STYLE_BTN_PRIMARY);
    style_button_base(s);
    lv_style_set_bg_color(s, UI_COL_ACCENT);
    lv_style_set_text_color(s, UI_COL_DARK_TEXT);
    style_button_states(UI_STYLE_BTN_PRIMARY, UI_COL_ACCENT, UI_COL_DARK_TEXT);

    s = ui_style(UI_STYLE_BTN_GHOST);
    style_button_base(s);
    lv_style_set_bg_opa(s, LV_OPA_TRANSP);
    lv_style_set_text_color(s, UI_COL_ACCENT);
    style_button_states(UI_STYLE_BTN_GHOST, UI_COL_ACCENT_DIM, UI_COL_ACCENT);

    s = ui_style(UI_STYLE_BTN_DISABLED);
    style_button_base(s);
    lv_style_set_bg_color(s, UI_COL_PANEL);
    lv_style_set_text_color(s, UI_COL_TEXT_DIM);
    lv_style_set_opa(s, LV_OPA_40);

    /* text */
    s = ui_style(UI_STYLE_TITLE);
    style_base(s);
    lv_style_set_bg_opa(s, LV_OPA_TRANSP);
    lv_style_set_text_font(s, UI_FONT_L);
    lv_style_set_text_color(s, UI_COL_TEXT);

    s = ui_style(UI_STYLE_LABEL);
    style_base(s);
    lv_style_set_bg_opa(s, LV_OPA_TRANSP);
    lv_style_set_text_font(s, UI_FONT_M);
    lv_style_set_text_color(s, UI_COL_TEXT);

    s = ui_style(UI_STYLE_LABEL_DIM);
    style_base(s);
    lv_style_set_bg_opa(s, LV_OPA_TRANSP);
    lv_style_set_text_font(s, UI_FONT_S);
    lv_style_set_text_color(s, UI_COL_TEXT_DIM);

    s = ui_style(UI_STYLE_TEXT_FAINT);
    style_base(s);
    lv_style_set_bg_opa(s, LV_OPA_TRANSP);
    lv_style_set_text_font(s, UI_FONT_S);
    lv_style_set_text_color(s, UI_COL_TEXT_FAINT);

    s = ui_style(UI_STYLE_HERO);
    style_base(s);
    lv_style_set_bg_opa(s, LV_OPA_TRANSP);
    lv_style_set_text_font(s, UI_FONT_HERO);
    lv_style_set_text_color(s, UI_COL_TEXT);

    s = ui_style(UI_STYLE_UNIT);
    style_base(s);
    lv_style_set_bg_opa(s, LV_OPA_TRANSP);
    lv_style_set_text_font(s, UI_FONT_S);
    lv_style_set_text_color(s, UI_COL_TEXT_DIM);

    /* sheet: elevation 2, rounded only at the top because it sits on the
     * bottom edge of the screen. One flat fill, no outline. */
    s = ui_style(UI_STYLE_MODAL_CARD);
    style_base(s);
    lv_style_set_bg_color(s, UI_COL_PANEL_HI);
    lv_style_set_radius(s, UI_RADIUS_SHEET);

    s = ui_style(UI_STYLE_MODAL_SCRIM);
    style_base(s);
    lv_style_set_bg_color(s, lv_color_black());
    lv_style_set_bg_opa(s, UI_SCRIM_OPA);
    lv_style_set_radius(s, 0);

    /* progress track; the fill is set per widget and inherits the pill radius */
    s = ui_style(UI_STYLE_BAR);
    style_base(s);
    lv_style_set_bg_color(s, UI_COL_PANEL_LO);
    lv_style_set_radius(s, UI_RADIUS_PILL);

    /* pills */
    s = ui_style(UI_STYLE_BADGE);
    style_pill(s, UI_COL_PANEL_HI, LV_OPA_COVER, UI_COL_TEXT);

    s = ui_style(UI_STYLE_PILL);
    style_pill(s, UI_COL_PANEL_HI, LV_OPA_COVER, UI_COL_TEXT_DIM);

    s = ui_style(UI_STYLE_PILL_ACCENT);
    style_pill(s, UI_COL_ACCENT_DIM, LV_OPA_COVER, UI_COL_ACCENT);

    s = ui_style(UI_STYLE_PILL_WARN);
    style_pill(s, UI_COL_WARN, LV_OPA_20, UI_COL_WARN);

    s = ui_style(UI_STYLE_PILL_ERR);
    style_pill(s, UI_COL_ERR, LV_OPA_20, UI_COL_ERR);

    s = ui_style(UI_STYLE_PILL_INFO);
    style_pill(s, UI_COL_INFO, LV_OPA_20, UI_COL_INFO);

    /* divider: a 1px line the caller sizes */
    s = ui_style(UI_STYLE_DIVIDER);
    style_base(s);
    lv_style_set_bg_color(s, UI_COL_LINE);
    lv_style_set_radius(s, 0);
}

/**
 * @file ui_theme.h
 * The design system: colors, spacing/radius tokens, fonts and the shared
 * styles every screen uses. See docs/DESIGN.md for how the pieces fit together.
 *
 * Rule of thumb for the palette: surfaces are near-black and recede, text is
 * the only thing that is bright, and color is reserved for filament and for
 * state. Nothing here draws a border unless the border *means* something.
 */

#ifndef UI_THEME_H
#define UI_THEME_H

#include "lvgl.h"

#include "ui/fonts/ui_fonts.h"

#include <stdbool.h>

#include "ui/ui_tokens.h"
#include "ui/fonts/ui_fonts.h"
#include "ui/fonts/ui_fonts_home.h"

/* ------------------------------------------------------------------ palette */

/* Surfaces, darkest to lightest. The screen itself is a vertical gradient so
 * the panel edges read as depth rather than as a flat fill. */
#define UI_COL_BG         UI_TOK_BG
#define UI_COL_BG_GRAD    UI_TOK_BG /* the screen is flat in the design language */
#define UI_COL_PANEL      UI_TOK_SURFACE_1
#define UI_COL_PANEL_LO   UI_TOK_SURFACE_3 /* wells: bar tracks, insets */
#define UI_COL_PANEL_HI   lv_color_hex(0x2C3138) /* pressed */

/* The one hairline color. Use it for separators, never for decoration. */
#define UI_COL_LINE       lv_color_hex(0x2E333B)

/* Text, icons and the remaining bar drawn *on* a filament fill. The choice
 * comes from the filament's own luminance: see ui_on_filament(). */
#define UI_COL_ON_LIGHT   lv_color_hex(0xF4F7F8)
#define UI_COL_ON_DARK    lv_color_hex(0x14181D)

/* Type ramp. TEXT for anything that is read, DIM for support, FAINT for
 * captions that should barely register. */
#define UI_COL_TEXT       UI_TOK_TEXT
#define UI_COL_TEXT_DIM   UI_TOK_TEXT_MUTED
#define UI_COL_TEXT_FAINT UI_TOK_TEXT_FAINT

/* Accent: the app's own color, used sparingly (active tab, primary action,
 * loaded state). ACCENT_DIM is its low-emphasis background. */
#define UI_COL_ACCENT     UI_TOK_ACCENT
#define UI_COL_ACCENT_DIM lv_color_hex(0x2B4A6B)

/* State colors. Filament colors are the heroes; these are not. */
#define UI_COL_OK         UI_TOK_OK
#define UI_COL_WARN       UI_TOK_WARN
#define UI_COL_ERR        UI_TOK_ERROR
#define UI_COL_INFO       UI_TOK_INFO
#define UI_COL_DARK_TEXT  UI_TOK_BG /* text on accent/light fills */

/* A disabled button reads as a dimmed version of its own kind, never a blank
 * dark bar, so its fill and label are exact colors, not a computed mix - a
 * disabled accent and a disabled danger get their own label color, a disabled
 * neutral or danger share one fill. */
#define UI_COL_DISABLED_ACCENT_BG   lv_color_hex(0x2B4A6B)
#define UI_COL_DISABLED_ACCENT_TEXT lv_color_hex(0x5A7EA3)
#define UI_COL_DISABLED_DANGER_TEXT lv_color_hex(0x672A2F)
#define UI_COL_DISABLED_NEUTRAL_BG  UI_TOK_SURFACE_3

/* ------------------------------------------------------------ spacing tokens */

#define UI_PAD_XS   2
#define UI_PAD_S    4
#define UI_PAD_M    6
#define UI_PAD_L    8
#define UI_PAD_XL   12
#define UI_PAD_XXL  16

#define UI_RADIUS      10
#define UI_RADIUS_SHEET 16   /* a sheet's top corners (it sits on the bottom edge) */
#define UI_RADIUS_S    6
#define UI_RADIUS_PILL LV_RADIUS_CIRCLE

#define UI_SCREEN_W        480
#define UI_SCREEN_H        320
#define UI_TOPBAR_H        44
#define UI_TOPBAR_HOME_H   40 /* home's top bar is taller: no back, room for the pill */
#define UI_BOTTOMNAV_H     0  /* no bottom bar on any screen (UNIFIED_UI 4d) */
#define UI_ACTIONBAR_H     36 /* the bottom action band that replaces the nav, y 284-320 */
#define UI_TOUCH_MIN       36

/* Modals dim the screen with one black rect at this opacity (179/255). */
#define UI_SCRIM_OPA       179

/* ------------------------------------------------------------------- fonts */

#define UI_FONT_S     (&ui_font_geist_12)
#define UI_FONT_S_MED (&ui_font_geist_13)
#define UI_FONT_M     (&ui_font_geist_13)
#define UI_FONT_L     (&ui_font_geist_15)
#define UI_FONT_XL    (&ui_font_geist_20)
#define UI_FONT_HERO  (&ui_font_inter_28) /* big numbers, 2-3 glyphs at most */
#define UI_FONT_ICON   (&ui_font_icons_18)
#define UI_FONT_ICON_L (&ui_font_icons_24)

/* ------------------------------------------------------------------ styles */

/* The first fourteen ids are used by the screens that predate this design
 * system: keep their values and their meaning, restyle them in place. */
typedef enum {
    UI_STYLE_SCREEN,       /* the root object: gradient background */
    UI_STYLE_PANEL,        /* card: 1 elevation, padding 8, no border */
    UI_STYLE_PANEL_HI,     /* card: 2 elevation, faint 1px outline */
    UI_STYLE_TOPBAR,       /* transparent strip across the top */
    UI_STYLE_BTN,          /* secondary button */
    UI_STYLE_BTN_PRIMARY,  /* primary button (accent) */
    UI_STYLE_BTN_DISABLED, /* disabled button */
    UI_STYLE_TITLE,        /* FONT_L, TEXT */
    UI_STYLE_LABEL,        /* FONT_M, TEXT */
    UI_STYLE_LABEL_DIM,    /* FONT_S, TEXT_DIM */
    UI_STYLE_MODAL_CARD,   /* elevated card used by modals */
    UI_STYLE_MODAL_SCRIM,  /* black scrim behind a modal */
    UI_STYLE_BAR,          /* progress track (PANEL_LO) */
    UI_STYLE_BADGE,        /* small pill */
    /* --- added by the design system --- */
    UI_STYLE_BTN_GHOST,    /* tertiary button: no fill, accent text */
    UI_STYLE_PILL,         /* neutral pill */
    UI_STYLE_PILL_ACCENT,  /* accent-dim pill */
    UI_STYLE_PILL_WARN,
    UI_STYLE_PILL_ERR,
    UI_STYLE_PILL_INFO,
    UI_STYLE_TEXT_FAINT,   /* FONT_S, TEXT_FAINT */
    UI_STYLE_HERO,         /* FONT_HERO number */
    UI_STYLE_UNIT,         /* FONT_S, TEXT_DIM unit suffix */
    UI_STYLE_DIVIDER,      /* 1px UI_COL_LINE hairline */
    UI_STYLE_COUNT
} ui_style_id_t;

/** Which of a widget's overlay states to fetch. */
typedef enum {
    UI_STATE_STYLE_PRESSED,
    UI_STATE_STYLE_DISABLED,
} ui_state_style_t;

void ui_theme_init(void);
lv_style_t *ui_style(ui_style_id_t id);

/** The pressed (or disabled) overlay for a button style. Returns NULL for a
 * style that has no such state; ui_fx_button_style() applies them for you. */
lv_style_t *ui_style_state(ui_style_id_t id, ui_state_style_t which);

/** Black or white, whichever reads better on @p rgb. */
lv_color_t ui_readable_text(uint32_t rgb);

/** Text/icon color to draw on a fill of filament color @p rgb: a luminance
 * decision made from @p rgb itself, not from a gradient stop. */
lv_color_t ui_on_filament(uint32_t rgb);

/** The vertical fill of a slot tile holding filament @p rgb. Writes the top
 * and bottom stops into @p top / @p bottom. A near-black, near-neutral spool
 * gets the fixed charcoal pair instead; a dark but saturated color (navy,
 * maroon, forest green) is lifted toward white so its hue still reads. */
void ui_filament_gradient(uint32_t rgb, lv_color_t *top, lv_color_t *bottom);

/** Whether a swatch of filament @p rgb needs the 1 px outline that keeps its
 * edge visible against the panel: true for anything the gradient above
 * renders dark, neutral or saturated alike. */
bool ui_filament_needs_outline(uint32_t rgb);

/** Color a severity maps to. */
lv_color_t ui_severity_color(int severity);

/** The low-emphasis pill that matches a severity (used for status pills). */
ui_style_id_t ui_severity_pill(int severity);

#endif /* UI_THEME_H */

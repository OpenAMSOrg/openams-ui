/**
 * @file ui_home.h
 * The restyled home screen's shared pieces: the geometry every home widget is
 * built on, the type ramp, and the three small drawing helpers they all use.
 *
 * Geometry is fixed, never computed from content (docs/design/PRINCIPLES.md 2:
 * "state changes content, never geometry"). The three bands are
 *
 *   0..44    the unit header      (ui_unit_header), one line
 *   46..68   the message row      (ui_toolhead_message)
 *   72..222  the tiles            (ui_tile)
 *   222..320 the filament path    (ui_toolhead_strip)
 *
 * Type and color come from ui_tokens.h, generated from design/tokens.json; the
 * screens that predate this design system keep using ui_theme.h.
 */

#ifndef UI_HOME_COMMON_H
#define UI_HOME_COMMON_H

#include "lvgl.h"
#include "ui/ui_tokens.h"
#include "ui/fonts/ui_fonts.h"
#include "ui/fonts/ui_fonts_home.h"
#include "ui/ui_icons.h"

#include <stdbool.h>

/* ------------------------------------------------------------- geometry */

#define UI_HOME_W            480
#define UI_HOME_H            320

#define UI_HOME_HEADER_H     44
#define UI_HOME_HEADER_PAD   12    /* the left and right margin of every band */

#define UI_HOME_MSG_Y        (UI_HOME_HEADER_H + 2)   /* 46 */
#define UI_HOME_MSG_H        22

#define UI_HOME_TILES_Y      (UI_HOME_MSG_Y + UI_HOME_MSG_H + 4)   /* 72 */
#define UI_HOME_TILE_W       108
#define UI_HOME_TILE_H       150
#define UI_HOME_TILE_GAP     8
#define UI_HOME_TILES_H      UI_HOME_TILE_H

/** Tiles across, and so bays to a page: a unit with more than this many bays
 *  is paged (ui_chevron.c), a unit with this many or fewer never is. */
#define UI_HOME_TILE_COLS    4

#define UI_HOME_STRIP_Y      (UI_HOME_TILES_Y + UI_HOME_TILES_H)      /* 222 */
#define UI_HOME_STRIP_H      (UI_HOME_H - UI_HOME_STRIP_Y)            /* 98 */

/** The left edge of tile @p i of a four-wide row. */
#define UI_HOME_TILE_X(i)    (UI_HOME_HEADER_PAD + (i) * (UI_HOME_TILE_W + UI_HOME_TILE_GAP))

/** The content column inside a band: the width between the two margins. */
#define UI_HOME_INNER_W      (UI_HOME_W - 2 * UI_HOME_HEADER_PAD)

/* The paging chevrons (ui_chevron.c). They live in the strip's empty band:
 * below the filament path, which never draws below the extruder's top edge,
 * and clear of the extruder icon itself, which is at the strip's left end.
 * The strip spans the full width from x 0, so x is the same in both spaces
 * and y is the offset from the strip's top. */
#define UI_HOME_CHEVRON_S    36  /* UI_TOUCH_MIN (ui_theme.h): the touch target floor */
#define UI_HOME_CHEVRON_GAP  8
#define UI_HOME_CHEVRON_Y    30  /* ui_toolhead_strip.c's ICON_Y: one line with the extruder */

/** The left edge of chevron @p i: 0 for the one that moves a page left,
 *  1 for the one that moves it right. Right-aligned to the tiles' margin. */
#define UI_HOME_CHEVRON_X(i) (UI_HOME_W - UI_HOME_HEADER_PAD - UI_HOME_CHEVRON_S \
                               - (1 - (i)) * (UI_HOME_CHEVRON_S + UI_HOME_CHEVRON_GAP))

/* ------------------------------------------------------------------ type */

/* design/tokens.json's type.device, mapped onto the faces
 * tools/gen_fonts.py generates. */
#define UI_HOME_FONT_TITLE   (&ui_font_geist_20)   /* 20/600 */
#define UI_HOME_FONT_SECTION (&ui_font_geist_15)   /* 15/600 */
#define UI_HOME_FONT_LABEL   (&ui_font_geist_20)   /* 20/600, the tile's T0 */
#define UI_HOME_FONT_BODY    (&ui_font_geist_13)   /* 13/500 */
#define UI_HOME_FONT_META    (&ui_font_geist_12)   /* 12/400 */
#define UI_HOME_FONT_MONO    (&ui_font_plex_18)    /* 18/400, values and ∞ */

/* The one icon face the home screen draws with (the RFID mark, the hotend). The same 18 px face ui_theme.h exposes as UI_FONT_ICON, named here
 * so the home widgets need nothing from the older theme. */
#define UI_HOME_FONT_ICON    (&ui_font_icons_18)

/* The severity glyphs (an octagon for an error, a triangle for information), in
 * a face of their own so the icon face the older screens use stays as committed. */
#define UI_HOME_FONT_SEV     (&ui_font_sev_18)

/* The paging chevrons (ui_chevron.c): Material Design's chevrons at the size of
 * their touch target, in a face of their own for the same reason. */
#define UI_HOME_FONT_CHEVRON (&ui_font_chevron_36)

/* The alert slot: the severity icon and its count, reserved even when empty. */
#define UI_HOME_SLOT_W       44
#define UI_HOME_SLOT_H       22

/* ----------------------------------------------------------------- parts */

/** A label with no theme style of its own, so its box is exactly the text. */
lv_obj_t *ui_home_label(lv_obj_t *parent, const char *text, const lv_font_t *font,
                        lv_color_t color, int x, int y);

/** How wide @p s draws in @p font, in px. */
int ui_home_text_w(const char *s, const lv_font_t *font);

/** One line, no room to elide, so the string itself has to be short:
 * ui_home_label() with characters dropped from the end until @p text fits
 * @p max_w px. Never truncates below one character. */
lv_obj_t *ui_home_label_fit(lv_obj_t *parent, const char *text, const lv_font_t *font,
                            lv_color_t color, int x, int y, int max_w);

/** An alert slot's content: the severity icon and the count beside it, in the
 * tone's color, inside @p slot (which must be UI_HOME_SLOT_W wide). Nothing is
 * drawn for a summary that is not present. */
void ui_home_alert_mark(lv_obj_t *slot, int tone, int count);
/** The slot's resting state when there is nothing to report: a muted check
 * (PRINCIPLES.md 2). One label, where the mark is two, so a slot's child count
 * says which of the two it holds. */
void ui_home_alert_none(lv_obj_t *slot);

/** A label in the text color of @p tone (a view_tone_t), for the one message
 * row and the tile tags. */
lv_color_t ui_home_tone_color(int tone);

/** The 8x8 A8 hatch tile, tiled over @p w x @p h of @p parent
 * (docs/design/UNIFIED_UI.md 4c: the one disabled look - a bay with no spool,
 * and an offline unit's tiles). Returns the object, which the caller creates
 * first so it draws behind everything else in the tile. */
lv_obj_t *ui_home_hatch(lv_obj_t *parent, int w, int h);

#endif /* UI_HOME_COMMON_H */

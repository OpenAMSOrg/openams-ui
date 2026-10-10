/**
 * @file ui_kit.h
 * The device's shared widgets in the design language (docs/design/UNIFIED_UI.md
 * 4a-4d): the one-line header every screen carries, and the few parts the
 * restyled screens are built from - a section title, a card row, a switch, a
 * chip, a button. Fonts and colors come from ui_tokens.h and ui_fonts_home.h
 * through ui_home.h; nothing here uses the older theme.
 */

#ifndef UI_KIT_H
#define UI_KIT_H

#include "lvgl.h"
#include "ui/ui_home.h"

#include <stdbool.h>

/* The screen frame: a dark token background, no scrolling. */
lv_obj_t *ui_kit_screen(void);

/** The one-line header, UI_HOME_HEADER_H tall: a back arrow in the left slot
 * (where home carries the menu), the title, and at the right end the alert
 * slot (a count beside the severity icon; a tap opens the alerts screen) when
 * @p alert_slot, or nothing - the screen may put its own action there with
 * ui_kit_header_action(). Long-pressing the title opens the hidden debug
 * screen. */
lv_obj_t *ui_kit_header(lv_obj_t *scr, const char *title, bool alert_slot);

/** A text action at the header's right end. */
lv_obj_t *ui_kit_header_action(lv_obj_t *scr, const char *label, lv_event_cb_t cb, void *user);

/* ------------------------------------------------------- a scrolling list */

/** A vertical scrolling column at (12, @p y), 456 wide, down to the screen's
 * bottom margin, its children spaced by the card gap. */
lv_obj_t *ui_kit_column(lv_obj_t *scr, int y, int h);

/** A section title as a column child. */
lv_obj_t *ui_kit_section(lv_obj_t *col, const char *text);

/** A card row: full column width and @p h tall (0 = grows with its content).
 * Tappable when @p cb is not NULL, with a pressed state. Its children are the
 * caller's, placed by position. */
lv_obj_t *ui_kit_row(lv_obj_t *col, int h, lv_event_cb_t cb, void *user);

/** A card row with a title, an optional note under it and, when @p trailing
 * is given, a right-aligned trailing glyph (a chevron or a check). Returns the
 * row. */
lv_obj_t *ui_kit_text_row(lv_obj_t *col, const char *title, const char *note,
                          const char *trailing, lv_color_t trailing_color,
                          lv_event_cb_t cb, void *user);

/* ---------------------------------------------------------------- controls */

/** A switch, 40x22, placed at (x, y) of @p parent. The state is read back with
 * ui_kit_switch_get() inside @p cb (LV_EVENT_VALUE_CHANGED). */
lv_obj_t *ui_kit_switch(lv_obj_t *parent, int x, int y, bool on, lv_event_cb_t cb, void *user);
bool ui_kit_switch_get(lv_obj_t *sw);

/** A selectable chip of @p w x 30 at (x, y); @p w 0 = as wide as the text needs. */
lv_obj_t *ui_kit_chip(lv_obj_t *parent, const char *label, int x, int y, int w,
                      bool selected, lv_event_cb_t cb, void *user);
void ui_kit_chip_set_selected(lv_obj_t *chip, bool selected);

typedef enum { UI_KIT_BTN_PRIMARY, UI_KIT_BTN_NORMAL } ui_kit_btn_t;

/** A button of @p w x @p h at (x, y). A disabled button wears the one
 * disabled look (the hatch) and takes no taps. */
lv_obj_t *ui_kit_button(lv_obj_t *parent, const char *label, ui_kit_btn_t kind,
                        int x, int y, int w, int h, bool enabled,
                        lv_event_cb_t cb, void *user);

/** A "- value +" stepper of @p w x 34 at (x, y); returns the value label. */
lv_obj_t *ui_kit_stepper(lv_obj_t *parent, int x, int y, int w, const char *value,
                         lv_event_cb_t on_minus, lv_event_cb_t on_plus);

/** ui_kit_stepper() whose buttons also repeat while held (LV_EVENT_LONG_PRESSED_REPEAT
 * reaches the callbacks: they tell it from a tap with lv_event_get_code()), for a
 * value too far from its start to tap to, and whose callbacks get @p user. */
lv_obj_t *ui_kit_stepper_hold(lv_obj_t *parent, int x, int y, int w, const char *value,
                              lv_event_cb_t on_minus, lv_event_cb_t on_plus, void *user);

/** A wrapping paragraph label of width @p w; its height follows the text. */
lv_obj_t *ui_kit_text(lv_obj_t *parent, const char *text, const lv_font_t *font,
                      lv_color_t color, int x, int y, int w);

/** Like ui_kit_button(), but a non-empty @p reason dims it (the one disabled
 * look: the hatch) and a tap shows the reason in a toast instead of calling
 * @p cb - touch has no hover (docs/design/PRINCIPLES.md 11). */
lv_obj_t *ui_kit_button_r(lv_obj_t *parent, const char *label, ui_kit_btn_t kind,
                          int x, int y, int w, int h, const char *reason,
                          lv_event_cb_t cb, void *user);

/** A card at (x, y) of @p w x @p h on a plain parent (not a flex column). */
lv_obj_t *ui_kit_card(lv_obj_t *parent, int x, int y, int w, int h);

/** A filament swatch of @p size px. */
lv_obj_t *ui_kit_swatch(lv_obj_t *parent, int x, int y, int size, uint32_t rgb, bool empty);

/** A square ring of @p size px at (x, y) with @p pct (0..100) swept in @p color;
 * @p text (may be empty) is centered inside it. */
lv_obj_t *ui_kit_ring(lv_obj_t *parent, int x, int y, int size, int pct, lv_color_t color,
                      const char *text);

typedef enum { UI_KIT_TONE_NEUTRAL, UI_KIT_TONE_INFO, UI_KIT_TONE_ERROR, UI_KIT_TONE_ACCENT } ui_kit_tone_t;

/** A solid chip (a status pill) with its text, at (x, y); returns it. Its
 * width is what the text needs. */
lv_obj_t *ui_kit_tag(lv_obj_t *parent, const char *text, ui_kit_tone_t tone, int x, int y);

/** The width ui_kit_tag() gives @p text. */
int ui_kit_tag_w(const char *text);

/** How a bay is named in a sentence or a title: "Spool 3" on this display's own
 * unit, "<unit> Spool 3" on another (never "A3"). The result is in a static
 * buffer, valid until the next call. */
const char *ui_kit_spool(int unit, int slot);

/** The tool label of the bay's tile ("T0", or "?" for a bay no group claims),
 * copied into @p out. */
void ui_kit_tool(int unit, int slot, char *out, size_t n);

#endif /* UI_KIT_H */

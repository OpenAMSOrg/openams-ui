/**
 * @file ui_fonts_lang.h
 * The per-size language slots the Geist faces fall back to, and the one call
 * that points them at a language.
 *
 * Every translated character that Geist does not have (a CJK ideograph, a
 * Japanese comma) is drawn by a face of its own, and which face depends on
 * the language the user picked. Geist is const in flash, so the choice cannot
 * live in Geist: it lives in four writable lv_font_t slots, one per size, and
 * each slot holds a struct copy of the face to use at that size. The chain is
 *
 *     Geist  ->  slot  ->  icons
 *
 * The copy is of the descriptor only - the glyph bitmaps stay const in flash,
 * and the slots cost four lv_font_t in RAM (about 60 bytes), not 244 KB.
 * Switching language is four struct assignments.
 */
#ifndef UI_FONTS_LANG_H
#define UI_FONTS_LANG_H

#include "lvgl.h"

/** Point every slot at @p code's faces. An unknown code (or a language with
 * no CJK face, which is every one but ja, ko and zh-Hans) puts the icon face
 * of that size in the slot, so the chain is Geist -> icons as it was before
 * this existed. Never fails and never allocates; safe to call with the same
 * code again. */
void ui_fonts_lang_set(const char *code);

#endif /* UI_FONTS_LANG_H */

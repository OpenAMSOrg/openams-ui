/**
 * @file ui_fonts_lang.c
 * The four writable language slots, and the one call that points them at a
 * language. See ui_fonts_lang.h.
 */
#include "ui/ui_fonts_lang.h"

#include <string.h>

#include "ui/fonts/ui_fonts.h"
#include "ui/fonts/ui_fonts_home.h"

/* Which languages this build carries (VS_LANG_JA and friends): the faces of a
 * language it does not carry are not compiled in (cmake/languages.cmake drops
 * them from the sources), so its row is left out here too. */
#include "view/strings_langs.gen.h"

/* The generated Geist faces name these in their .fallback and cannot see them
 * without a declaration, so each one carries an extern of its own. */
lv_font_t ui_font_cjk_slot_12;
lv_font_t ui_font_cjk_slot_13;
lv_font_t ui_font_cjk_slot_15;
lv_font_t ui_font_cjk_slot_20;

/* One row per CJK language the build carries: the four faces, in size order. A
 * code that is not in this table (every Latin and Cyrillic language, and the
 * pseudo-locale) has no faces of its own, and its slots hold the icon face each
 * Geist size falls back to today, which is the chain this build had before the
 * slots existed. */
static const struct {
    const char *code;
    const lv_font_t *at_12;
    const lv_font_t *at_13;
    const lv_font_t *at_15;
    const lv_font_t *at_20;
} s_cjk[] = {
#if VS_LANG_JA
    { "ja", &ui_font_cjk_ja_12, &ui_font_cjk_ja_13, &ui_font_cjk_ja_15, &ui_font_cjk_ja_20 }, /* lit-ok: a language code */
#endif
#if VS_LANG_KO
    { "ko", &ui_font_cjk_ko_12, &ui_font_cjk_ko_13, &ui_font_cjk_ko_15, &ui_font_cjk_ko_20 }, /* lit-ok: a language code */
#endif
#if VS_LANG_ZH_HANS
    { "zh-Hans", &ui_font_cjk_zh_12, &ui_font_cjk_zh_13, &ui_font_cjk_zh_15, &ui_font_cjk_zh_20 }, /* lit-ok: a language code */
#endif
};

void ui_fonts_lang_set(const char *code)
{
    const lv_font_t *at_12 = &ui_font_icons_18;
    const lv_font_t *at_13 = &ui_font_icons_18;
    const lv_font_t *at_15 = &ui_font_icons_18;
    const lv_font_t *at_20 = &ui_font_icons_24;
    const size_t n = sizeof s_cjk / sizeof s_cjk[0];   /* 0 in a Latin-only build */
    size_t i;

    for(i = 0; code && i < n; i++) {
        if(strcmp(s_cjk[i].code, code) == 0) {
            at_12 = s_cjk[i].at_12;
            at_13 = s_cjk[i].at_13;
            at_15 = s_cjk[i].at_15;
            at_20 = s_cjk[i].at_20;
            break;
        }
    }

    /* A struct copy, not a pointer swap: the slot itself is what the const
     * Geist face in flash points at, so it has to hold the descriptor. The
     * glyph bitmaps behind .dsc stay const in flash. */
    ui_font_cjk_slot_12 = *at_12;
    ui_font_cjk_slot_13 = *at_13;
    ui_font_cjk_slot_15 = *at_15;
    ui_font_cjk_slot_20 = *at_20;
}

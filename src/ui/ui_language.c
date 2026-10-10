/**
 * @file ui_language.c
 * See ui_language.h.
 */
#include "ui/ui_language.h"

#include "app/platform.h"
#include "ui/ui_fonts_lang.h"
#include "view/strings.h"

bool ui_set_language(const char *code)
{
    if(!view_set_language(code)) return false;

    /* The strings first, so the faces are chosen for the language whose text
     * is about to be drawn, and the code is read back from the table rather
     * than from the caller's pointer. */
    ui_fonts_lang_set(view_lang_code(view_lang_current()));
    platform_language_set(view_lang_code(view_lang_current()));
    return true;
}

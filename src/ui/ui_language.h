/**
 * @file ui_language.h
 * The one entry point for changing the language: the strings, the fonts that
 * draw them and the stored choice, in that order.
 */
#ifndef UI_LANGUAGE_H
#define UI_LANGUAGE_H

#include <stdbool.h>

/** Make @p code the language: pick its string table, point the per-size font
 * slots at its faces, and store the choice so the next boot starts there.
 * False, with nothing changed at all, when no such language is built in -
 * a refused language is a check failing, not a silent fallback to English. */
bool ui_set_language(const char *code);

#endif /* UI_LANGUAGE_H */

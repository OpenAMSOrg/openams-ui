/**
 * @file test_languages.c
 * The language list this build carries, and what happens to a code it does not.
 *
 * -DMMU_LANGUAGES picks the set (cmake/languages.cmake); it arrives here as
 * MMU_LANGUAGES_* definitions and, in MMU_TEST_LANGS, as the codes themselves,
 * so this test checks the build it is part of: the count and every code of a
 * restricted build, and the whole catalog of the default one.
 *
 * Plain C, plain assert, no framework.
 */

/* The default CMAKE_BUILD_TYPE here is Release, which puts -DNDEBUG on the
 * command line and would compile every assert() in this file away: the CTest
 * run would then pass without checking anything. Keep them live. */
#undef NDEBUG

#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "view/strings.h"

/* What the build asked for, comma separated, in table order. */
#define WANTED MMU_TEST_LANGS

static void test_count_and_codes(void)
{
    int i;
    int n = 0;
    const char *want = WANTED;

    /* Every code of MMU_TEST_LANGS is in the list, in that order. */
    for(i = 0; want[i]; i++) {
        if(want[i] != ',') continue;
        n++;
    }
    n++;
    assert(view_lang_count() == n);

    for(i = 0; i < n; i++) {
        const char *comma = strchr(want, ',');
        size_t len = comma ? (size_t)(comma - want) : strlen(want);

        assert(view_lang_code(i) != NULL);
        assert(strlen(view_lang_code(i)) == len);
        assert(strncmp(view_lang_code(i), want, len) == 0);
        assert(view_lang_name(i)[0] != '\0');   /* never translated */
        want += len + (comma ? 1 : 0);
    }

    /* English is index 0 whatever was asked for: it is the fallback. */
    assert(strcmp(view_lang_code(0), "en") == 0);
    assert(view_lang_current() == 0);

    /* Out of range is empty, not a crash. */
    assert(view_lang_code(-1)[0] == '\0');
    assert(view_lang_name(-1)[0] == '\0');
    assert(view_lang_code(n)[0] == '\0');
    assert(view_lang_name(n)[0] == '\0');
}

/** Every compiled code can be selected, and the code that comes back is the one
 *  asked for. */
static void test_every_listed_code_selects(void)
{
    int i;

    for(i = 0; i < view_lang_count(); i++) {
        const char *code = view_lang_code(i);

        assert(view_set_language(code));
        assert(view_lang_current() == i);
        assert(strcmp(view_lang_code(view_lang_current()), code) == 0);
    }
    assert(view_set_language("en") && view_lang_current() == 0);
}

/** A code this build does not carry is refused, the language in use stays, and
 *  what is drawn stays English: the device's stored NVS "lang" can name a
 *  language a later, smaller image does not have. */
static void test_a_missing_code_stays_english(void)
{
    static const char *const candidates[] = { "de", "fr", "ja", "ko", "ru", "zh-Hans" };
    const char *missing = NULL;
    const char *english;
    size_t i;

    for(i = 0; i < sizeof candidates / sizeof candidates[0]; i++) {
        int j;
        bool found = false;

        for(j = 0; j < view_lang_count(); j++) {
            if(strcmp(view_lang_code(j), candidates[i]) == 0) found = true;
        }
        if(!found) {
            missing = candidates[i];
            break;
        }
    }
    /* The tables are const in flash and stay where they are, so the text can
     * be compared through the pointer across a refused switch. */
    english = view_str(VS_TAG_RUNOUT);
    assert(english[0] != '\0');

    if(missing) {
        assert(view_set_language(missing) == false);
        assert(view_lang_current() == 0);
        assert(strcmp(view_lang_code(view_lang_current()), "en") == 0);
        assert(strcmp(view_str(VS_TAG_RUNOUT), english) == 0);
        printf("languages: %d compiled in, \"%s\" falls back to English\n",
               view_lang_count(), missing);
    } else {
        /* Every language is compiled in, so nothing is missing to refuse. */
        printf("languages: all %d compiled in\n", view_lang_count());
    }

    /* A code no build carries, and no code at all, are refused the same way. */
    assert(view_set_language("xx") == false);
    assert(view_set_language(NULL) == false);
    assert(view_lang_current() == 0);
    assert(strcmp(view_str(VS_TAG_RUNOUT), english) == 0);
}

int main(void)
{
    printf("languages: %s\n", WANTED);
    test_count_and_codes();
    test_every_listed_code_selects();
    test_a_missing_code_stays_english();
    printf("LANGUAGES OK\n");
    return 0;
}
# One list of languages per build, and everything that follows from it.
#
# A device needs only its user's language. Carrying all eleven costs about
# 240 KB of CJK fonts plus every string table, and the ESP32 app slot has 3% to
# spare, so the build takes a language set (owner, 2026-09-30) and the image
# carries that set only.
#
#   MMU_LANGUAGES=all      every language: the simulator, the wasm core and the
#                          tests build this by default and are unchanged
#              =latin     en de es fr it pl pt-BR ru   (the ESP32 default)
#              =ja        en ja    (likewise ko and zh-Hans)
#              =en;ja     any ';' list of codes; English is always added, since
#                          it is the fallback for every id a translation leaves
#                          out and the index every other language falls back to
#
# The same list drives everything: the MMU_LANG_* definitions below guard the
# tables in src/view/strings_table.gen.h and the CJK faces of the image, and
# MMU_LANGUAGE_TAG rides on the ESP32 app version so an OTA user can tell a
# latin image from a ja one.
#
# Included by the root CMakeLists.txt, platform/esp32/main/CMakeLists.txt,
# platform/esp32/CMakeLists.txt and platform/core-wasm/CMakeLists.txt. Parsing
# happens on include, so each of those has the results in scope.

if(NOT DEFINED MMU_LANGUAGES)
    set(MMU_LANGUAGES "all" CACHE STRING
        "Languages this build carries: a preset (all, latin, ja, ko, zh-Hans) \
or a ';' list of codes.")
endif()

# Every language the repo has a table for: design/strings/<code>.json, minus
# glossary.json, which holds no table. tools/gen_strings.mjs reads the same
# directory and emits English first, the real languages in alphabetical order and
# the pseudo-locale last; mmu_languages_order() below reproduces that order, so
# the codes this module defines arrive in the order the tables have.
# Read from the directory itself, so a language added there is known (and in
# `all`) without touching this file.
file(GLOB _mmu_lang_files "${CMAKE_CURRENT_LIST_DIR}/../design/strings/*.json")
set(_mmu_lang_codes "")
foreach(_f IN LISTS _mmu_lang_files)
    get_filename_component(_c "${_f}" NAME_WE)
    if(NOT _c STREQUAL "glossary")
        list(APPEND _mmu_lang_codes "${_c}")
    endif()
endforeach()

# The named sets. `all` is every language including the pseudo-locale, because
# the host builds keep the pseudo-locale they have always had.
# (`all` selects nothing at compile time: every table is built in, whatever
# design/strings holds; MMU_PRESET_all is only the list for messages and fonts.)
set(MMU_PRESET_latin "en;de;es;fr;it;pl;pt-BR;ru")
set(MMU_PRESET_ja "en;ja")
set(MMU_PRESET_ko "en;ko")
set(MMU_PRESET_zh-Hans "en;zh-Hans")

# Language code -> the key in the CJK face file names
# (src/ui/fonts/ui_font_cjk_<key>_<size>.c; zh-Hans is the "zh" subset).
set(MMU_CJK_FONT_KEYS "ja:ja" "ko:ko" "zh-Hans:zh")

# English first, the rest alphabetically, the pseudo-locale last: the order
# tools/gen_strings.mjs emits the tables in.
function(mmu_languages_order out codes)
    set(ordered "en")
    set(rest "")
    foreach(c IN LISTS codes)
        if(NOT c STREQUAL "en" AND NOT c STREQUAL "qps")
            list(APPEND rest ${c})
        endif()
    endforeach()
    list(SORT rest)
    list(APPEND ordered ${rest})
    if("qps" IN_LIST codes)
        list(APPEND ordered "qps")
    endif()
    set(${out} "${ordered}" PARENT_SCOPE)
endfunction()

mmu_languages_order(MMU_KNOWN_LANGUAGES "${_mmu_lang_codes}")
set(MMU_PRESET_all "${MMU_KNOWN_LANGUAGES}")

# The compile definitions that select the set: the switch plus one macro per
# language. The generated header turns them into VS_LANG_<CODE> 1 or 0.
function(mmu_languages_defines out)
    if(MMU_LANGUAGE_REQUEST STREQUAL "all")
        # No selection at all: the generated header then builds every language.
        set(${out} "" PARENT_SCOPE)
        return()
    endif()
    set(defs "MMU_LANG_SELECTED")
    foreach(c IN LISTS MMU_LANGUAGE_CODES)
        # Upper case, "pt-BR" -> PT_BR: the macros the generated header looks for.
        string(MAKE_C_IDENTIFIER "MMU_LANG_${c}" macro)
        string(TOUPPER "${macro}" macro)
        list(APPEND defs ${macro})
    endforeach()
    set(${out} "${defs}" PARENT_SCOPE)
endfunction()

# ${out} <- ${sources} without the CJK faces of the languages this build does
# not carry. A face file excluded here is the one ui_fonts_lang.c leaves out
# too, so no unresolved symbol is left behind.
function(mmu_languages_filter_sources out)
    set(sources ${ARGN})
    set(disabled "")
    foreach(entry IN LISTS MMU_CJK_FONT_KEYS)
        string(REPLACE ":" ";" pair "${entry}")
        list(GET pair 0 code)
        list(GET pair 1 key)
        if(NOT code IN_LIST MMU_LANGUAGE_CODES)
            list(APPEND disabled "${key}")
        endif()
    endforeach()
    if(disabled)
        string(REPLACE ";" "|" keys "${disabled}")
        list(FILTER sources EXCLUDE REGEX "/ui_font_cjk_(${keys})_[0-9]+\\.c$")
    endif()
    set(${out} "${sources}" PARENT_SCOPE)
endfunction()

# ------------------------------------------------------------------ parse

# -DMMU_LANGUAGES=latin (a preset) or -DMMU_LANGUAGES="en;ja" (codes).
string(STRIP "${MMU_LANGUAGES}" MMU_LANGUAGES)

set(MMU_LANGUAGE_REQUEST "${MMU_LANGUAGES}")
if(MMU_LANGUAGES MATCHES "^(all|latin|ja|ko|zh-Hans)$")
    set(requested "${MMU_PRESET_${MMU_LANGUAGES}}")
    set(MMU_LANGUAGE_TAG "${MMU_LANGUAGES}")
else()
    string(REPLACE "," ";" requested "${MMU_LANGUAGES}")
    list(REMOVE_DUPLICATES requested)
endif()

# An unknown code is a configure error that names it: a typo must not quietly
# build a smaller image than the one that was asked for.
set(known_codes "")
foreach(code IN LISTS requested)
    string(STRIP "${code}" code)
    if(code STREQUAL "")
        continue()
    endif()
    if(NOT code IN_LIST MMU_KNOWN_LANGUAGES)
        message(FATAL_ERROR
            "MMU_LANGUAGES: unknown language \"${code}\". "
            "Known languages: ${MMU_KNOWN_LANGUAGES}. "
            "Presets: all latin ja ko zh-Hans, or a ';' list, e.g. -DMMU_LANGUAGES=\"en;ja\".")
    endif()
    list(APPEND known_codes "${code}")
endforeach()
if(NOT known_codes)
    message(FATAL_ERROR "MMU_LANGUAGES is empty: name a preset (all, latin, ja, ko, zh-Hans) or a ';' list of codes.")
endif()

# English is always there: index 0, the fallback for every id a translation
# leaves out, and the language app_init() falls back to.
list(APPEND known_codes "en")
mmu_languages_order(MMU_LANGUAGE_CODES "${known_codes}")

# The version tag: what this image is, short enough for an app version. A named
# set is tagged with its own name, a one-language set with the code, and
# anything else with the count.
if(NOT DEFINED MMU_LANGUAGE_TAG)
    set(non_english "")
    foreach(c IN LISTS MMU_LANGUAGE_CODES)
        if(NOT c STREQUAL "en")
            list(APPEND non_english "${c}")
        endif()
    endforeach()
    list(LENGTH MMU_LANGUAGE_CODES count)
    list(LENGTH non_english n_non_english)
    if(n_non_english EQUAL 1)
        list(GET non_english 0 MMU_LANGUAGE_TAG)
    else()
        set(MMU_LANGUAGE_TAG "lang${count}")
    endif()
endif()

mmu_languages_defines(MMU_LANGUAGE_DEFINES)

message(STATUS "MMU_LANGUAGES=${MMU_LANGUAGE_REQUEST}: "
               "${MMU_LANGUAGE_CODES} (${MMU_LANGUAGE_TAG})")
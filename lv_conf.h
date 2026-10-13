/**
 * @file lv_conf.h
 * Shared LVGL configuration for the MMU display.
 *
 * ONE file for both targets:
 *   - ESP32-2432S028 (CYD): no SDL, display driver added in a later chunk.
 *   - Desktop SDL simulator: CMake adds -DLV_USE_SDL=1, which the
 *     #ifndef guard below lets through without editing this file.
 *
 * Options not listed here keep LVGL's defaults (see lv_conf_internal.h).
 * Only the fonts and widgets actually used by src/ are enabled.
 */

/* clang-format off */
#if 1 /*Set it to "1" to enable content*/

#ifndef LV_CONF_H
#define LV_CONF_H

#include <stdint.h>

/*====================
   COLOR SETTINGS
 *====================*/

#define LV_COLOR_DEPTH 16
/* v9.6 replaced LV_COLOR_DEPTH with LV_COLOR_FORMAT_DEFAULT; stating both
 * keeps the 16-bit requirement explicit and silences the deprecation warning.
 * The library's own fallback for a missing value is exactly this pair. */
#define LV_COLOR_FORMAT_DEFAULT LV_COLOR_FORMAT_RGB565
#define LV_COLOR_16_SWAP 0

/*=========================
   MEMORY SETTINGS
 *=========================*/

#define LV_USE_STDLIB_MALLOC LV_STDLIB_BUILTIN
#define LV_USE_STDLIB_STRING LV_STDLIB_BUILTIN
#define LV_USE_STDLIB_SPRINTF LV_STDLIB_BUILTIN

#if LV_USE_STDLIB_MALLOC == LV_STDLIB_BUILTIN
    /* The 480x320 relayout's wider screens - most visibly Settings' new
     * Display section (a stepper row plus three chip rows, docs/VISUAL_SPEC_480.md
     * "Settings") - measurably outgrew the 320x240-era 64 KB pool: the full
     * --smoke walk's own peak is ~64.8 KB, which is already over the walk's
     * 90%-of-pool failure threshold before counting whatever the OTHER
     * relayout branch's heavier Home/Mapping/Print screens add once merged.
     * 128 KB left that headroom, but Wi-Fi (Fork 2 D-net) needs about 20 KB
     * of static DRAM the ESP32 no longer had. Measured 2026-09-28: the full
     * smoke walk (276 screens, 506 modals) peaks at 65,432 bytes, so 96 KB
     * keeps it near 67% of the pool, under the walk's 90% failure threshold
     * with about 20 KB to spare. Re-measure before adding heavy screens. */
    #define LV_MEM_SIZE (96 * 1024)
#endif

#define LV_USE_OS LV_OS_NONE

/*====================
   HAL SETTINGS
 *====================*/

/* How soon a change reaches the screen: LVGL renders invalid areas at most
 * once per period, so on the device a change waits up to 15 ms, not 33. An
 * idle refresh costs nothing (no invalid area, no draw). The touch panel has
 * its own read period, set by the device's touch driver. */
#if defined(ESP_PLATFORM)
    #define LV_DEF_REFR_PERIOD 15
#else
    #define LV_DEF_REFR_PERIOD 33
#endif
#define LV_DPI_DEF 130

/*====================
   RENDERING
 *====================*/

#define LV_USE_DRAW_SW 1
#define LV_USE_DRAW_SW_COMPLEX_GRADIENTS 0
#define LV_USE_SNAPSHOT 1
#define LV_USE_VECTOR_GRAPHIC 0
#define LV_USE_THORVG 0
#define LV_USE_MATRIX 0

/*====================
   LOGGING
 *====================*/

#define LV_USE_LOG 1
#if LV_USE_LOG
    #define LV_LOG_LEVEL LV_LOG_LEVEL_WARN
    #define LV_LOG_PRINTF 1
    #define LV_LOG_USE_TIMESTAMP 0
    #define LV_LOG_USE_FILE_LINE 1
#else
    #define LV_LOG_LEVEL LV_LOG_LEVEL_WARN
#endif

/*====================
   THEMES
 *====================*/

#define LV_USE_THEME_DEFAULT 1
#if LV_USE_THEME_DEFAULT
    #define LV_THEME_DEFAULT_DARK 1
    #define LV_THEME_DEFAULT_GROW 0
    #define LV_THEME_DEFAULT_TRANSITION_TIME 0
#endif
#define LV_USE_THEME_SIMPLE 0
#define LV_USE_THEME_MONO 0

/*====================
   LAYOUTS
 *====================*/

#define LV_USE_FLEX 1
#define LV_USE_GRID 0

/*====================
   FONTS
 *====================*/

#define LV_FONT_MONTSERRAT_12 1
#define LV_FONT_MONTSERRAT_14 1
#define LV_FONT_MONTSERRAT_16 1
#define LV_FONT_MONTSERRAT_20 1
#define LV_FONT_MONTSERRAT_8 0
#define LV_FONT_MONTSERRAT_10 0
#define LV_FONT_MONTSERRAT_18 0
#define LV_FONT_MONTSERRAT_22 0
#define LV_FONT_MONTSERRAT_24 0
#define LV_FONT_MONTSERRAT_26 0
/* The ESP32 image's spool tag writer (MMU_FEATURE_TAG_WRITER, set for every
 * component by platform/esp32/CMakeLists.txt) titles its pages in Montserrat
 * 28 and uses a slider and a spinner: enabled for that image only, so every
 * other build stays as it was. */
#if defined(MMU_FEATURE_TAG_WRITER)
#define LV_FONT_MONTSERRAT_28 1
#else
#define LV_FONT_MONTSERRAT_28 0
#endif
#define LV_FONT_MONTSERRAT_30 0
#define LV_FONT_MONTSERRAT_32 0
#define LV_FONT_MONTSERRAT_34 0
#define LV_FONT_MONTSERRAT_36 0
#define LV_FONT_MONTSERRAT_38 0
#define LV_FONT_MONTSERRAT_40 0
#define LV_FONT_MONTSERRAT_42 0
#define LV_FONT_MONTSERRAT_44 0
#define LV_FONT_MONTSERRAT_46 0
#define LV_FONT_MONTSERRAT_48 0

/* The generated Inter faces (src/ui/fonts/, see tools/gen_fonts.py) are the
 * app's real typography; Montserrat stays enabled only as the icon fonts'
 * fallback for the few symbols Inter does not carry. */
#define LV_FONT_CUSTOM_DECLARE LV_FONT_DECLARE(ui_font_inter_14)
#define LV_FONT_DEFAULT &ui_font_inter_14
#define LV_USE_FONT_COMPRESSED 0
#define LV_USE_FONT_PLACEHOLDER 1
#define LV_USE_FREETYPE 0

/*====================
   WIDGETS
 *====================*/

#define LV_USE_LABEL 1
#if LV_USE_LABEL
    #define LV_LABEL_TEXT_SELECTION 0
    #define LV_LABEL_LONG_TXT_HINT 0
#endif
#define LV_USE_BUTTON 1
#define LV_USE_BAR 1
#define LV_USE_LINE 1

#define LV_USE_3DTEXTURE 0
#define LV_USE_ANIMIMG 0
#define LV_USE_ARC 1
#define LV_USE_ARCLABEL 0
#define LV_USE_BARCODE 0
#define LV_USE_BUTTONMATRIX 0
#define LV_USE_CALENDAR 0
#define LV_USE_CANVAS 0
#define LV_USE_CHART 0
#define LV_USE_CHECKBOX 0
#define LV_USE_DROPDOWN 0
#define LV_USE_GIF 0
#define LV_USE_IMAGE 0
#define LV_USE_IMAGEBUTTON 0
#define LV_USE_KEYBOARD 0
#define LV_USE_LED 0
#define LV_USE_LIST 0
#define LV_USE_MENU 0
#define LV_USE_MSGBOX 0
#define LV_USE_QRCODE 0
#define LV_USE_ROLLER 0
#define LV_USE_SCALE 0
#if defined(MMU_FEATURE_TAG_WRITER)
#define LV_USE_SLIDER 1
#else
#define LV_USE_SLIDER 0
#endif
#define LV_USE_SPAN 0
#define LV_USE_SPINBOX 0
#if defined(MMU_FEATURE_TAG_WRITER)
#define LV_USE_SPINNER 1
#else
#define LV_USE_SPINNER 0
#endif
#define LV_USE_SWITCH 0
#define LV_USE_TABLE 0
#define LV_USE_TABVIEW 0
#define LV_USE_TEXTAREA 0
#define LV_USE_TILEVIEW 0
#define LV_USE_WIN 0

/*====================
   DISPLAY / INPUT DRIVERS
 *====================*/

#define LV_USE_LINUX_DRM 0
#define LV_USE_LINUX_FBDEV 0
#define LV_USE_EVDEV 0
#define LV_USE_LIBINPUT 0
#define LV_USE_TFT_ESPI 0

/* Desktop-only driver. CMake passes -DLV_USE_SDL=1 for the simulator;
 * the ESP32 build never defines it, so the shared file stays valid there. */
#ifndef LV_USE_SDL
    #define LV_USE_SDL 0
#endif
#if LV_USE_SDL
    #define LV_SDL_INCLUDE_PATH "SDL2/SDL.h"
    #define LV_SDL_BACKEND LV_SDL_BACKEND_SW
    #define LV_SDL_RENDER_MODE LV_DISPLAY_RENDER_MODE_DIRECT
    #define LV_SDL_BUF_COUNT 1
    #define LV_SDL_ACCELERATED 0
    #define LV_SDL_FULLSCREEN 0
    #define LV_SDL_DIRECT_EXIT 0
    #define LV_SDL_MOUSEWHEEL_MODE LV_SDL_MOUSEWHEEL_MODE_ENCODER
#endif

/*====================
   OTHERS
 *====================*/

#define LV_USE_OBJ_ID 0
#define LV_USE_OBJ_PROPERTY 0
#define LV_USE_TRANSLATION 0
#define LV_USE_COLOR_FILTER 0
#define LV_USE_SYSMON 0
#define LV_USE_PERF_MONITOR 0
#define LV_USE_MEM_MONITOR 0
#define LV_USE_PROFILER 0
#define LV_USE_TEST 0
#define LV_USE_MONKEY 0
#define LV_USE_FS_STDIO 0
#define LV_USE_FS_POSIX 0
#define LV_USE_FS_MEMFS 0
#define LV_USE_LODEPNG 0
#define LV_USE_BMP 0
#define LV_USE_RLE 0
#define LV_USE_LZ4 0
#define LV_USE_FLOAT 0

#endif /*LV_CONF_H*/

#endif /*End of "Content enable"*/

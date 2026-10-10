/*******************************************************************************
 * Size: 36 px
 * Bpp: 4
 * Opts: --font assets/fonts/materialdesignicons-webfont.ttf --size 36 --bpp 4 --format lvgl --lv-include lvgl.h --no-compress --lv-font-name ui_font_chevron_36 --range 0xF0141,0xF0142 --lv-fallback lv_font_montserrat_14 -o src/ui/fonts/ui_font_chevron_36.c
 ******************************************************************************/

#ifdef LV_LVGL_H_INCLUDE_SIMPLE
#include "lvgl.h"
#else
#include "lvgl.h"
#endif

#ifndef UI_FONT_CHEVRON_36
#define UI_FONT_CHEVRON_36 1
#endif

#if UI_FONT_CHEVRON_36

/*-----------------
 *    BITMAPS
 *----------------*/

/*Store the image of the glyphs*/
static LV_ATTRIBUTE_LARGE_CONST const uint8_t glyph_bitmap[] = {
    /* U+F0141 "󰅁" */
    0x0, 0x0, 0x0, 0x0, 0x12, 0x0, 0x0, 0x0,
    0x0, 0x1, 0xde, 0x20, 0x0, 0x0, 0x0, 0x1d,
    0xff, 0xd0, 0x0, 0x0, 0x1, 0xdf, 0xff, 0x40,
    0x0, 0x0, 0x1d, 0xff, 0xf4, 0x0, 0x0, 0x1,
    0xdf, 0xff, 0x40, 0x0, 0x0, 0x1d, 0xff, 0xf4,
    0x0, 0x0, 0x1, 0xdf, 0xff, 0x40, 0x0, 0x0,
    0x1d, 0xff, 0xf4, 0x0, 0x0, 0x0, 0xbf, 0xff,
    0x70, 0x0, 0x0, 0x0, 0x1d, 0xff, 0xf4, 0x0,
    0x0, 0x0, 0x1, 0xdf, 0xff, 0x40, 0x0, 0x0,
    0x0, 0x1d, 0xff, 0xf4, 0x0, 0x0, 0x0, 0x1,
    0xdf, 0xff, 0x40, 0x0, 0x0, 0x0, 0x1d, 0xff,
    0xf4, 0x0, 0x0, 0x0, 0x1, 0xdf, 0xff, 0x40,
    0x0, 0x0, 0x0, 0x1d, 0xff, 0xd0, 0x0, 0x0,
    0x0, 0x1, 0xde, 0x20, 0x0, 0x0, 0x0, 0x0,
    0x12, 0x0,

    /* U+F0142 "󰅂" */
    0x0, 0x21, 0x0, 0x0, 0x0, 0x0, 0x2, 0xed,
    0x10, 0x0, 0x0, 0x0, 0xd, 0xff, 0xd1, 0x0,
    0x0, 0x0, 0x4, 0xff, 0xfd, 0x10, 0x0, 0x0,
    0x0, 0x4f, 0xff, 0xd1, 0x0, 0x0, 0x0, 0x4,
    0xff, 0xfd, 0x10, 0x0, 0x0, 0x0, 0x4f, 0xff,
    0xd1, 0x0, 0x0, 0x0, 0x4, 0xff, 0xfd, 0x10,
    0x0, 0x0, 0x0, 0x4f, 0xff, 0xd1, 0x0, 0x0,
    0x0, 0x7, 0xff, 0xfb, 0x0, 0x0, 0x0, 0x4f,
    0xff, 0xd1, 0x0, 0x0, 0x4, 0xff, 0xfd, 0x10,
    0x0, 0x0, 0x4f, 0xff, 0xd1, 0x0, 0x0, 0x4,
    0xff, 0xfd, 0x10, 0x0, 0x0, 0x4f, 0xff, 0xd1,
    0x0, 0x0, 0x4, 0xff, 0xfd, 0x10, 0x0, 0x0,
    0xd, 0xff, 0xd1, 0x0, 0x0, 0x0, 0x2, 0xed,
    0x10, 0x0, 0x0, 0x0, 0x0, 0x21, 0x0, 0x0,
    0x0, 0x0
};


/*---------------------
 *  GLYPH DESCRIPTION
 *--------------------*/

static const lv_font_fmt_txt_glyph_dsc_t glyph_dsc[] = {
    {.bitmap_index = 0, .adv_w = 0, .box_w = 0, .box_h = 0, .ofs_x = 0, .ofs_y = 0} /* id = 0 reserved */,
    {.bitmap_index = 0, .adv_w = 576, .box_w = 12, .box_h = 19, .ofs_x = 12, .ofs_y = 4},
    {.bitmap_index = 114, .adv_w = 576, .box_w = 12, .box_h = 19, .ofs_x = 12, .ofs_y = 4}
};

/*---------------------
 *  CHARACTER MAPPING
 *--------------------*/



/*Collect the unicode lists and glyph_id offsets*/
static const lv_font_fmt_txt_cmap_t cmaps[] =
{
    {
        .range_start = 983361, .range_length = 2, .glyph_id_start = 1,
        .unicode_list = NULL, .glyph_id_ofs_list = NULL, .list_length = 0, .type = LV_FONT_FMT_TXT_CMAP_FORMAT0_TINY
    }
};



/*--------------------
 *  ALL CUSTOM DATA
 *--------------------*/

#if LVGL_VERSION_MAJOR == 8
/*Store all the custom data of the font*/
static  lv_font_fmt_txt_glyph_cache_t cache;
#endif

#if LVGL_VERSION_MAJOR >= 8
static const lv_font_fmt_txt_dsc_t font_dsc = {
#else
static lv_font_fmt_txt_dsc_t font_dsc = {
#endif
    .glyph_bitmap = glyph_bitmap,
    .glyph_dsc = glyph_dsc,
    .cmaps = cmaps,
    .kern_dsc = NULL,
    .kern_scale = 0,
    .cmap_num = 1,
    .bpp = 4,
    .kern_classes = 0,
    .bitmap_format = 0,
#if LVGL_VERSION_MAJOR == 8
    .cache = &cache
#endif
};

extern const lv_font_t lv_font_montserrat_14;


/*-----------------
 *  PUBLIC FONT
 *----------------*/

/*Initialize a public general font descriptor*/
#if LVGL_VERSION_MAJOR >= 8
const lv_font_t ui_font_chevron_36 = {
#else
lv_font_t ui_font_chevron_36 = {
#endif
    .get_glyph_dsc = lv_font_get_glyph_dsc_fmt_txt,    /*Function pointer to get glyph's data*/
    .get_glyph_bitmap = lv_font_get_bitmap_fmt_txt,    /*Function pointer to get glyph's bitmap*/
    .line_height = 19,          /*The maximum line height required by the font*/
    .base_line = -4,             /*Baseline measured from the bottom of the line*/
#if !(LVGL_VERSION_MAJOR == 6 && LVGL_VERSION_MINOR == 0)
    .subpx = LV_FONT_SUBPX_NONE,
#endif
#if LV_VERSION_CHECK(7, 4, 0) || LVGL_VERSION_MAJOR >= 8
    .underline_position = 0,
    .underline_thickness = 0,
#endif
    .dsc = &font_dsc,          /*The custom font data. Will be accessed by `get_glyph_bitmap/dsc` */
#if LV_VERSION_CHECK(8, 2, 0) || LVGL_VERSION_MAJOR >= 9
    .fallback = &lv_font_montserrat_14,
#endif
    .user_data = NULL,
};



#endif /*#if UI_FONT_CHEVRON_36*/


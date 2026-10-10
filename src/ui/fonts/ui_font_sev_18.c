/*******************************************************************************
 * Size: 18 px
 * Bpp: 4
 * Opts: --font assets/fonts/materialdesignicons-webfont.ttf --size 18 --bpp 4 --format lvgl --lv-include lvgl.h --no-compress --lv-font-name ui_font_sev_18 --range 0xF0CE6,0xF002A --lv-fallback lv_font_montserrat_14 -o src/ui/fonts/ui_font_sev_18.c
 ******************************************************************************/

#ifdef LV_LVGL_H_INCLUDE_SIMPLE
#include "lvgl.h"
#else
#include "lvgl.h"
#endif

#ifndef UI_FONT_SEV_18
#define UI_FONT_SEV_18 1
#endif

#if UI_FONT_SEV_18

/*-----------------
 *    BITMAPS
 *----------------*/

/*Store the image of the glyphs*/
static LV_ATTRIBUTE_LARGE_CONST const uint8_t glyph_bitmap[] = {
    /* U+F002A "󰀪" */
    0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0,
    0x0, 0x0, 0x0, 0x0, 0x0, 0x93, 0x0, 0x0,
    0x0, 0x0, 0x0, 0x0, 0x0, 0x3, 0xfc, 0x0,
    0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0xc, 0xff,
    0x60, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x5f,
    0x6c, 0xe1, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0,
    0xec, 0x2, 0xf8, 0x0, 0x0, 0x0, 0x0, 0x0,
    0x8, 0xf3, 0x10, 0x9f, 0x20, 0x0, 0x0, 0x0,
    0x0, 0x2f, 0xa0, 0xe8, 0x1e, 0xb0, 0x0, 0x0,
    0x0, 0x0, 0xaf, 0x10, 0xe8, 0x6, 0xf4, 0x0,
    0x0, 0x0, 0x4, 0xf7, 0x0, 0xe8, 0x0, 0xdd,
    0x0, 0x0, 0x0, 0xd, 0xd0, 0x0, 0x0, 0x0,
    0x4f, 0x70, 0x0, 0x0, 0x6f, 0x40, 0x0, 0x95,
    0x0, 0xa, 0xf1, 0x0, 0x1, 0xeb, 0x0, 0x0,
    0xb6, 0x0, 0x2, 0xf9, 0x0, 0x9, 0xfa, 0x88,
    0x88, 0x88, 0x88, 0x88, 0xdf, 0x30, 0x2f, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xc0,

    /* U+F0CE6 "󰳦" */
    0x0, 0x0, 0x77, 0x77, 0x77, 0x10, 0x0, 0x0,
    0xa, 0xff, 0xff, 0xff, 0xd1, 0x0, 0x0, 0xaf,
    0x70, 0x0, 0x3, 0xfd, 0x10, 0xa, 0xf7, 0x0,
    0x47, 0x0, 0x3f, 0xd1, 0x7f, 0x70, 0x0, 0x9e,
    0x0, 0x3, 0xfd, 0x9e, 0x0, 0x0, 0x9e, 0x0,
    0x0, 0x9e, 0x9e, 0x0, 0x0, 0x9e, 0x0, 0x0,
    0x9e, 0x9e, 0x0, 0x0, 0x9e, 0x0, 0x0, 0x9e,
    0x9e, 0x0, 0x0, 0x0, 0x0, 0x0, 0x9e, 0x9f,
    0x10, 0x0, 0x47, 0x0, 0x0, 0xbe, 0x3f, 0xd1,
    0x0, 0x9e, 0x0, 0xa, 0xf7, 0x3, 0xfd, 0x10,
    0x0, 0x0, 0xaf, 0x70, 0x0, 0x3f, 0xd8, 0x88,
    0x8b, 0xf7, 0x0, 0x0, 0x3, 0xff, 0xff, 0xff,
    0x70, 0x0
};


/*---------------------
 *  GLYPH DESCRIPTION
 *--------------------*/

static const lv_font_fmt_txt_glyph_dsc_t glyph_dsc[] = {
    {.bitmap_index = 0, .adv_w = 0, .box_w = 0, .box_h = 0, .ofs_x = 0, .ofs_y = 0} /* id = 0 reserved */,
    {.bitmap_index = 0, .adv_w = 288, .box_w = 18, .box_h = 15, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 135, .adv_w = 288, .box_w = 14, .box_h = 14, .ofs_x = 2, .ofs_y = 0}
};

/*---------------------
 *  CHARACTER MAPPING
 *--------------------*/

static const uint16_t unicode_list_0[] = {
    0x0, 0xcbc
};

/*Collect the unicode lists and glyph_id offsets*/
static const lv_font_fmt_txt_cmap_t cmaps[] =
{
    {
        .range_start = 983082, .range_length = 3261, .glyph_id_start = 1,
        .unicode_list = unicode_list_0, .glyph_id_ofs_list = NULL, .list_length = 2, .type = LV_FONT_FMT_TXT_CMAP_SPARSE_TINY
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
const lv_font_t ui_font_sev_18 = {
#else
lv_font_t ui_font_sev_18 = {
#endif
    .get_glyph_dsc = lv_font_get_glyph_dsc_fmt_txt,    /*Function pointer to get glyph's data*/
    .get_glyph_bitmap = lv_font_get_bitmap_fmt_txt,    /*Function pointer to get glyph's bitmap*/
    .line_height = 15,          /*The maximum line height required by the font*/
    .base_line = 0,             /*Baseline measured from the bottom of the line*/
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



#endif /*#if UI_FONT_SEV_18*/


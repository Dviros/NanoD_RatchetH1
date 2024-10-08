/*******************************************************************************
 * Size: 60 px
 * Bpp: 1
 ******************************************************************************/

#include "../ui.h"

#ifndef UI_FONT_SGDIG60
#define UI_FONT_SGDIG60 1
#endif

#if UI_FONT_SGDIG60

/*-----------------
 *    BITMAPS
 *----------------*/

/*Store the image of the glyphs*/
static LV_ATTRIBUTE_LARGE_CONST const uint8_t glyph_bitmap[] = {
    /* U+0030 "0" */
    0x3, 0x80, 0x38, 0x3, 0x80, 0x7, 0x0, 0x70,
    0x7, 0x3, 0x80, 0x38, 0x3, 0x80, 0x7, 0x0,
    0x70, 0x7, 0x3, 0x80, 0x38, 0x3, 0x80, 0x7,
    0x0, 0x70, 0x7, 0x3, 0x80, 0x38, 0x3, 0x80,
    0x7, 0x0, 0x70, 0x7, 0x3, 0x80, 0x38, 0x3,
    0x80, 0x7, 0x0, 0x70, 0x7, 0x3, 0x80, 0x38,
    0x3, 0x80, 0x7, 0x0, 0x70, 0x7, 0xe0, 0xe,
    0x0, 0xe0, 0x1, 0xc0, 0x1c, 0x1, 0xc0,

    /* U+0031 "1" */
    0xe0, 0xe, 0x0, 0xe0, 0x1, 0xc0, 0x1c, 0x1,
    0xc0, 0xe0, 0xe, 0x0, 0xe0, 0x1, 0xc0, 0x1c,
    0x1, 0xc0, 0xe0, 0xe, 0x0, 0xe0, 0x1, 0xc0,
    0x1c, 0x1, 0xc0, 0xe0, 0xe, 0x0, 0xe0, 0x1,
    0xc0, 0x1c, 0x1, 0xc0, 0xe0, 0xe, 0x0, 0xe0,
    0x1, 0xc0, 0x1c, 0x1, 0xc0, 0xe0, 0xe, 0x0,
    0xe0, 0x1, 0xc0, 0x1c, 0x1, 0xc0, 0x3, 0x80,
    0x38, 0x3, 0x80, 0x7, 0x0, 0x70, 0x7, 0x3,
    0x80, 0x38, 0x3, 0x80, 0x7, 0x0, 0x70, 0x7,

    /* U+0032 "2" */
    0xe0, 0xe, 0x0, 0xe0, 0x1, 0xc0, 0x1c, 0x1,
    0xc0, 0x3, 0x80, 0x38, 0x3, 0x80, 0x7, 0x0,
    0x70, 0x7, 0x3, 0x80, 0x38, 0x3, 0x80, 0x7,
    0x0, 0x70, 0x7, 0x3, 0x80, 0x38, 0x3, 0x80,
    0x7, 0x0, 0x70, 0x7, 0xe0, 0xe, 0x0, 0xe0,
    0x1, 0xc0, 0x1c, 0x1, 0xc0, 0x0, 0x0, 0x0,
    0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x3, 0x80,
    0x38, 0x3, 0x80, 0x7, 0x0, 0x70, 0x7, 0x3,
    0x80, 0x38, 0x3, 0x80, 0x7, 0x0, 0x70, 0x7,

    /* U+0033 "3" */
    0xe0, 0xe, 0x0, 0xe0, 0x1, 0xc0, 0x1c, 0x1,
    0xc0, 0x3, 0x80, 0x38, 0x3, 0x80, 0x7, 0x0,
    0x70, 0x7, 0x3, 0x80, 0x38, 0x3, 0x80, 0x7,
    0x0, 0x70, 0x7, 0x3, 0x80, 0x38, 0x3, 0x80,
    0x7, 0x0, 0x70, 0x7, 0x3, 0x80, 0x38, 0x3,
    0x80, 0x7, 0x0, 0x70, 0x7, 0x3, 0x80, 0x38,
    0x3, 0x80, 0x7, 0x0, 0x70, 0x7, 0x3, 0x80,
    0x38, 0x3, 0x80, 0x7, 0x0, 0x70, 0x7, 0xe0,
    0xe, 0x0, 0xe0, 0x1, 0xc0, 0x1c, 0x1, 0xc0,

    /* U+0034 "4" */
    0xe3, 0x8e, 0x7, 0x1c, 0x7e, 0x38, 0xe0, 0x71,
    0xc7, 0xe3, 0x8e, 0x7, 0x1c, 0x7e, 0x38, 0xe0,
    0x71, 0xc7, 0xe3, 0x8e, 0x7, 0x1c, 0x7e, 0x38,
    0xe0, 0x71, 0xc7, 0xe3, 0x8e, 0x7, 0x1c, 0x7e,
    0x38, 0xe0, 0x71, 0xc7,

    /* U+0035 "5" */
    0x3, 0x80, 0x38, 0x3, 0x80, 0x7, 0x0, 0x70,
    0x7, 0x3, 0x80, 0x38, 0x3, 0x80, 0x7, 0x0,
    0x70, 0x7, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0,
    0x0, 0x0, 0x0, 0xe0, 0xe, 0x0, 0xe0, 0x1,
    0xc0, 0x1c, 0x1, 0xc0, 0x3, 0x80, 0x38, 0x3,
    0x80, 0x7, 0x0, 0x70, 0x7, 0x3, 0x80, 0x38,
    0x3, 0x80, 0x7, 0x0, 0x70, 0x7, 0x3, 0x80,
    0x38, 0x3, 0x80, 0x7, 0x0, 0x70, 0x7, 0xe0,
    0xe, 0x0, 0xe0, 0x1, 0xc0, 0x1c, 0x1, 0xc0,

    /* U+0036 "6" */
    0x3, 0x80, 0x38, 0x3, 0x80, 0x7, 0x0, 0x70,
    0x7, 0x3, 0x80, 0x38, 0x3, 0x80, 0x7, 0x0,
    0x70, 0x7, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0,
    0x0, 0x0, 0x0, 0xe0, 0xe, 0x0, 0xe0, 0x1,
    0xc0, 0x1c, 0x1, 0xc0, 0x3, 0x80, 0x38, 0x3,
    0x80, 0x7, 0x0, 0x70, 0x7, 0x3, 0x80, 0x38,
    0x3, 0x80, 0x7, 0x0, 0x70, 0x7, 0x3, 0x80,
    0x38, 0x3, 0x80, 0x7, 0x0, 0x70, 0x7, 0xe0,
    0xe, 0x0, 0xe0, 0x1, 0xc0, 0x1c, 0x1, 0xc0,

    /* U+0037 "7" */
    0xe0, 0xe, 0x0, 0xe0, 0x1, 0xc0, 0x1c, 0x1,
    0xc0, 0x3, 0x80, 0x38, 0x3, 0x80, 0x7, 0x0,
    0x70, 0x7, 0x3, 0x80, 0x38, 0x3, 0x80, 0x7,
    0x0, 0x70, 0x7, 0x3, 0x80, 0x38, 0x3, 0x80,
    0x7, 0x0, 0x70, 0x7, 0x3, 0x80, 0x38, 0x3,
    0x80, 0x7, 0x0, 0x70, 0x7, 0x3, 0x80, 0x38,
    0x3, 0x80, 0x7, 0x0, 0x70, 0x7, 0x3, 0x80,
    0x38, 0x3, 0x80, 0x7, 0x0, 0x70, 0x7, 0x3,
    0x80, 0x38, 0x3, 0x80, 0x7, 0x0, 0x70, 0x7,

    /* U+0038 "8" */
    0xe0, 0xe, 0x0, 0xe0, 0x1, 0xc0, 0x1c, 0x1,
    0xc0, 0x3, 0x80, 0x38, 0x3, 0x80, 0x7, 0x0,
    0x70, 0x7, 0x3, 0x80, 0x38, 0x3, 0x80, 0x7,
    0x0, 0x70, 0x7, 0x3, 0x80, 0x38, 0x3, 0x80,
    0x7, 0x0, 0x70, 0x7, 0x3, 0x80, 0x38, 0x3,
    0x80, 0x7, 0x0, 0x70, 0x7, 0x3, 0x80, 0x38,
    0x3, 0x80, 0x7, 0x0, 0x70, 0x7, 0x3, 0x80,
    0x38, 0x3, 0x80, 0x7, 0x0, 0x70, 0x7, 0xe0,
    0xe, 0x0, 0xe0, 0x1, 0xc0, 0x1c, 0x1, 0xc0,

    /* U+0039 "9" */
    0xe3, 0x8e, 0x7, 0x1c, 0x7e, 0x38, 0xe0, 0x71,
    0xc7, 0xe3, 0x8e, 0x7, 0x1c, 0x7e, 0x38, 0xe0,
    0x71, 0xc7, 0xe3, 0x8e, 0x7, 0x1c, 0x7e, 0x38,
    0xe0, 0x71, 0xc7, 0xe3, 0x8e, 0x7, 0x1c, 0x70
};


/*---------------------
 *  GLYPH DESCRIPTION
 *--------------------*/

static const lv_font_fmt_txt_glyph_dsc_t glyph_dsc[] = {
    {.bitmap_index = 0, .adv_w = 0, .box_w = 0, .box_h = 0, .ofs_x = 0, .ofs_y = 0} /* id = 0 reserved */,
    {.bitmap_index = 0, .adv_w = 672, .box_w = 12, .box_h = 42, .ofs_x = 24, .ofs_y = 0},
    {.bitmap_index = 63, .adv_w = 479, .box_w = 12, .box_h = 48, .ofs_x = 12, .ofs_y = 0},
    {.bitmap_index = 135, .adv_w = 672, .box_w = 12, .box_h = 48, .ofs_x = 24, .ofs_y = 0},
    {.bitmap_index = 207, .adv_w = 672, .box_w = 12, .box_h = 48, .ofs_x = 24, .ofs_y = 0},
    {.bitmap_index = 279, .adv_w = 672, .box_w = 6, .box_h = 48, .ofs_x = 30, .ofs_y = 0},
    {.bitmap_index = 315, .adv_w = 672, .box_w = 12, .box_h = 48, .ofs_x = 24, .ofs_y = 0},
    {.bitmap_index = 387, .adv_w = 672, .box_w = 12, .box_h = 48, .ofs_x = 24, .ofs_y = 0},
    {.bitmap_index = 459, .adv_w = 672, .box_w = 12, .box_h = 48, .ofs_x = 24, .ofs_y = 0},
    {.bitmap_index = 531, .adv_w = 671, .box_w = 12, .box_h = 48, .ofs_x = 24, .ofs_y = 0},
    {.bitmap_index = 603, .adv_w = 673, .box_w = 6, .box_h = 42, .ofs_x = 30, .ofs_y = 0}
};

/*---------------------
 *  CHARACTER MAPPING
 *--------------------*/



/*Collect the unicode lists and glyph_id offsets*/
static const lv_font_fmt_txt_cmap_t cmaps[] =
{
    {
        .range_start = 48, .range_length = 10, .glyph_id_start = 1,
        .unicode_list = NULL, .glyph_id_ofs_list = NULL, .list_length = 0, .type = LV_FONT_FMT_TXT_CMAP_FORMAT0_TINY
    }
};



/*--------------------
 *  ALL CUSTOM DATA
 *--------------------*/

#if LV_VERSION_CHECK(8, 0, 0)
/*Store all the custom data of the font*/
static  lv_font_fmt_txt_glyph_cache_t cache;
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
    .bpp = 1,
    .kern_classes = 0,
    .bitmap_format = 0,
#if LV_VERSION_CHECK(8, 0, 0)
    .cache = &cache
#endif
};


/*-----------------
 *  PUBLIC FONT
 *----------------*/

/*Initialize a public general font descriptor*/
#if LV_VERSION_CHECK(9, 0, 0)
const lv_font_t ui_font_SGdig60 = {
#else
lv_font_t ui_font_SGdig60 = {
#endif
    .get_glyph_dsc = lv_font_get_glyph_dsc_fmt_txt,    /*Function pointer to get glyph's data*/
    .get_glyph_bitmap = lv_font_get_bitmap_fmt_txt,    /*Function pointer to get glyph's bitmap*/
    .line_height = 48,          /*The maximum line height required by the font*/
    .base_line = 0,             /*Baseline measured from the bottom of the line*/
#if !(LVGL_VERSION_MAJOR == 6 && LVGL_VERSION_MINOR == 0)
    .subpx = LV_FONT_SUBPX_NONE,
#endif
#if LV_VERSION_CHECK(7, 4, 0) || LVGL_VERSION_MAJOR >= 8
    .underline_position = 0,
    .underline_thickness = 0,
#endif
    .dsc = &font_dsc           /*The custom font data. Will be accessed by `get_glyph_bitmap/dsc` */
};



#endif /*#if UI_FONT_SGDIG60*/


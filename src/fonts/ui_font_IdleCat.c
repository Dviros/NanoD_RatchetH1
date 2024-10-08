/*******************************************************************************
 * Size: 60 px
 * Bpp: 1
 * Opts: --bpp 1 --size 60 --font /Users/karolmalota/SquareLine/assets/idlecat-Regular.otf -o /Users/karolmalota/SquareLine/assets/ui_font_IdleCat.c --format lvgl -r 0x41-0x45 --no-compress --no-prefilter
 ******************************************************************************/

#include "../ui.h"

#ifndef UI_FONT_IDLECAT
#define UI_FONT_IDLECAT 1
#endif

#if UI_FONT_IDLECAT

/*-----------------
 *    BITMAPS
 *----------------*/

/*Store the image of the glyphs*/
static LV_ATTRIBUTE_LARGE_CONST const uint8_t glyph_bitmap[] = {
    /* U+0041 "A" */
    0x0, 0x0, 0x0, 0xf0, 0xf, 0x0, 0x0, 0x0,
    0x0, 0x0, 0xf0, 0xf, 0x0, 0x0, 0x0, 0x0,
    0x0, 0xf0, 0xf, 0x0, 0x0, 0x0, 0x0, 0x0,
    0xf0, 0xf, 0x0, 0x0, 0x0, 0x0, 0xf, 0xf0,
    0xf, 0xf0, 0x0, 0x0, 0x0, 0xf, 0xf0, 0xf,
    0xf0, 0x0, 0x0, 0x0, 0xf, 0xf0, 0xf, 0xf0,
    0x0, 0x0, 0x0, 0xf, 0xf0, 0xf, 0xf0, 0x0,
    0x0, 0x0, 0xff, 0xf0, 0xf, 0xf0, 0x0, 0x0,
    0x0, 0xff, 0xf0, 0xf, 0xf0, 0x0, 0x0, 0x0,
    0xff, 0xf0, 0xf, 0xf0, 0x0, 0x0, 0x0, 0xff,
    0xf0, 0xf, 0xf0, 0x0, 0x0, 0xf, 0xff, 0xff,
    0xff, 0xff, 0x0, 0x0, 0xf, 0xff, 0xff, 0xff,
    0xff, 0x0, 0x0, 0xf, 0xff, 0xff, 0xff, 0xff,
    0x0, 0x0, 0xf, 0xff, 0xff, 0xff, 0xff, 0x0,
    0xf, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xf,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xf, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xf, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0x0, 0xff, 0xff, 0xf0,
    0xf0, 0xff, 0xf0, 0x0, 0xff, 0xff, 0xf0, 0xf0,
    0xff, 0xf0, 0x0, 0xff, 0xff, 0xf0, 0xf0, 0xff,
    0xf0, 0x0, 0xff, 0xff, 0xf0, 0xf0, 0xff, 0xf0,
    0xf, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xf,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xf, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xf, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0x0, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xf0, 0x0, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xf0, 0x0, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xf0, 0x0, 0xff, 0xff, 0xff, 0xff, 0xff, 0xf0,
    0x0, 0xf, 0xff, 0xff, 0xff, 0xff, 0x0, 0x0,
    0xf, 0xff, 0xff, 0xff, 0xff, 0x0, 0x0, 0xf,
    0xff, 0xff, 0xff, 0xff, 0x0, 0x0, 0xf, 0xff,
    0xff, 0xff, 0xff, 0x0, 0x0, 0x0, 0x0, 0x0,
    0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0,
    0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0,
    0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0,
    0x0, 0x0, 0xff, 0xff, 0xff, 0xf0, 0x0, 0x0,
    0x0, 0xff, 0xff, 0xff, 0xf0, 0x0, 0x0, 0x0,
    0xff, 0xff, 0xff, 0xf0, 0x0, 0x0, 0x0, 0xff,
    0xff, 0xff, 0xf0, 0x0, 0xf, 0xf, 0xff, 0xff,
    0xff, 0xff, 0x0, 0xf, 0xf, 0xff, 0xff, 0xff,
    0xff, 0x0, 0xf, 0xf, 0xff, 0xff, 0xff, 0xff,
    0x0, 0xf, 0xf, 0xff, 0xff, 0xff, 0xff, 0x0,
    0xf0, 0xf, 0xff, 0xff, 0xff, 0xff, 0x0, 0xf0,
    0xf, 0xff, 0xff, 0xff, 0xff, 0x0, 0xf0, 0xf,
    0xff, 0xff, 0xff, 0xff, 0x0, 0xf0, 0xf, 0xff,
    0xff, 0xff, 0xff, 0x0, 0xf, 0xf, 0xff, 0xff,
    0xff, 0xff, 0x0, 0xf, 0xf, 0xff, 0xff, 0xff,
    0xff, 0x0, 0xf, 0xf, 0xff, 0xff, 0xff, 0xff,
    0x0, 0xf, 0xf, 0xff, 0xff, 0xff, 0xff, 0x0,
    0x0, 0xff, 0xff, 0xf0, 0xf0, 0xf0, 0x0, 0x0,
    0xff, 0xff, 0xf0, 0xf0, 0xf0, 0x0, 0x0, 0xff,
    0xff, 0xf0, 0xf0, 0xf0, 0x0, 0x0, 0xff, 0xff,
    0xf0, 0xf0, 0xf0, 0x0,

    /* U+0042 "B" */
    0x0, 0x0, 0x0, 0xf0, 0xf, 0x0, 0x0, 0x0,
    0x0, 0x0, 0xf0, 0xf, 0x0, 0x0, 0x0, 0x0,
    0x0, 0xf0, 0xf, 0x0, 0x0, 0x0, 0x0, 0x0,
    0xf0, 0xf, 0x0, 0x0, 0x0, 0x0, 0xf, 0xf0,
    0xf, 0xf0, 0x0, 0x0, 0x0, 0xf, 0xf0, 0xf,
    0xf0, 0x0, 0x0, 0x0, 0xf, 0xf0, 0xf, 0xf0,
    0x0, 0x0, 0x0, 0xf, 0xf0, 0xf, 0xf0, 0x0,
    0x0, 0x0, 0xff, 0xf0, 0xf, 0xf0, 0x0, 0x0,
    0x0, 0xff, 0xf0, 0xf, 0xf0, 0x0, 0x0, 0x0,
    0xff, 0xf0, 0xf, 0xf0, 0x0, 0x0, 0x0, 0xff,
    0xf0, 0xf, 0xf0, 0x0, 0x0, 0xf, 0xff, 0xff,
    0xff, 0xff, 0x0, 0x0, 0xf, 0xff, 0xff, 0xff,
    0xff, 0x0, 0x0, 0xf, 0xff, 0xff, 0xff, 0xff,
    0x0, 0x0, 0xf, 0xff, 0xff, 0xff, 0xff, 0x0,
    0xf, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xf,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xf, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xf, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0x0, 0xff, 0xff, 0xf0,
    0xf0, 0xff, 0xf0, 0x0, 0xff, 0xff, 0xf0, 0xf0,
    0xff, 0xf0, 0x0, 0xff, 0xff, 0xf0, 0xf0, 0xff,
    0xf0, 0x0, 0xff, 0xff, 0xf0, 0xf0, 0xff, 0xf0,
    0xf, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xf,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xf, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xf, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0x0, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xf0, 0x0, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xf0, 0x0, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xf0, 0x0, 0xff, 0xff, 0xff, 0xff, 0xff, 0xf0,
    0x0, 0xf, 0xff, 0xff, 0xff, 0xff, 0x0, 0x0,
    0xf, 0xff, 0xff, 0xff, 0xff, 0x0, 0x0, 0xf,
    0xff, 0xff, 0xff, 0xff, 0x0, 0x0, 0xf, 0xff,
    0xff, 0xff, 0xff, 0x0, 0x0, 0x0, 0x0, 0x0,
    0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0,
    0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0,
    0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0,
    0x0, 0x0, 0xff, 0xff, 0xff, 0xf0, 0x0, 0x0,
    0x0, 0xff, 0xff, 0xff, 0xf0, 0x0, 0x0, 0x0,
    0xff, 0xff, 0xff, 0xf0, 0x0, 0x0, 0x0, 0xff,
    0xff, 0xff, 0xf0, 0x0, 0xf0, 0xf, 0xff, 0xff,
    0xff, 0xff, 0x0, 0xf0, 0xf, 0xff, 0xff, 0xff,
    0xff, 0x0, 0xf0, 0xf, 0xff, 0xff, 0xff, 0xff,
    0x0, 0xf0, 0xf, 0xff, 0xff, 0xff, 0xff, 0x0,
    0xf0, 0xf, 0xff, 0xff, 0xff, 0xff, 0x0, 0xf0,
    0xf, 0xff, 0xff, 0xff, 0xff, 0x0, 0xf0, 0xf,
    0xff, 0xff, 0xff, 0xff, 0x0, 0xf0, 0xf, 0xff,
    0xff, 0xff, 0xff, 0x0, 0xf, 0xf, 0xff, 0xff,
    0xff, 0xff, 0x0, 0xf, 0xf, 0xff, 0xff, 0xff,
    0xff, 0x0, 0xf, 0xf, 0xff, 0xff, 0xff, 0xff,
    0x0, 0xf, 0xf, 0xff, 0xff, 0xff, 0xff, 0x0,
    0x0, 0xff, 0xff, 0xf0, 0xf0, 0xf0, 0x0, 0x0,
    0xff, 0xff, 0xf0, 0xf0, 0xf0, 0x0, 0x0, 0xff,
    0xff, 0xf0, 0xf0, 0xf0, 0x0, 0x0, 0xff, 0xff,
    0xf0, 0xf0, 0xf0, 0x0,

    /* U+0043 "C" */
    0x0, 0x0, 0x0, 0xf, 0x0, 0xf0, 0x0, 0x0,
    0x0, 0x0, 0x0, 0xf0, 0xf, 0x0, 0x0, 0x0,
    0x0, 0x0, 0xf, 0x0, 0xf0, 0x0, 0x0, 0x0,
    0x0, 0x0, 0xf0, 0xf, 0x0, 0x0, 0x0, 0x0,
    0x0, 0xff, 0x0, 0xff, 0x0, 0x0, 0x0, 0x0,
    0xf, 0xf0, 0xf, 0xf0, 0x0, 0x0, 0x0, 0x0,
    0xff, 0x0, 0xff, 0x0, 0x0, 0x0, 0x0, 0xf,
    0xf0, 0xf, 0xf0, 0x0, 0x0, 0x0, 0xf, 0xff,
    0x0, 0xff, 0x0, 0x0, 0x0, 0x0, 0xff, 0xf0,
    0xf, 0xf0, 0x0, 0x0, 0x0, 0xf, 0xff, 0x0,
    0xff, 0x0, 0x0, 0x0, 0x0, 0xff, 0xf0, 0xf,
    0xf0, 0x0, 0x0, 0xf0, 0xff, 0xff, 0xff, 0xff,
    0xf0, 0xf0, 0xf, 0xf, 0xff, 0xff, 0xff, 0xff,
    0xf, 0x0, 0xf0, 0xff, 0xff, 0xff, 0xff, 0xf0,
    0xf0, 0xf, 0xf, 0xff, 0xff, 0xff, 0xff, 0xf,
    0x0, 0xf, 0xff, 0xff, 0xff, 0xff, 0xff, 0x0,
    0x0, 0xff, 0xff, 0xff, 0xff, 0xff, 0xf0, 0x0,
    0xf, 0xff, 0xff, 0xff, 0xff, 0xff, 0x0, 0x0,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xf0, 0x0, 0xff,
    0xff, 0xff, 0xf, 0xf, 0xff, 0xf0, 0xf, 0xff,
    0xff, 0xf0, 0xf0, 0xff, 0xff, 0x0, 0xff, 0xff,
    0xff, 0xf, 0xf, 0xff, 0xf0, 0xf, 0xff, 0xff,
    0xf0, 0xf0, 0xff, 0xff, 0x0, 0xf, 0xff, 0xff,
    0xff, 0xff, 0xff, 0x0, 0x0, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xf0, 0x0, 0xf, 0xff, 0xff, 0xff,
    0xff, 0xff, 0x0, 0x0, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xf0, 0x0, 0xf, 0xff, 0xff, 0xff, 0xff,
    0xff, 0x0, 0x0, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xf0, 0x0, 0xf, 0xff, 0xff, 0xff, 0xff, 0xff,
    0x0, 0x0, 0xff, 0xff, 0xff, 0xff, 0xff, 0xf0,
    0x0, 0x0, 0xff, 0xff, 0xff, 0xff, 0xf0, 0x0,
    0x0, 0xf, 0xff, 0xff, 0xff, 0xff, 0x0, 0x0,
    0x0, 0xff, 0xff, 0xff, 0xff, 0xf0, 0x0, 0x0,
    0xf, 0xff, 0xff, 0xff, 0xff, 0x0, 0x0, 0x0,
    0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0,
    0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0,
    0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0,
    0x0, 0x0, 0x0, 0x0, 0xf0, 0x0, 0xf, 0xff,
    0xff, 0xff, 0x0, 0xf, 0x0, 0x0, 0xff, 0xff,
    0xff, 0xf0, 0x0, 0xf0, 0x0, 0xf, 0xff, 0xff,
    0xff, 0x0, 0xf, 0x0, 0x0, 0xff, 0xff, 0xff,
    0xf0, 0x0, 0xf, 0x0, 0xff, 0xff, 0xff, 0xff,
    0xf0, 0x0, 0xf0, 0xf, 0xff, 0xff, 0xff, 0xff,
    0x0, 0xf, 0x0, 0xff, 0xff, 0xff, 0xff, 0xf0,
    0x0, 0xf0, 0xf, 0xff, 0xff, 0xff, 0xff, 0x0,
    0x0, 0xf0, 0xff, 0xff, 0xff, 0xff, 0xf0, 0x0,
    0xf, 0xf, 0xff, 0xff, 0xff, 0xff, 0x0, 0x0,
    0xf0, 0xff, 0xff, 0xff, 0xff, 0xf0, 0x0, 0xf,
    0xf, 0xff, 0xff, 0xff, 0xff, 0x0, 0x0, 0xf,
    0xff, 0xff, 0xf, 0xf, 0x0, 0x0, 0x0, 0xff,
    0xff, 0xf0, 0xf0, 0xf0, 0x0, 0x0, 0xf, 0xff,
    0xff, 0xf, 0xf, 0x0, 0x0, 0x0, 0xff, 0xff,
    0xf0, 0xf0, 0xf0, 0x0,

    /* U+0044 "D" */
    0xf0, 0x0, 0xf0, 0x0, 0xf0, 0x0, 0xf0, 0x0,
    0xf, 0x0, 0xf, 0x0, 0xf, 0x0, 0xf, 0x0,
    0xf0, 0x0, 0xf0, 0x0, 0xf0, 0x0, 0xf0, 0x0,
    0xf, 0x0, 0xf, 0x0, 0xf, 0x0, 0xf, 0x0,
    0x0, 0xf0, 0x0, 0xf0, 0x0, 0xf0, 0x0, 0xf0,
    0x0, 0xf, 0x0, 0xf, 0x0, 0xf, 0x0, 0xf,
    0x0, 0xf0, 0x0, 0xf0, 0x0, 0xf0, 0x0, 0xf0,
    0x0, 0xf, 0x0, 0xf, 0x0, 0xf, 0x0, 0xf,
    0x0, 0xf0, 0x0, 0xf0, 0x0, 0xf0, 0x0, 0xf0,
    0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0,
    0x0, 0xf0, 0x0, 0xf0, 0x0, 0xf0, 0x0, 0xf0,
    0xf, 0x0, 0xf, 0x0, 0xf, 0x0, 0xf, 0x0,
    0x0, 0xf0, 0x0, 0xf0, 0x0, 0xf0, 0x0, 0xf0,

    /* U+0045 "E" */
    0xf0, 0x0, 0xf, 0x0, 0x0, 0xf0, 0x0, 0xf,
    0x0, 0x0, 0xf, 0x0, 0x0, 0xf0, 0x0, 0xf,
    0x0, 0x0, 0xf0, 0x0, 0x0, 0xf0, 0x0, 0xf,
    0x0, 0x0, 0xf0, 0x0, 0xf, 0x0, 0x0, 0x0,
    0xf0, 0x0, 0xf, 0x0, 0x0, 0xf0, 0x0, 0xf,
    0x0, 0xf, 0x0, 0x0, 0xf0, 0x0, 0xf, 0x0,
    0x0, 0xf0, 0x0, 0x0, 0xf0, 0x0, 0xf, 0x0,
    0x0, 0xf0, 0x0, 0xf, 0x0, 0xf, 0x0, 0x0,
    0xf0, 0x0, 0xf, 0x0, 0x0, 0xf0, 0x0, 0xf0,
    0x0, 0xf, 0x0, 0x0, 0xf0, 0x0, 0xf, 0x0,
    0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0,
    0x0, 0x0, 0xf, 0x0, 0x0, 0xf0, 0x0, 0xf,
    0x0, 0x0, 0xf0, 0x0, 0x0, 0xf0, 0x0, 0xf,
    0x0, 0x0, 0xf0, 0x0, 0xf, 0x0, 0xf, 0x0,
    0x0, 0xf0, 0x0, 0xf, 0x0, 0x0, 0xf0, 0x0,
    0x0, 0xf0, 0x0, 0xf, 0x0, 0x0, 0xf0, 0x0,
    0xf, 0x0
};


/*---------------------
 *  GLYPH DESCRIPTION
 *--------------------*/

static const lv_font_fmt_txt_glyph_dsc_t glyph_dsc[] = {
    {.bitmap_index = 0, .adv_w = 0, .box_w = 0, .box_h = 0, .ofs_x = 0, .ofs_y = 0} /* id = 0 reserved */,
    {.bitmap_index = 0, .adv_w = 960, .box_w = 56, .box_h = 60, .ofs_x = 4, .ofs_y = -8},
    {.bitmap_index = 420, .adv_w = 960, .box_w = 56, .box_h = 60, .ofs_x = 4, .ofs_y = -8},
    {.bitmap_index = 840, .adv_w = 960, .box_w = 60, .box_h = 56, .ofs_x = 0, .ofs_y = -8},
    {.bitmap_index = 1260, .adv_w = 960, .box_w = 16, .box_h = 52, .ofs_x = 44, .ofs_y = -4},
    {.bitmap_index = 1364, .adv_w = 960, .box_w = 20, .box_h = 52, .ofs_x = 40, .ofs_y = -4}
};

/*---------------------
 *  CHARACTER MAPPING
 *--------------------*/



/*Collect the unicode lists and glyph_id offsets*/
static const lv_font_fmt_txt_cmap_t cmaps[] =
{
    {
        .range_start = 65, .range_length = 5, .glyph_id_start = 1,
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
const lv_font_t ui_font_IdleCat = {
#else
static lv_font_fmt_txt_dsc_t ui_font_IdleCat = {
#endif
    .get_glyph_dsc = lv_font_get_glyph_dsc_fmt_txt,    /*Function pointer to get glyph's data*/
    .get_glyph_bitmap = lv_font_get_bitmap_fmt_txt,    /*Function pointer to get glyph's bitmap*/
    .line_height = 60,          /*The maximum line height required by the font*/
    .base_line = 8,             /*Baseline measured from the bottom of the line*/
#if !(LVGL_VERSION_MAJOR == 6 && LVGL_VERSION_MINOR == 0)
    .subpx = LV_FONT_SUBPX_NONE,
#endif
#if LV_VERSION_CHECK(7, 4, 0) || LVGL_VERSION_MAJOR >= 8
    .underline_position = 0,
    .underline_thickness = 0,
#endif
    .dsc = &font_dsc           /*The custom font data. Will be accessed by `get_glyph_bitmap/dsc` */
};



#endif /*#if UI_FONT_IDLECAT*/


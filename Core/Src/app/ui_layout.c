#include "ui.h"
#include "lv_font_ui_12.h"
#include "lv_font_ui_14.h"
#include "lv_font_ui_16.h"
#include "lv_font_ui_28.h"
#include "lv_font_ui_32.h"

/* Static reference page, 320x240. No peripheral commands or live states.
 * All backgrounds/borders are created BEFORE labels. Label coordinates are
 * screen-relative, avoiding inherited padding and parent-content clipping. */
#define COL_BG       0x081B12
#define COL_HEADER   0x06140D
#define COL_LINE     0x244B36
#define COL_GREEN    0x6FFFA7
#define COL_BODY     0xD2ECDD
#define COL_MUTED    0x739C88
#define COL_DISABLED 0x446251
#define COL_YELLOW   0xFFD54A

static lv_obj_t *box(lv_obj_t *screen, int x, int y, int w, int h,
                     uint32_t bg, uint32_t border, int border_width)
{
    lv_obj_t *obj = lv_obj_create(screen);
    lv_obj_remove_style_all(obj);
    lv_obj_set_pos(obj, x, y);
    lv_obj_set_size(obj, w, h);
    lv_obj_set_style_bg_color(obj, lv_color_hex(bg), 0);
    lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(obj, lv_color_hex(border), 0);
    lv_obj_set_style_border_width(obj, border_width, 0);
    lv_obj_set_style_pad_all(obj, 0, 0);
    lv_obj_set_style_radius(obj, 0, 0);
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_CLICKABLE);
    return obj;
}

static void label_at(lv_obj_t *screen, const char *value,
                     int x, int y, int w, int h,
                     const lv_font_t *font, uint32_t color,
                     lv_text_align_t align)
{
    lv_obj_t *obj = lv_label_create(screen);
    lv_obj_remove_style_all(obj);
    lv_obj_set_style_text_font(obj, font, 0);
    lv_obj_set_style_text_color(obj, lv_color_hex(color), 0);
    lv_obj_set_style_text_align(obj, align, 0);
    lv_obj_set_style_text_letter_space(obj, 0, 0);
    lv_obj_set_style_text_line_space(obj, 0, 0);
    lv_obj_set_style_pad_all(obj, 0, 0);
    lv_label_set_long_mode(obj, LV_LABEL_LONG_CLIP);
    lv_label_set_text(obj, value);
    lv_obj_set_pos(obj, x, y);
    lv_obj_set_size(obj, w, h);
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_CLICKABLE);
}

void ui_layout_init(void)
{
    lv_obj_t *screen = lv_screen_active();
    lv_obj_t *obj;
    lv_obj_clean(screen);
    lv_obj_remove_style_all(screen);
    lv_obj_set_size(screen, 320, 240);
    lv_obj_set_style_bg_color(screen, lv_color_hex(COL_BG), 0);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(screen, 0, 0);
    lv_obj_clear_flag(screen, LV_OBJ_FLAG_SCROLLABLE);

    /* Backgrounds, dividers and focus outlines. */
    box(screen, 0, 0, 320, 24, COL_HEADER, COL_HEADER, 0);
    box(screen, 0, 23, 320, 1, COL_LINE, COL_LINE, 0);
    box(screen, 200, 24, 1, 216, COL_LINE, COL_LINE, 0);
    box(screen, 0, 88, 200, 1, COL_LINE, COL_LINE, 0);
    box(screen, 0, 188, 200, 1, COL_LINE, COL_LINE, 0);
    box(screen, 201, 24, 59, 72, 0x143523, COL_LINE, 1);
    box(screen, 260, 24, 60, 72, 0x102B1D, COL_LINE, 1);
    box(screen, 201, 96, 59, 72, COL_BG, COL_LINE, 1);
    box(screen, 260, 96, 60, 72, 0x10251A, COL_LINE, 1);
    /* Focus stays inside the first action block; icon band is intentionally empty. */
    box(screen, 202, 25, 57, 70, 0x143523, 0xA8D8B7, 2);
    obj = box(screen, 8, 9, 6, 6, COL_GREEN, COL_GREEN, 0);
    lv_obj_set_style_radius(obj, LV_RADIUS_CIRCLE, 0);
    obj = box(screen, 51, 4, 51, 17, COL_HEADER, 0x408D61, 1);
    lv_obj_set_style_radius(obj, 3, 0);
    obj = box(screen, 108, 4, 43, 17, 0x321B18, 0xA84B48, 1);
    lv_obj_set_style_radius(obj, 3, 0);
    obj = box(screen, 8, 228, 184, 4, 0x263C2F, 0x263C2F, 0);
    lv_obj_set_style_radius(obj, 2, 0);
    obj = box(screen, 8, 228, 37, 4, COL_YELLOW, COL_YELLOW, 0);
    lv_obj_set_style_radius(obj, 2, 0);

    /* Header. Captions use only glyphs present in the supplied fonts.
     * "cover closed" uses the compact wording "cover:close" (no missing 'yi'). */
    label_at(screen, "ALM", 19, 5, 30, 15, &lv_font_ui_12, COL_MUTED, LV_TEXT_ALIGN_LEFT);
    label_at(screen, "\xE7\x9B\x96\x3A\xE5\x85\xB3", 54, 5, 45, 15, &lv_font_ui_12, COL_GREEN, LV_TEXT_ALIGN_CENTER);
    label_at(screen, "\xE5\x8D\x87\xE6\xB8\xA9", 111, 5, 37, 15, &lv_font_ui_12, 0xFF7770, LV_TEXT_ALIGN_CENTER);
    label_at(screen, "\xE8\xAE\xBE\xE5\xAE\x9A\x20\x33\x37\x2E\x30\x20\x2F\x20\x33\x37\x2E\x30", 156, 5, 158, 15, &lv_font_ui_12, COL_MUTED, LV_TEXT_ALIGN_RIGHT);

    /* Left upper: calibrated temperature and target value. */
    label_at(screen, "\xE6\xB8\xA9\xE5\xBA\xA6\x20\x54\x45\x4D\x50", 8, 29, 180, 18, &lv_font_ui_14, COL_MUTED, LV_TEXT_ALIGN_LEFT);
    label_at(screen, "37.0", 8, 48, 104, 26, &lv_font_ui_32, COL_GREEN, LV_TEXT_ALIGN_LEFT);
    label_at(screen, "\xE2\x84\x83", 114, 62, 24, 16, &lv_font_ui_12, COL_MUTED, LV_TEXT_ALIGN_LEFT);
    label_at(screen, "\xE7\x9B\xAE\xE6\xA0\x87\x20\x33\x37\x2E\x30\x20\xE2\x84\x83", 8, 74, 100, 14, &lv_font_ui_12, COL_MUTED, LV_TEXT_ALIGN_LEFT);

    /* Left middle: incubation time; unused vertical space matches reference. */
    label_at(screen, "\xE5\xAD\xB5\xE8\x82\xB2\xE6\x97\xB6\xE9\x97\xB4\x20\x54\x49\x4D\x45", 8, 99, 184, 18, &lv_font_ui_14, COL_MUTED, LV_TEXT_ALIGN_LEFT);
    label_at(screen, "15.0", 8, 122, 104, 27, &lv_font_ui_32, COL_BODY, LV_TEXT_ALIGN_LEFT);
    label_at(screen, "min", 114, 134, 40, 16, &lv_font_ui_12, COL_MUTED, LV_TEXT_ALIGN_LEFT);
    label_at(screen, "\xE5\xBE\x85\xE5\xBC\x80\xE5\xA7\x8B", 8, 153, 184, 17, &lv_font_ui_12, COL_MUTED, LV_TEXT_ALIGN_LEFT);

    /* Left bottom: configured delay, remaining sample and progress. */
    label_at(screen, "\xE5\xBB\xB6\xE8\xBF\x9F\xE8\xBD\xAC\x20\x34\xE2\x84\x83\x20\xE4\xBF\x9D\xE5\xAD\x98", 8, 191, 140, 14, &lv_font_ui_12, COL_MUTED, LV_TEXT_ALIGN_LEFT);
    label_at(screen, "60 s", 150, 191, 42, 14, &lv_font_ui_12, COL_BODY, LV_TEXT_ALIGN_RIGHT);
    label_at(screen, "60", 8, 205, 46, 23, &lv_font_ui_28, COL_YELLOW, LV_TEXT_ALIGN_LEFT);
    label_at(screen, "s", 57, 212, 16, 16, &lv_font_ui_12, COL_MUTED, LV_TEXT_ALIGN_LEFT);

    /* Right action grid. Icon areas: x204..257 / x263..317,
     * y30..53 / y102..125. Do not place text in those reserved bands. */
    label_at(screen, "\xE5\xBC\x80\xE5\xA7\x8B", 204, 59, 53, 18, &lv_font_ui_14, COL_GREEN, LV_TEXT_ALIGN_CENTER);
    label_at(screen, "\x53\x45\x54\x20\xE5\xBC\x80\xE5\xA7\x8B", 204, 78, 53, 16, &lv_font_ui_12, COL_MUTED, LV_TEXT_ALIGN_CENTER);
    label_at(screen, "\xE5\x85\xB3\xE7\x9B\x96", 263, 59, 54, 18, &lv_font_ui_14, COL_GREEN, LV_TEXT_ALIGN_CENTER);
    label_at(screen, "\x53\x45\x54\x20\xE5\xBC\x80\xE7\x9B\x96", 263, 78, 54, 16, &lv_font_ui_12, COL_MUTED, LV_TEXT_ALIGN_CENTER);
    label_at(screen, "\xE6\xB6\x88\xE9\x9F\xB3", 204, 131, 53, 18, &lv_font_ui_14, COL_DISABLED, LV_TEXT_ALIGN_CENTER);
    label_at(screen, "\xE5\xBE\x85\xE6\x9C\xBA", 204, 150, 53, 16, &lv_font_ui_12, COL_DISABLED, LV_TEXT_ALIGN_CENTER);
    label_at(screen, "\xE5\xBB\xB6\xE8\xBF\x9F\x20\x34\xE2\x84\x83", 263, 131, 54, 18, &lv_font_ui_12, COL_BODY, LV_TEXT_ALIGN_CENTER);
    label_at(screen, "60 s", 263, 150, 54, 16, &lv_font_ui_12, COL_MUTED, LV_TEXT_ALIGN_CENTER);
}

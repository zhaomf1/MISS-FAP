#include "lvgl_port.h"
#include "lcd_st7789.h"
#include "cmsis_os.h"
#include "lvgl.h"
#include "key.h"
#include <stdio.h>

#define LVGL_BUF_LINES 8

static lv_display_t *s_display;
static lv_color_t s_display_buf[320 * LVGL_BUF_LINES];
static lv_indev_t *s_keypad;
static key_id_t s_last_key = KEY_OK;
static lv_group_t *s_key_group;

static uint32_t key_to_lvgl(key_id_t key)
{
    switch (key) {
    case KEY_LEFT:  return LV_KEY_LEFT;
    case KEY_RIGHT: return LV_KEY_RIGHT;
    case KEY_UP:    return LV_KEY_UP;
    case KEY_DOWN:  return LV_KEY_DOWN;
    case KEY_OK:    return LV_KEY_ENTER;
    default:        return 0;
    }
}

static void keypad_read_cb(lv_indev_t *indev, lv_indev_data_t *data)
{
    key_event_t event;
    (void)indev;

    data->state = LV_INDEV_STATE_RELEASED;
    data->key = 0;

    while (key_get_event(&event)) {
        uint32_t key = key_to_lvgl(event.id);
        if (key == 0) continue;

        /* 显式处理方向键，避免不同 LVGL v9 配置下 keypad 导航不生效。 */
        if (event.type == KEY_EVENT_PRESS && s_key_group != NULL) {
            if (event.id == KEY_LEFT || event.id == KEY_UP) {
                lv_group_focus_prev(s_key_group);
                data->key = 0;
                data->state = LV_INDEV_STATE_RELEASED;
                continue;
            }
            if (event.id == KEY_RIGHT || event.id == KEY_DOWN) {
                lv_group_focus_next(s_key_group);
                data->key = 0;
                data->state = LV_INDEV_STATE_RELEASED;
                continue;
            }
        }

        data->key = key;
        s_last_key = event.id;
        data->state = (event.type == KEY_EVENT_PRESS)
                    ? LV_INDEV_STATE_PRESSED
                    : LV_INDEV_STATE_RELEASED;
        break;
    }

    if (data->state == LV_INDEV_STATE_RELEASED && data->key == 0) {
        data->key = key_to_lvgl(s_last_key);
    }
}

static lv_obj_t *ui_label(lv_obj_t *parent, const char *text,
                          int32_t x, int32_t y, int32_t w, int32_t h,
                          lv_color_t color, uint16_t font_size)
{
    lv_obj_t *label = lv_label_create(parent);
    lv_label_set_text(label, text);
    lv_obj_set_pos(label, x, y);
    lv_obj_set_size(label, w, h);
    lv_obj_set_style_text_color(label, color, 0);
    if (font_size == 20) lv_obj_set_style_text_font(label, &lv_font_montserrat_20, 0);
    else if (font_size == 16) lv_obj_set_style_text_font(label, &lv_font_montserrat_16, 0);
    else lv_obj_set_style_text_font(label, &lv_font_montserrat_14, 0);
    return label;
}

static void panel_button_event(lv_event_t *event)
{
    if (lv_event_get_code(event) == LV_EVENT_CLICKED) {
        const char *name = (const char *)lv_event_get_user_data(event);
        printf("[lvgl] button clicked: %s\r\n", name);
    }
}

static lv_obj_t *panel_button(lv_obj_t *parent, const char *text,
                              const char *log_name, int32_t x, int32_t y,
                              int32_t w, int32_t h, lv_group_t *group)
{
    lv_obj_t *button = lv_button_create(parent);
    lv_obj_set_pos(button, x, y);
    lv_obj_set_size(button, w, h);
    lv_obj_set_style_bg_color(button, lv_color_hex(0x09251B), 0);
    lv_obj_set_style_border_color(button, lv_color_hex(0x2F7D5B), 0);
    lv_obj_set_style_border_width(button, 1, 0);
    lv_obj_set_style_radius(button, 2, 0);
    lv_obj_set_style_bg_color(button, lv_color_hex(0x174B36), LV_STATE_FOCUSED);
    lv_obj_set_style_border_color(button, lv_color_hex(0x57D99A), LV_STATE_FOCUSED);
    lv_obj_set_style_border_width(button, 2, LV_STATE_FOCUSED);

    lv_obj_t *label = lv_label_create(button);
    lv_label_set_text(label, text);
    lv_obj_set_style_text_color(label, lv_color_hex(0x6FFFB0), 0);
    lv_obj_center(label);
    lv_group_add_obj(group, button);
    lv_obj_add_event_cb(button, panel_button_event, LV_EVENT_CLICKED,
                        (void *)log_name);
    return button;
}

static void lvgl_input_test_init(void)
{
    lv_group_t *group = lv_group_create();
    s_key_group = group;
    lv_group_set_default(group);

    lv_obj_t *screen = lv_screen_active();
    lv_obj_set_style_bg_color(screen, lv_color_hex(0x061A12), 0);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);

    ui_label(screen, "ALM", 8, 4, 35, 16, lv_color_hex(0xB8E6CE), 14);
    ui_label(screen, "NORMAL", 42, 4, 58, 16, lv_color_hex(0x6FFFB0), 14);
    ui_label(screen, "SET 37.0 / 37.0", 228, 4, 88, 16, lv_color_hex(0x78B69B), 14);

    lv_obj_t *left = lv_obj_create(screen);
    lv_obj_set_pos(left, 0, 25);
    lv_obj_set_size(left, 202, 166);
    lv_obj_set_style_bg_color(left, lv_color_hex(0x061A12), 0);
    lv_obj_set_style_border_width(left, 1, 0);
    lv_obj_set_style_border_color(left, lv_color_hex(0x1F5A43), 0);
    lv_obj_set_style_radius(left, 0, 0);
    lv_obj_clear_flag(left, LV_OBJ_FLAG_SCROLLABLE);
    ui_label(left, "TEMP", 8, 8, 60, 16, lv_color_hex(0x78B69B), 14);
    ui_label(left, "37.0", 8, 28, 100, 28, lv_color_hex(0x6FFFB0), 20);
    ui_label(left, "C", 84, 35, 20, 16, lv_color_hex(0xB8E6CE), 14);
    ui_label(left, "SET 37.0 C", 8, 62, 100, 16, lv_color_hex(0x78B69B), 14);
    ui_label(left, "TIME", 8, 82, 60, 16, lv_color_hex(0x78B69B), 14);
    ui_label(left, "15.0", 8, 102, 100, 28, lv_color_hex(0xD5F3E4), 20);
    ui_label(left, "min", 84, 109, 30, 16, lv_color_hex(0xB8E6CE), 14);
    ui_label(left, "NOT STARTED", 8, 136, 100, 16, lv_color_hex(0x78B69B), 14);

    lv_obj_t *bottom = lv_obj_create(screen);
    lv_obj_set_pos(bottom, 0, 191);
    lv_obj_set_size(bottom, 202, 49);
    lv_obj_set_style_bg_color(bottom, lv_color_hex(0x061A12), 0);
    lv_obj_set_style_border_width(bottom, 1, 0);
    lv_obj_set_style_border_color(bottom, lv_color_hex(0x1F5A43), 0);
    lv_obj_set_style_radius(bottom, 0, 0);
    lv_obj_clear_flag(bottom, LV_OBJ_FLAG_SCROLLABLE);
    ui_label(bottom, "DELAY 4 C  SAVE", 8, 4, 130, 16, lv_color_hex(0xB8E6CE), 14);
    ui_label(bottom, "60 s", 8, 21, 55, 20, lv_color_hex(0xFFD33D), 16);
    ui_label(bottom, "60 s", 170, 5, 25, 16, lv_color_hex(0xD5F3E4), 14);

    panel_button(screen, "START", "START", 204, 25, 58, 72, group);
    panel_button(screen, "CLOSE", "CLOSE", 263, 25, 57, 72, group);
    panel_button(screen, "CANCEL", "CANCEL", 204, 98, 58, 72, group);
    panel_button(screen, "DELAY", "DELAY", 263, 98, 57, 72, group);

    s_keypad = lv_indev_create();
    lv_indev_set_type(s_keypad, LV_INDEV_TYPE_KEYPAD);
    lv_indev_set_read_cb(s_keypad, keypad_read_cb);
    lv_indev_set_group(s_keypad, group);
}

static void lvgl_flush_cb(lv_display_t *display,
                          const lv_area_t *area,
                          uint8_t *px_map)
{
    uint16_t x = (uint16_t)area->x1;
    uint16_t y = (uint16_t)area->y1;
    uint16_t w = (uint16_t)(area->x2 - area->x1 + 1);
    uint16_t h = (uint16_t)(area->y2 - area->y1 + 1);

    lcd_blit(x, y, w, h, (const uint16_t *)px_map);
    lv_display_flush_ready(display);
}

void lvgl_port_init(void)
{
    lv_init();

    s_display = lv_display_create(320, 240);
    lv_display_set_buffers(s_display,
                           s_display_buf,
                           NULL,
                           sizeof(s_display_buf),
                           LV_DISPLAY_RENDER_MODE_PARTIAL);
    lv_display_set_flush_cb(s_display, lvgl_flush_cb);

    /* 第一阶段测试控件，确认 LVGL 到 ST7789 的链路正常。 */
    lv_obj_t *label = lv_label_create(lv_screen_active());
    lv_label_set_text(label, "LVGL OK");
    lv_obj_center(label);
    lvgl_input_test_init();
}

void lvgl_port_tick_inc(uint32_t elapsed_ms)
{
    lv_tick_inc(elapsed_ms);
}

void lvgl_port_flush(uint16_t x, uint16_t y, uint16_t w, uint16_t h,
                     const uint16_t *pixels)
{
    lcd_blit(x, y, w, h, pixels);
}

void lvgl_port_task(void *argument)
{
    (void)argument;
    lcd_init();
    lvgl_port_init();
    for (;;) {
        lvgl_port_tick_inc(5);
        lv_timer_handler();
        osDelay(5);
    }
}

#include "lvgl_port.h"
#include "lcd_st7789.h"
#include "cmsis_os.h"
#include "lvgl.h"
#include "key.h"
#include "ui.h"
#include "FreeRTOS.h"
#include "task.h"
#include <stdio.h>

#define LVGL_BUF_LINES 8

/* 1 = paint the panel red for 200 ms before LVGL starts.
 * Black still => LCD/SPI/backlight fault. Red then black => LVGL/UI fault.
 * Set to 0 once the display path is confirmed. */
#define LCD_BOOT_SELFTEST 0

/* Must match the number of children created by ui_layout_init(). A smaller
 * count means lv_obj_create() ran out of LVGL heap. */
#define UI_EXPECTED_CHILDREN 39

#define UI_FAULT_NO_DISPLAY      1
#define UI_FAULT_HEAP_LOW        2
#define UI_FAULT_OBJECTS_MISSING 3

volatile lvgl_diag_t g_lvgl_diag;
volatile uint32_t g_lvgl_probe_mode;

static void diag_screen_snapshot(void)
{
    lv_obj_t *root = lv_screen_active();
    lv_mem_monitor_t mon;
    lv_mem_monitor(&mon);
    g_lvgl_diag.lv_biggest_free = mon.free_biggest_size;
    g_lvgl_diag.rtos_free = xPortGetFreeHeapSize();
    if(root == NULL) return;
    g_lvgl_diag.root_bg565 = lv_color_to_u16(lv_obj_get_style_bg_color(root, 0));
    g_lvgl_diag.root_bg_opa = lv_obj_get_style_bg_opa(root, 0);
    g_lvgl_diag.root_opa = lv_obj_get_style_opa(root, 0);
    g_lvgl_diag.root_width = lv_obj_get_width(root);
    g_lvgl_diag.root_height = lv_obj_get_height(root);
    g_lvgl_diag.root_children = lv_obj_get_child_count(root);
}

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

        if (event.type == KEY_EVENT_PRESS) {
            ui_key_event(key);
            data->key = 0;
            data->state = LV_INDEV_STATE_RELEASED;
            continue;
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
    uint32_t i, nonzero = 0, max_pixel = 0;
    uint32_t count = (uint32_t)w * h;
    const uint16_t *pixels = (const uint16_t *)px_map;
    lv_mem_monitor_t mon;

    g_lvgl_diag.flush_count++;
    g_lvgl_diag.last_x = x;
    g_lvgl_diag.last_y = y;
    g_lvgl_diag.last_w = w;
    g_lvgl_diag.last_h = h;
    g_lvgl_diag.last_pixel_count = count;
    g_lvgl_diag.first_pixel = pixels[0];
    for(i = 0; i < count; i++) {
        if(pixels[i] != 0) nonzero++;
        if(pixels[i] > max_pixel) max_pixel = pixels[i];
    }
    g_lvgl_diag.last_nonzero = nonzero;
    g_lvgl_diag.max_pixel = max_pixel;
    if(nonzero) g_lvgl_diag.nonzero_flushes++;
    lv_mem_monitor(&mon);
    g_lvgl_diag.lv_free_at_flush = mon.free_size;
    diag_screen_snapshot();

    lcd_blit(x, y, w, h, (const uint16_t *)px_map);
    lv_display_flush_ready(display);
}

/* Turn the whole panel red and record why. The direct LCD fill runs before the
 * LVGL path so the red is visible even when LVGL itself is what failed. */
static void ui_fault_screen(uint32_t code)
{
    lv_obj_t *root = lv_screen_active();
    lv_mem_monitor_t mon;

    g_lvgl_diag.ui_fault = code;
    g_lvgl_diag.stage = 100 + code;

    lcd_fill_screen(0xF800);

    if(root != NULL) {
        lv_obj_clean(root);
        lv_obj_remove_style_all(root);
        lv_obj_set_style_bg_color(root, lv_color_hex(0xFF0000), 0);
        lv_obj_set_style_bg_opa(root, LV_OPA_COVER, 0);
        lv_obj_invalidate(root);
        lv_obj_update_layout(root);
        lv_refr_now(s_display);
    }

    diag_screen_snapshot();
    lv_mem_monitor(&mon);
    printf("[ui] FAULT code=%lu free=%lu biggest=%lu children=%lu\r\n",
           (unsigned long)code,
           (unsigned long)mon.free_size,
           (unsigned long)mon.free_biggest_size,
           (unsigned long)g_lvgl_diag.root_children);
}

void lvgl_port_init(void)
{
    lv_mem_monitor_t mon;
    lv_obj_t *root;

    g_lvgl_diag.stage = 2;
    lv_init();

    s_display = lv_display_create(320, 240);
    if(s_display == NULL) {
        ui_fault_screen(UI_FAULT_NO_DISPLAY);
        return;
    }

    lv_display_set_buffers(s_display,
                           s_display_buf,
                           NULL,
                           sizeof(s_display_buf),
                           LV_DISPLAY_RENDER_MODE_PARTIAL);
    lv_display_set_flush_cb(s_display, lvgl_flush_cb);

    g_lvgl_diag.stage = 3;
    g_lvgl_diag.color_format = lv_display_get_color_format(s_display);
    lv_mem_monitor(&mon);
    g_lvgl_diag.lv_free_before_ui = mon.free_size;
    printf("[ui] heap before=%lu biggest=%lu fmt=%lu\r\n",
           (unsigned long)mon.free_size,
           (unsigned long)mon.free_biggest_size,
           (unsigned long)g_lvgl_diag.color_format);

    root = lv_screen_active();
    if(root == NULL) {
        ui_fault_screen(UI_FAULT_NO_DISPLAY);
        return;
    }

    ui_layout_init();
    g_lvgl_diag.stage = 4;

    root = lv_screen_active();
    if(root == NULL) {
        ui_fault_screen(UI_FAULT_NO_DISPLAY);
        return;
    }

    lv_obj_update_layout(root);
    lv_mem_monitor(&mon);
    g_lvgl_diag.lv_free_after_ui = mon.free_size;
    diag_screen_snapshot();
    printf("[ui] heap after=%lu biggest=%lu children=%lu bg=%04lX\r\n",
           (unsigned long)mon.free_size,
           (unsigned long)mon.free_biggest_size,
           (unsigned long)lv_obj_get_child_count(root),
           (unsigned long)g_lvgl_diag.root_bg565);

    if(lv_obj_get_child_count(root) < UI_EXPECTED_CHILDREN) {
        ui_fault_screen(UI_FAULT_OBJECTS_MISSING);
        return;
    }
    if(mon.free_size < 1024 || mon.free_biggest_size < 256) {
        ui_fault_screen(UI_FAULT_HEAP_LOW);
        return;
    }

    g_lvgl_diag.stage = 5;
    lv_refr_now(s_display);   /* paint now instead of waiting for the refresh timer */
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
    uint32_t last_probe = 0;
    (void)argument;
    g_lvgl_diag.stage = 1;
    lcd_init();

#if LCD_BOOT_SELFTEST
    /* 上电自检：整屏红 200ms。若之后仍全黑，问题在 LCD/SPI/背光；
     * 若红然后转黑，问题在 LVGL/UI 侧。确认显示链路后可改为 0。 */
    lcd_fill_screen(0xF800);
    osDelay(200);
#endif

    lvgl_port_init();
    for (;;) {
        uint32_t probe = g_lvgl_probe_mode;
        if(probe != last_probe) {
            if(probe == 1) {
                lv_obj_t *root = lv_screen_active();
                lv_obj_clean(root);
                lv_obj_set_style_opa(root, LV_OPA_COVER, 0);
                lv_obj_set_style_bg_opa(root, LV_OPA_COVER, 0);
                lv_obj_set_style_bg_color(root, lv_color_hex(0xFF0000), 0);
                lv_obj_invalidate(root);
            }
            else if(probe == 2) {
                lcd_fill_screen(0xF800);
            }
            else if(probe == 3 || probe == 0) {
                ui_layout_init();
            }
            last_probe = probe;
        }
        if(probe == 2) {
            osDelay(20);
            continue;
        }
        g_lvgl_diag.loop_count++;
        g_lvgl_diag.ui_stack_free_words = uxTaskGetStackHighWaterMark(NULL);
        lvgl_port_tick_inc(5);
        lv_timer_handler();
        g_lvgl_diag.handler_returns++;
        osDelay(5);
    }
}

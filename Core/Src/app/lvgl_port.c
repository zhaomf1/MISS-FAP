#include "lvgl_port.h"
#include "lcd_st7789.h"
#include "cmsis_os.h"
#include "lvgl.h"

#define LVGL_BUF_LINES 8

static lv_display_t *s_display;
static lv_color_t s_display_buf[320 * LVGL_BUF_LINES];

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

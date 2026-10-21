#include "lvgl_port.h"
#include "lcd_st7789.h"
#include "cmsis_os.h"

void lvgl_port_init(void)
{
    /* 此函数为后续 LVGL 初始化预留。 */
}

void lvgl_port_tick_inc(uint32_t elapsed_ms)
{
    (void)elapsed_ms;
    /* 接入 LVGL 后在此调用 lv_tick_inc(elapsed_ms)。 */
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
    for (;;) {
        /* 后续接入 LVGL 后在此调用 lv_timer_handler()。 */
        osDelay(5);
    }
}

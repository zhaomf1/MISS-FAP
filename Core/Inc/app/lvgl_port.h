#ifndef LVGL_PORT_H
#define LVGL_PORT_H

#include "main.h"

/* Debugger-visible snapshot. No dependence on optimized local variables. */
typedef struct {
    uint32_t stage;
    uint32_t ui_fault;      /* 0 = ok, otherwise the reason code shown red. */
    uint32_t loop_count;
    uint32_t handler_returns;
    uint32_t flush_count;
    uint32_t nonzero_flushes;
    uint32_t last_nonzero;
    uint32_t last_pixel_count;
    uint32_t last_x;
    uint32_t last_y;
    uint32_t last_w;
    uint32_t last_h;
    uint32_t first_pixel;
    uint32_t max_pixel;
    uint32_t root_bg565;
    uint32_t root_bg_opa;
    uint32_t root_opa;
    int32_t root_width;
    int32_t root_height;
    uint32_t root_children;
    uint32_t lv_free_before_ui;
    uint32_t lv_free_after_ui;
    uint32_t lv_free_at_flush;
    uint32_t lv_biggest_free;
    uint32_t rtos_free;
    uint32_t color_format;
    uint32_t ui_stack_free_words;
} lvgl_diag_t;
extern volatile lvgl_diag_t g_lvgl_diag;
/* 0=normal page, 1=plain red LVGL page, 2=direct LCD red (LVGL paused).
 * Write 3 to recreate the normal page after a probe. */
extern volatile uint32_t g_lvgl_probe_mode;

/* 轻量显示适配层：当前不依赖 LVGL，后续接入时可直接绑定 flush/tick。 */
void lvgl_port_init(void);
void lvgl_port_tick_inc(uint32_t elapsed_ms);
void lvgl_port_flush(uint16_t x, uint16_t y, uint16_t w, uint16_t h,
                     const uint16_t *pixels);
void lvgl_port_task(void *argument);

#endif

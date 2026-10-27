#ifndef LVGL_PORT_H
#define LVGL_PORT_H

/** @file lvgl_port.h
 *  @brief LVGL 时基、刷屏、诊断信息和 UI 任务接口。
 */

#include "main.h"

/* 调试器可直接观察的快照，不依赖被编译器优化掉的局部变量。 */
typedef struct {
    uint32_t stage;
    uint32_t ui_fault;      /* 0 表示正常，否则为红屏显示的故障码。 */
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
/* 0=正常页面，1=纯红 LVGL 页面，2=直接 LCD 红屏（暂停 LVGL）。
 * 写入 3 可以在测试后重新创建正常页面。 */
extern volatile uint32_t g_lvgl_probe_mode;

/** @brief 初始化 LVGL、LCD 刷屏回调和应用界面。 */
void lvgl_port_init(void);
/** @brief 根据系统调度器推进 LVGL 毫秒时基。 */
void lvgl_port_tick_inc(uint32_t elapsed_ms);
/** @brief 将 RGB565 矩形区域刷新到 ST7789 屏幕。 */
void lvgl_port_flush(uint16_t x, uint16_t y, uint16_t w, uint16_t h,
                     const uint16_t *pixels);
/** @brief 执行周期性 LVGL 处理，并分发界面按键事件。 */
void lvgl_port_task(void *argument);

#endif

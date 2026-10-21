#ifndef LVGL_PORT_H
#define LVGL_PORT_H

#include "main.h"

/* 轻量显示适配层：当前不依赖 LVGL，后续接入时可直接绑定 flush/tick。 */
void lvgl_port_init(void);
void lvgl_port_tick_inc(uint32_t elapsed_ms);
void lvgl_port_flush(uint16_t x, uint16_t y, uint16_t w, uint16_t h,
                     const uint16_t *pixels);
void lvgl_port_task(void *argument);

#endif

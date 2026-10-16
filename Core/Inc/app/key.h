/**
  ******************************************************************************
  * @file    key.h
  * @brief   5个独立按键扫描驱动 (10ms 周期, 消抖, +/-键长按连发)
  *
  * 引脚映射 (2026-10-16 回退确认, 测试板):
  *   K1  = PB9  方向左键 (默认映射为温度"-")
  *   K2  = PE0  方向右键 (默认映射为温度"+")
  *   K3  = PE1  SET 确认键
  *   PC6      开舱门键 (原 USART6_TX, 运行时重配为 GPIO 输入)
  *   PC7      关舱门键 (原 USART6_RX, 运行时重配为 GPIO 输入)
  * 开/关键若要对调, 交换 key.c 的 s_keys 表后两项 id 即可。
  ******************************************************************************
  */
#ifndef KEY_H
#define KEY_H

#ifdef __cplusplus
extern "C" {
#endif

#include "main.h"

/* 按键有效电平: 1=按下接地(需内部上拉), 0=按下接3.3V(需内部下拉) */
#define KEY_ACTIVE_LOW       1

/* +/-键长按连发: 按住 600ms 后每 150ms 连发一次; 置 0 关闭 */
#define KEY_AUTO_REPEAT      1
#define KEY_REPEAT_DELAY_MS  600
#define KEY_REPEAT_RATE_MS   150

/* 消抖: 连续 2 次采样一致(20ms)才认状态变化 */
#define KEY_DEBOUNCE_N       2

typedef enum {
  KEY_NONE = 0,
  KEY_DEC = 1,        /* K1 左 = 温度减 */
  KEY_INC,            /* K2 右 = 温度加 */
  KEY_SET,            /* K3 确认/设置 */
  KEY_DOOR_OPEN,     /* PC6 开舱门 */
  KEY_DOOR_CLOSE,    /* PC7 关舱门 */
} key_id_t;

typedef enum { KEY_EVT_PRESS = 0, KEY_EVT_REPEAT, KEY_EVT_RELEASE } key_evt_type_t;

typedef struct {
  key_id_t      id;
  key_evt_type_t type;
} key_event_t;

/* 由 freertos.c 的 MX_FREERTOS_Init 创建 */
void KeyTask(void *argument);

/* 调试: 按键事件回显开关 (CLI 'k' 切换) */
void key_set_echo(int on);
int  key_toggle_echo(void);
void key_dump_pins(void);   /* CLI 'p': 打印5脚实时电平 */

const char *key_name(key_id_t id);

#ifdef __cplusplus
}
#endif
#endif /* KEY_H */

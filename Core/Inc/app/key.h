#ifndef APP_KEY_H
#define APP_KEY_H

#include "main.h"
#include "cmsis_os.h"

typedef enum {
    KEY_LEFT = 0,
    KEY_RIGHT,
    KEY_OK,
    KEY_UP,
    KEY_DOWN,
    KEY_COUNT
} key_id_t;

typedef enum {
    KEY_EVENT_PRESS = 0,
    KEY_EVENT_RELEASE
} key_event_type_t;

typedef struct {
    key_id_t id;
    key_event_type_t type;
} key_event_t;

/* 按键扫描线程：10 ms采样，约20 ms消抖。 */
void key_task(void *argument);

/* 非阻塞读取按键事件；返回1表示读到事件，返回0表示队列为空。 */
int key_get_event(key_event_t *event);

/* 读取当前稳定状态：1=按下，0=释放。 */
int key_is_pressed(key_id_t key);

#endif

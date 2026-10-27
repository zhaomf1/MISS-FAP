#ifndef APP_KEY_H
#define APP_KEY_H

/** @file key.h
 *  @brief 五个按键扫描、消抖和事件队列接口。
 */

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

/** @brief 按键扫描任务，每 10ms 采样，约 20ms 消抖。 */
void key_task(void *argument);

/** @brief 非阻塞读取一个按键事件。 */
int key_get_event(key_event_t *event);

/** @brief 读取消抖后的稳定状态：1 表示按下，0 表示释放。 */
int key_is_pressed(key_id_t key);

#endif

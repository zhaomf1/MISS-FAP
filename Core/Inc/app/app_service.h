#ifndef APP_SERVICE_H
#define APP_SERVICE_H
#include "incubator_model.h"
#include "temp_service.h"
#include "motor.h"

typedef enum {
    APP_SET_TEMP, APP_SET_TIME, APP_SET_DELAY, APP_START, APP_STOP,
    APP_MUTE, APP_OPEN, APP_CLOSE
} app_command_kind_t;
typedef struct { app_command_kind_t kind; int32_t value; } app_command_t;
typedef enum {
    APP_MSG_NONE, APP_MSG_BUSY, APP_MSG_SET_TEMP, APP_MSG_CONTROL_PENDING,
    APP_MSG_TEMP_OFFLINE, APP_MSG_WRITE_FAILED, APP_MSG_DOOR_FAILED,
    APP_MSG_SETTING_LOCKED
} app_message_t;
typedef struct {
    incubator_model_t run;
    temp_snapshot_t temp;
    motor_state_t door;
    uint8_t door_fault, target_pending, at_temperature, alarm, buzzer_hardware;
    uint8_t write_fault; /* 锁存写入故障，直到下一次设定值成功写入。 */
    app_message_t message;
} app_snapshot_t;

/** @brief 初始化应用状态及相关服务。 */
int app_service_init(void);

/**
 * @brief 将用户/应用命令放入服务任务队列。
 * @param kind app_command_kind_t 类型的命令。
 * @param value 命令值，例如目标温度或时间。
 * @return 非 0 表示命令已被接受。
 */
int app_service_submit(app_command_kind_t kind, int32_t value);

/**
 * @brief 复制最新应用状态，供界面或调试使用。
 * @param out 状态快照目标地址，不能为 NULL。
 */
void app_service_snapshot(app_snapshot_t *out);

/**
 * @brief 运行应用服务任务。
 * @param argument RTOS 任务参数，当前实现未使用。
 */
void app_service_task(void *argument);

/**
 * @brief 驱动实际蜂鸣器的板级接口。
 * @param enabled 非 0 开启蜂鸣器，0 关闭蜂鸣器。
 * @return 硬件更新成功返回非 0。
 * @note 默认弱实现返回 0，不修改任何 GPIO。
 */
int app_board_buzzer_set(int enabled);
#endif

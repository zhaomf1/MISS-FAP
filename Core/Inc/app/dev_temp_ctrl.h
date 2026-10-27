#ifndef APP_DEV_TEMP_CTRL_H
#define APP_DEV_TEMP_CTRL_H

#include "main.h"

/** @file dev_temp_ctrl.h
 *  @brief 温控器 Modbus 寄存器接口。
 */
#define TEMP_CTRL_SLAVE_ADDR 0x80u

/** @brief 初始化温控器通信参数。 */
int dev_temp_init(void);
/** @brief 开启或关闭温控器闭环输出。 */
int temp_ctrl_switch_ctrl_temperature(uint16_t state);
/** @brief 写入温控器设定值，单位为 0.01℃。 */
int temp_ctrl_set_temperature(uint16_t temperature);
/** @brief 读取温控器当前过程值，单位为 0.01℃。 */
int temp_ctrl_read_temperature(uint16_t *temperature);
/** @brief 读取温控器报警/状态寄存器。 */
int temp_ctrl_read_alarm(uint16_t *alarm_info);
/** @brief 设置温控器通信超时时间，单位为秒。 */
int temp_ctrl_set_timeout(uint16_t second);
/** @brief 请求温控器执行系统复位。 */
int temp_ctrl_system_reset(void);

#endif

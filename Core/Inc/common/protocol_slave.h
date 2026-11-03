#ifndef APP_PROTOCOL_SLAVE_H
#define APP_PROTOCOL_SLAVE_H

#include "main.h"

/** @brief 启动 USART1 Modbus RTU 从站接收；地址从预留 Flash 扇区恢复。 */
int protocol_slave_init(void);

/** @brief Modbus RTU 从站任务入口。 */
void protocol_slave_task(void *argument);

/** @brief 查询 USART1 是否已由 Modbus 协议占用，供 printf 重定向避让。 */
uint8_t protocol_slave_uart_active(void);

#endif

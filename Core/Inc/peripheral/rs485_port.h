#ifndef APP_RS485_PORT_H
#define APP_RS485_PORT_H

#include "main.h"

#define RS485_PORT_MAX_FRAME 256u

int rs485_port_init(void);
int rs485_port_exchange(const uint8_t *tx, uint16_t tx_len,
                        uint8_t *rx, uint16_t rx_capacity,
                        uint16_t *rx_len, uint32_t timeout_ms);
void rs485_port_on_idle_irq(void);

#endif

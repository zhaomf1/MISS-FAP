#ifndef APP_DEV_TEMP_CTRL_H
#define APP_DEV_TEMP_CTRL_H

#include "main.h"

#define TEMP_CTRL_SLAVE_ADDR 0x80u

int dev_temp_init(void);
int temp_ctrl_switch_ctrl_temperature(uint16_t state);
int temp_ctrl_set_temperature(uint16_t temperature);
int temp_ctrl_read_temperature(uint16_t *temperature);
int temp_ctrl_read_alarm(uint16_t *alarm_info);
int temp_ctrl_set_timeout(uint16_t second);
int temp_ctrl_system_reset(void);

#endif

#include "dev_temp_ctrl.h"
#include "modbus_rtu.h"

int temp_ctrl_switch_ctrl_temperature(uint16_t state) { return modbus_write_single_coil(TEMP_CTRL_SLAVE_ADDR, 0x0000, state != 0); }
int temp_ctrl_set_temperature(uint16_t temperature) { return modbus_write_single_register(TEMP_CTRL_SLAVE_ADDR, 0x0000, temperature); }
int temp_ctrl_read_temperature(uint16_t *temperature)
{
    if (!temperature) return MODBUS_ERR_PARAM;
    return modbus_read_input_registers(TEMP_CTRL_SLAVE_ADDR, 0x0000, 1, temperature);
}
int temp_ctrl_read_alarm(uint16_t *alarm_info)
{
    if (!alarm_info) return MODBUS_ERR_PARAM;
    return modbus_read_input_registers(TEMP_CTRL_SLAVE_ADDR, 0x0003, 1, alarm_info);
}
int temp_ctrl_set_timeout(uint16_t second) { return modbus_write_single_register(TEMP_CTRL_SLAVE_ADDR, 0x0009, second); }
int temp_ctrl_system_reset(void) { return modbus_write_single_coil(TEMP_CTRL_SLAVE_ADDR, 0x0001, 1); }
int dev_temp_init(void) { return temp_ctrl_set_timeout(3600) == MODBUS_OK ? 0 : -1; }

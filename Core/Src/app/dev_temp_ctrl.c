#include "dev_temp_ctrl.h"
#include "modbus_rtu.h"

/*
 * 温控器驱动适配层：把 Modbus 寄存器地址封装成有意义的操作接口。
 * 上层不直接拼接协议帧，温度值按 0.01℃ 传递。
 */

/* 写控制线圈：state 非 0 表示开启温度控制。 */
int temp_ctrl_switch_ctrl_temperature(uint16_t state) { return modbus_write_single_coil(TEMP_CTRL_SLAVE_ADDR, 0x0000, state != 0); }
/* 写温控器设定值，单位为 0.01℃。 */
int temp_ctrl_set_temperature(uint16_t temperature) { return modbus_write_single_register(TEMP_CTRL_SLAVE_ADDR, 0x0000, temperature); }
/* 读取当前过程温度 PV。 */
int temp_ctrl_read_temperature(uint16_t *temperature)
{
    if (!temperature) return MODBUS_ERR_PARAM;
    return modbus_read_input_registers(TEMP_CTRL_SLAVE_ADDR, 0x0000, 1, temperature);
}
/* 读取温控器报警寄存器。 */
int temp_ctrl_read_alarm(uint16_t *alarm_info)
{
    if (!alarm_info) return MODBUS_ERR_PARAM;
    return modbus_read_input_registers(TEMP_CTRL_SLAVE_ADDR, 0x0003, 1, alarm_info);
}
/* 设置通信超时时间，单位为秒。 */
int temp_ctrl_set_timeout(uint16_t second) { return modbus_write_single_register(TEMP_CTRL_SLAVE_ADDR, 0x0009, second); }
/* 请求温控器执行系统复位。 */
int temp_ctrl_system_reset(void) { return modbus_write_single_coil(TEMP_CTRL_SLAVE_ADDR, 0x0001, 1); }
/* 初始化温控器默认超时时间。 */
int dev_temp_init(void)
{
    /* 返回原始 Modbus 结果码，温控服务据此输出初始化失败的具体原因。 */
    return temp_ctrl_set_timeout(3600);
}

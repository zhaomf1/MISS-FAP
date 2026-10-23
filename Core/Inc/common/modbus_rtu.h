#ifndef APP_MODBUS_RTU_H
#define APP_MODBUS_RTU_H

#include "main.h"

#define MODBUS_OK 0
#define MODBUS_ERR_PARAM -1
#define MODBUS_ERR_PORT -2
#define MODBUS_ERR_TIMEOUT -3
#define MODBUS_ERR_FRAME -4
#define MODBUS_ERR_CRC -5
#define MODBUS_ERR_EXCEPTION -6
#define MODBUS_ERR_RESPONSE -7

#define MODBUS_FC_READ_HOLDING_REGS 0x03
#define MODBUS_FC_READ_INPUT_REGS   0x04
#define MODBUS_FC_WRITE_SINGLE_COIL 0x05
#define MODBUS_FC_WRITE_SINGLE_REG  0x06
#define MODBUS_FC_WRITE_MULTIPLE_REGS 0x10

int modbus_rtu_init(void);
uint16_t modbus_crc16(const uint8_t *data, uint16_t len);
int modbus_read_input_registers(uint8_t slave, uint16_t start, uint16_t count, uint16_t *dest);
int modbus_read_holding_registers(uint8_t slave, uint16_t start, uint16_t count, uint16_t *dest);
int modbus_write_single_register(uint8_t slave, uint16_t addr, uint16_t value);
int modbus_write_single_coil(uint8_t slave, uint16_t addr, uint8_t state);

#endif

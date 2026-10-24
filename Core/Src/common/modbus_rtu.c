#include "modbus_rtu.h"
#include "rs485_port.h"
#include "cmsis_os2.h"

#define MODBUS_TIMEOUT_MS 200u
#define MODBUS_RETRIES 2u

static uint8_t s_initialized;

uint16_t modbus_crc16(const uint8_t *data, uint16_t len)
{
    uint16_t crc = 0xFFFFu;
    while (len--) {
        crc ^= *data++;
        for (uint8_t i = 0; i < 8; ++i) crc = (crc & 1u) ? (crc >> 1) ^ 0xA001u : (crc >> 1);
    }
    return crc;
}

int modbus_rtu_init(void)
{
    int ret = rs485_port_init();
    s_initialized = (ret == 0);
    return ret;
}

static int transaction(uint8_t slave, uint8_t func, const uint8_t *payload, uint16_t payload_len,
                       uint8_t *data, uint16_t expected_data_len)
{
    uint8_t tx[256], rx[RS485_PORT_MAX_FRAME];
    uint16_t rx_len = 0, crc;
    if (!s_initialized || slave == 0 || payload_len > 250 || expected_data_len > 250) return MODBUS_ERR_PARAM;
    tx[0] = slave; tx[1] = func;
    for (uint16_t i = 0; i < payload_len; ++i) tx[2 + i] = payload[i];
    crc = modbus_crc16(tx, (uint16_t)(payload_len + 2));
    tx[payload_len + 2] = (uint8_t)crc; tx[payload_len + 3] = (uint8_t)(crc >> 8);

    for (uint8_t attempt = 0; attempt <= MODBUS_RETRIES; ++attempt) {
        int port_ret = rs485_port_exchange(tx, (uint16_t)(payload_len + 4), rx, sizeof(rx), &rx_len, MODBUS_TIMEOUT_MS);
        if (port_ret != 0) { if (attempt == MODBUS_RETRIES) return port_ret == -4 ? MODBUS_ERR_TIMEOUT : MODBUS_ERR_PORT; osDelay(10); continue; }
        if (rx_len < 5 || rx[0] != slave || (rx[1] != func && rx[1] != (uint8_t)(func | 0x80))) { if (attempt == MODBUS_RETRIES) return MODBUS_ERR_RESPONSE; continue; }
        crc = modbus_crc16(rx, (uint16_t)(rx_len - 2));
        if (rx[rx_len - 2] != (uint8_t)crc || rx[rx_len - 1] != (uint8_t)(crc >> 8)) { if (attempt == MODBUS_RETRIES) return MODBUS_ERR_CRC; continue; }
        if (rx[1] & 0x80) return MODBUS_ERR_EXCEPTION;
        if (func == MODBUS_FC_READ_INPUT_REGS || func == MODBUS_FC_READ_HOLDING_REGS) {
            if (rx_len < 5 || rx[2] != expected_data_len || rx_len != (uint16_t)(5 + expected_data_len) || (expected_data_len & 1u)) return MODBUS_ERR_RESPONSE;
            /* Preserve network-order BYTES; read_regs assembles signed/unsigned
             * 16-bit register values afterwards. The former uint8_t word cast
             * discarded the high byte and left half of raw[] uninitialized. */
            for (uint16_t i = 0; i < expected_data_len; ++i) data[i] = rx[3 + i];
        } else {
            if(rx_len != 8 || rx[1] != func) return MODBUS_ERR_RESPONSE;
            /* FC05/06 must echo both address and value, not just frame length. */
            for(uint16_t i = 0; i < 4; ++i)
                if(rx[2 + i] != payload[i]) return MODBUS_ERR_RESPONSE;
        }
        return MODBUS_OK;
    }
    return MODBUS_ERR_PORT;
}

static int read_regs(uint8_t slave, uint8_t func, uint16_t start, uint16_t count, uint16_t *dest)
{
    uint8_t p[4], raw[250];
    if (!dest || count == 0 || count > 125) return MODBUS_ERR_PARAM;
    p[0] = (uint8_t)(start >> 8); p[1] = (uint8_t)start; p[2] = (uint8_t)(count >> 8); p[3] = (uint8_t)count;
    int ret = transaction(slave, func, p, 4, raw, (uint16_t)(count * 2));
    if (ret == MODBUS_OK) for (uint16_t i = 0; i < count; ++i) dest[i] = (uint16_t)((raw[i * 2] << 8) | raw[i * 2 + 1]);
    return ret;
}

int modbus_read_input_registers(uint8_t slave, uint16_t start, uint16_t count, uint16_t *dest) { return read_regs(slave, MODBUS_FC_READ_INPUT_REGS, start, count, dest); }
int modbus_read_holding_registers(uint8_t slave, uint16_t start, uint16_t count, uint16_t *dest) { return read_regs(slave, MODBUS_FC_READ_HOLDING_REGS, start, count, dest); }

int modbus_write_single_register(uint8_t slave, uint16_t addr, uint16_t value)
{
    uint8_t p[4] = {(uint8_t)(addr >> 8), (uint8_t)addr, (uint8_t)(value >> 8), (uint8_t)value};
    return transaction(slave, MODBUS_FC_WRITE_SINGLE_REG, p, 4, NULL, 0);
}

int modbus_write_single_coil(uint8_t slave, uint16_t addr, uint8_t state)
{
    uint8_t p[4] = {(uint8_t)(addr >> 8), (uint8_t)addr, state ? 0xFFu : 0x00u, 0x00u};
    return transaction(slave, MODBUS_FC_WRITE_SINGLE_COIL, p, 4, NULL, 0);
}

#include "protocol_slave.h"
#include "usart.h"
#include "app_service.h"
#include "cmsis_os2.h"
#include <string.h>

/* USART1 与上位机通信，USART3 仍保留给温控器 Modbus 主站。 */
#define PROTOCOL_RX_BUFFER_SIZE 256u
#define PROTOCOL_FRAME_MAX      256u
#define PROTOCOL_DEFAULT_ADDR   1u
#define PROTOCOL_GAP_MS         2u
#define PROTOCOL_ADDR_FLASH     0x080E0000u /* 预留的 STM32F405 第 11 扇区起始地址 */
#define PROTOCOL_ADDR_MAGIC     0x4D42u
#define PROTOCOL_ADDR_VALID     0xA55Au
#define PROTOCOL_ADDR_SLOTS     16384u

/* 协议版本字节：保留、主版本、次版本、微版本，即 V1.0.0。 */
#define PROTOCOL_VERSION_WORD0  0x0001u
#define PROTOCOL_VERSION_WORD1  0x0000u

/* Modbus 标准异常码。 */
#define MODBUS_EXCEPTION_FUNCTION 0x01u
#define MODBUS_EXCEPTION_ADDRESS  0x02u
#define MODBUS_EXCEPTION_VALUE    0x03u
#define MODBUS_EXCEPTION_FAILURE  0x04u
#define MODBUS_EXCEPTION_BUSY     0x06u

static uint8_t s_rx_byte;
static uint8_t s_rx_ring[PROTOCOL_RX_BUFFER_SIZE];
static volatile uint16_t s_rx_head;
static volatile uint16_t s_rx_tail;
static volatile uint8_t s_rx_overflow;
static volatile uint8_t s_uart_active;
static uint8_t s_slave_address = PROTOCOL_DEFAULT_ADDR;
static uint32_t s_next_address_slot;

/* 顺序扫描预留 Flash 地址日志，恢复最后一条有效从站地址记录。 */
static uint8_t load_slave_address(void)
{
    const volatile uint16_t *records = (const volatile uint16_t *)PROTOCOL_ADDR_FLASH;
    uint8_t address = PROTOCOL_DEFAULT_ADDR;
    uint32_t slot;

    s_next_address_slot = PROTOCOL_ADDR_SLOTS;
    for (slot = 0u; slot < PROTOCOL_ADDR_SLOTS; ++slot) {
        uint32_t offset = slot * 4u;
        uint16_t value = records[offset];
        uint16_t inverse = records[offset + 1u];
        uint16_t magic = records[offset + 2u];
        uint16_t valid = records[offset + 3u];

        if (value == 0xFFFFu && inverse == 0xFFFFu && magic == 0xFFFFu && valid == 0xFFFFu) {
            s_next_address_slot = slot;
            break;
        }
        if (magic == PROTOCOL_ADDR_MAGIC && valid == PROTOCOL_ADDR_VALID &&
            value >= 1u && value <= 247u && inverse == (uint16_t)~value)
            address = (uint8_t)value;
    }
    return address;
}

/* 追加写入地址日志，避免每次修改地址都擦除整个 Flash 扇区。 */
static int save_slave_address(uint8_t address)
{
    FLASH_EraseInitTypeDef erase;
    uint32_t sector_error = 0u;
    uint32_t record_address;
    HAL_StatusTypeDef status;

    if (address == s_slave_address) return 1;
    HAL_FLASH_Unlock();
    if (s_next_address_slot >= PROTOCOL_ADDR_SLOTS) {
        memset(&erase, 0, sizeof(erase));
        erase.TypeErase = FLASH_TYPEERASE_SECTORS;
        erase.Sector = FLASH_SECTOR_11;
        erase.NbSectors = 1u;
        erase.VoltageRange = FLASH_VOLTAGE_RANGE_3;
        status = HAL_FLASHEx_Erase(&erase, &sector_error);
        if (status == HAL_OK) s_next_address_slot = 0u;
    } else {
        status = HAL_OK;
    }
    record_address = PROTOCOL_ADDR_FLASH + s_next_address_slot * 8u;
    if (status == HAL_OK)
        status = HAL_FLASH_Program(FLASH_TYPEPROGRAM_HALFWORD, record_address,
                                   address);
    if (status == HAL_OK)
        status = HAL_FLASH_Program(FLASH_TYPEPROGRAM_HALFWORD, record_address + 2u,
                                   (uint16_t)~address);
    if (status == HAL_OK)
        status = HAL_FLASH_Program(FLASH_TYPEPROGRAM_HALFWORD, record_address + 4u,
                                   PROTOCOL_ADDR_MAGIC);
    if (status == HAL_OK)
        status = HAL_FLASH_Program(FLASH_TYPEPROGRAM_HALFWORD, record_address + 6u,
                                   PROTOCOL_ADDR_VALID);
    HAL_FLASH_Lock();
    if (status != HAL_OK) return 0;
    s_slave_address = address;
    ++s_next_address_slot;
    return 1;
}

/* Modbus CRC16，CRC 低字节先发送。 */
static uint16_t protocol_crc16(const uint8_t *data, uint16_t length)
{
    uint16_t crc = 0xFFFFu;
    uint8_t bit;

    while (length-- != 0u) {
        crc ^= *data++;
        for (bit = 0u; bit < 8u; ++bit)
            crc = (crc & 1u) ? (uint16_t)((crc >> 1) ^ 0xA001u) : (uint16_t)(crc >> 1);
    }
    return crc;
}

/* 将 16 位寄存器按 Modbus 大端顺序写入应答帧。 */
static void put_u16(uint8_t *out, uint16_t value)
{
    out[0] = (uint8_t)(value >> 8);
    out[1] = (uint8_t)value;
}

/* 添加 CRC 并通过 USART1 发送完整 RTU 帧。 */
static void send_frame(uint8_t *frame, uint16_t length)
{
    uint16_t crc = protocol_crc16(frame, length);
    frame[length++] = (uint8_t)crc;
    frame[length++] = (uint8_t)(crc >> 8);
    (void)HAL_UART_Transmit(&huart1, frame, length, 100u);
}

/* 发送 Modbus 异常响应；广播请求按协议不返回响应。 */
static void send_exception(uint8_t address, uint8_t function, uint8_t exception)
{
    uint8_t response[5];
    if (address == 0u) return;
    response[0] = address;
    response[1] = (uint8_t)(function | 0x80u);
    response[2] = exception;
    send_frame(response, 3u);
}

/* 将应用的内部阶段转换为协议定义的 0~7 工作状态值。 */
static uint16_t work_status(const app_snapshot_t *snapshot)
{
    switch (snapshot->run.phase) {
    case INC_RUNNING: return 1u;
    case INC_DELAY: return 2u;
    case INC_COOL_REQUEST:
    case INC_COOLING: return 3u;
    case INC_STORAGE: return 4u;
    case INC_FINISHED:
    case INC_ERROR: return 5u; /* 终止阶段；具体异常原因另由 0x0035 报告。 */
    case INC_IDLE:
    default: return 0u;
    }
}

/* 汇总协议 0x0003/0x0035 中定义的故障位(舱门不再产生故障位)。 */
static uint16_t fault_bits(const app_snapshot_t *snapshot)
{
    uint16_t bits = 0u;
    if (snapshot->temp.alarm != 0u) bits |= (uint16_t)(1u << 0);
    if (!snapshot->temp.online || !snapshot->temp.valid) bits |= (uint16_t)(1u << 1);
    if (snapshot->write_fault) bits |= (uint16_t)(1u << 3);
    return bits;
}

/* 读取一个协议寄存器；未定义地址返回 0，调用方将回异常码 0x02。 */
static int read_register(uint16_t address, const app_snapshot_t *snapshot, uint16_t *value)
{
    switch (address) {
    case 0x0000u: *value = PROTOCOL_VERSION_WORD0; return 1;
    case 0x0001u: *value = PROTOCOL_VERSION_WORD1; return 1;
    case 0x0002u: *value = s_slave_address; return 1;
    case 0x0003u:
    case 0x0035u: *value = fault_bits(snapshot); return 1;
    case 0x0020u:
        *value = (uint16_t)(snapshot->run.target_centi / 10); /* 0.01℃转为协议0.1℃ */
        return 1;
    case 0x0021u:
        *value = (uint16_t)(snapshot->run.time_s / 6u); /* 秒转为0.1分钟 */
        return 1;
    case 0x0022u: *value = snapshot->run.delay_s; return 1;
    case 0x0030u:
        *value = (uint16_t)(int16_t)(snapshot->temp.pv_centi / 10);
        return 1;
    case 0x0031u: *value = work_status(snapshot); return 1;
    case 0x0032u: {
        uint32_t remaining = (snapshot->run.remaining_ms + 5999u) / 6000u;
        *value = (uint16_t)(remaining > 6000u ? 6000u : remaining);
        return 1;
    }
    case 0x0033u:
        /* 0=已关到位, 1=已开到位, 2=开/关动作中或位置未知 */
        if (snapshot->door == MOTOR_CLOSED) *value = 0u;
        else if (snapshot->door == MOTOR_OPEN) *value = 1u;
        else *value = 2u;
        return 1;
    case 0x0034u: {
        uint32_t remaining = (snapshot->run.delay_remaining_ms + 999u) / 1000u;
        *value = (uint16_t)(remaining > 600u ? 600u : remaining);
        return 1;
    }
    default: return 0;
    }
}

/* 检查一次 FC06 写入，并将设置或动作放入应用服务队列。 */
static uint8_t write_register(uint16_t address, uint16_t value)
{
    app_command_kind_t command;
    int32_t app_value;

    if (address == 0x0002u) {
        if (value < 1u || value > 247u) return MODBUS_EXCEPTION_VALUE;
        if (!save_slave_address((uint8_t)value)) return MODBUS_EXCEPTION_FAILURE;
        return 0u;
    }

    switch (address) {
    case 0x0010u: if (value != 1u) return MODBUS_EXCEPTION_VALUE; command = APP_START; app_value = 0; break;
    case 0x0011u: if (value != 1u) return MODBUS_EXCEPTION_VALUE; command = APP_STOP; app_value = 0; break;
    case 0x0012u: if (value != 1u) return MODBUS_EXCEPTION_VALUE; command = APP_OPEN; app_value = 0; break;
    case 0x0013u: if (value != 1u) return MODBUS_EXCEPTION_VALUE; command = APP_CLOSE; app_value = 0; break;
    case 0x0014u: if (value != 1u) return MODBUS_EXCEPTION_VALUE; command = APP_MUTE; app_value = 0; break;
    case 0x0020u:
        if (value > 1000u) return MODBUS_EXCEPTION_VALUE;
        command = APP_SET_TEMP;
        app_value = (int32_t)value * 10; /* 协议0.1℃转为应用0.01℃ */
        break;
    case 0x0021u:
        if (value < 1u || value > 6000u) return MODBUS_EXCEPTION_VALUE;
        command = APP_SET_TIME;
        app_value = (int32_t)value * 6; /* 协议0.1分钟转为秒 */
        break;
    case 0x0022u:
        if (value > 600u) return MODBUS_EXCEPTION_VALUE;
        command = APP_SET_DELAY;
        app_value = value; /* 单位秒；0表示立即进入降温流程 */
        break;
    default: return MODBUS_EXCEPTION_ADDRESS;
    }

    return app_service_submit(command, app_value) ? 0u : MODBUS_EXCEPTION_BUSY;
}

/* 处理一帧已按静默间隔切分的 Modbus RTU 请求。 */
static void process_frame(const uint8_t *request, uint16_t length)
{
    uint8_t address, function;
    uint16_t crc, start, count;
    app_snapshot_t snapshot;

    if (length < 4u) return;
    address = request[0];
    function = request[1];
    if (address != 0u && address != s_slave_address) return;
    crc = protocol_crc16(request, (uint16_t)(length - 2u));
    if (request[length - 2u] != (uint8_t)crc || request[length - 1u] != (uint8_t)(crc >> 8)) return;

    if (function == 0x03u) {
        uint8_t response[5u + 2u * 125u];
        if (length != 8u) { send_exception(address, function, MODBUS_EXCEPTION_VALUE); return; }
        start = (uint16_t)(((uint16_t)request[2] << 8) | request[3]);
        count = (uint16_t)(((uint16_t)request[4] << 8) | request[5]);
        if (count == 0u || count > 125u) { send_exception(address, function, MODBUS_EXCEPTION_VALUE); return; }
        app_service_snapshot(&snapshot);
        for (uint16_t i = 0u; i < count; ++i) {
            uint16_t value;
            if (!read_register((uint16_t)(start + i), &snapshot, &value)) {
                send_exception(address, function, MODBUS_EXCEPTION_ADDRESS);
                return;
            }
            put_u16(&response[3u + 2u * i], value);
        }
        if (address == 0u) return;
        response[0] = address;
        response[1] = function;
        response[2] = (uint8_t)(count * 2u);
        send_frame(response, (uint16_t)(3u + count * 2u));
        return;
    }

    if (function == 0x06u) {
        uint8_t exception;
        uint16_t register_address, value;
        if (length != 8u) { send_exception(address, function, MODBUS_EXCEPTION_VALUE); return; }
        register_address = (uint16_t)(((uint16_t)request[2] << 8) | request[3]);
        value = (uint16_t)(((uint16_t)request[4] << 8) | request[5]);
        exception = write_register(register_address, value);
        if (exception != 0u) { send_exception(address, function, exception); return; }
        if (address == 0u) return;
        /* FC06 正常响应原样回显寄存器地址和值。 */
        {
            uint8_t response[8];
            memcpy(response, request, 6u);
            send_frame(response, 6u);
        }
        return;
    }

    send_exception(address, function, MODBUS_EXCEPTION_FUNCTION);
}

/* HAL 每收到一个字节进入此回调；只入环形缓冲，复杂处理留给任务。 */
void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart)
{
    if (huart->Instance != USART1) return;
    {
        uint16_t head = s_rx_head;
        if ((uint16_t)(head - s_rx_tail) < PROTOCOL_RX_BUFFER_SIZE) {
            s_rx_ring[head & (PROTOCOL_RX_BUFFER_SIZE - 1u)] = s_rx_byte;
            s_rx_head = (uint16_t)(head + 1u);
        } else {
            s_rx_overflow = 1u;
        }
    }
    (void)HAL_UART_Receive_IT(&huart1, &s_rx_byte, 1u);
}

/* USART1 接收异常时清空 HAL 状态并重新开启单字节接收。 */
void HAL_UART_ErrorCallback(UART_HandleTypeDef *huart)
{
    if (huart->Instance != USART1) return;
    (void)HAL_UART_AbortReceive(huart);
    (void)HAL_UART_Receive_IT(&huart1, &s_rx_byte, 1u);
}

int protocol_slave_init(void)
{
    s_slave_address = load_slave_address();
    s_rx_head = 0u;
    s_rx_tail = 0u;
    s_rx_overflow = 0u;
    s_uart_active = 1u;
    if (HAL_UART_Receive_IT(&huart1, &s_rx_byte, 1u) != HAL_OK) {
        s_uart_active = 0u;
        return 0;
    }
    return 1;
}

uint8_t protocol_slave_uart_active(void)
{
    return s_uart_active;
}

/* 以 2ms 静默间隔划分 RTU 帧，并在任务上下文执行寄存器读写。 */
void protocol_slave_task(void *argument)
{
    uint8_t frame[PROTOCOL_FRAME_MAX];
    uint16_t frame_length = 0u;
    uint32_t last_byte_ms = 0u;
    (void)argument;

    for (;;) {
        while (s_rx_tail != s_rx_head) {
            uint16_t tail = s_rx_tail;
            uint8_t byte = s_rx_ring[tail & (PROTOCOL_RX_BUFFER_SIZE - 1u)];
            s_rx_tail = (uint16_t)(tail + 1u);
            if (frame_length < sizeof(frame)) frame[frame_length++] = byte;
            else frame_length = 0u;
            last_byte_ms = HAL_GetTick();
        }

        if (s_rx_overflow) {
            s_rx_overflow = 0u;
            frame_length = 0u;
        } else if (frame_length != 0u && (uint32_t)(HAL_GetTick() - last_byte_ms) >= PROTOCOL_GAP_MS) {
            process_frame(frame, frame_length);
            frame_length = 0u;
        }
        osDelay(1u);
    }
}

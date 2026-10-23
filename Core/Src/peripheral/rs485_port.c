#include "rs485_port.h"
#include "usart.h"
#include "dma.h"
#include "cmsis_os2.h"
#include <string.h>

#define RS485_RX_DMA_SIZE RS485_PORT_MAX_FRAME

static uint8_t s_rx_dma[RS485_RX_DMA_SIZE];
static uint8_t s_rx_frame[RS485_RX_DMA_SIZE];
static uint16_t s_rx_len;
static osMutexId_t s_bus_mutex;
static osSemaphoreId_t s_rx_done;
static uint8_t s_initialized;

/* Board-specific DE/RE control can be supplied by the board port later. */
__WEAK void rs485_port_set_transmit(int enable) { (void)enable; }

int rs485_port_init(void)
{
    if (s_initialized) return 0;
    s_bus_mutex = osMutexNew(NULL);
    s_rx_done = osSemaphoreNew(1, 0, NULL);
    if (s_bus_mutex == NULL || s_rx_done == NULL) return -1;
    s_rx_len = 0;
    s_initialized = 1;
    __HAL_UART_ENABLE_IT(&huart3, UART_IT_IDLE);
    return HAL_UART_Receive_DMA(&huart3, s_rx_dma, sizeof(s_rx_dma)) == HAL_OK ? 0 : -1;
}

static void rx_restart(void)
{
    HAL_UART_DMAStop(&huart3);
    memset(s_rx_dma, 0, sizeof(s_rx_dma));
    (void)HAL_UART_Receive_DMA(&huart3, s_rx_dma, sizeof(s_rx_dma));
    __HAL_UART_CLEAR_IDLEFLAG(&huart3);
    __HAL_UART_ENABLE_IT(&huart3, UART_IT_IDLE);
}

void rs485_port_on_idle_irq(void)
{
    uint16_t received;
    if (!s_initialized) return;
    __HAL_UART_CLEAR_IDLEFLAG(&huart3);
    received = (uint16_t)(sizeof(s_rx_dma) - __HAL_DMA_GET_COUNTER(&hdma_usart3_rx));
    if (received > sizeof(s_rx_frame)) received = sizeof(s_rx_frame);
    memcpy(s_rx_frame, s_rx_dma, received);
    s_rx_len = received;
    (void)osSemaphoreRelease(s_rx_done);
}

int rs485_port_exchange(const uint8_t *tx, uint16_t tx_len,
                        uint8_t *rx, uint16_t rx_capacity,
                        uint16_t *rx_len, uint32_t timeout_ms)
{
    if (!s_initialized || !tx || tx_len == 0 || !rx || !rx_len || rx_capacity == 0) return -1;
    if (tx_len > RS485_PORT_MAX_FRAME) return -1;
    if (osMutexAcquire(s_bus_mutex, timeout_ms) != osOK) return -2;

    (void)osSemaphoreAcquire(s_rx_done, 0);
    rx_restart();
    rs485_port_set_transmit(1);
    if (HAL_UART_Transmit(&huart3, (uint8_t *)tx, tx_len, timeout_ms) != HAL_OK) {
        rs485_port_set_transmit(0);
        osMutexRelease(s_bus_mutex);
        return -3;
    }
    while (__HAL_UART_GET_FLAG(&huart3, UART_FLAG_TC) == RESET) { }
    rs485_port_set_transmit(0);

    if (osSemaphoreAcquire(s_rx_done, timeout_ms) != osOK) {
        rx_restart();
        osMutexRelease(s_bus_mutex);
        return -4;
    }
    if (s_rx_len > rx_capacity) {
        rx_restart();
        osMutexRelease(s_bus_mutex);
        return -5;
    }
    memcpy(rx, s_rx_frame, s_rx_len);
    *rx_len = s_rx_len;
    rx_restart();
    osMutexRelease(s_bus_mutex);
    return 0;
}

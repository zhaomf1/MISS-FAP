#include "temp_service.h"
#include "temp_calibration.h"
#include "dev_temp_ctrl.h"
#include "modbus_rtu.h"
#include "cmsis_os2.h"
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "semphr.h"
#include <string.h>

typedef struct { int32_t value; uint32_t id; } temp_command_t;
static SemaphoreHandle_t s_lock;
static QueueHandle_t s_commands;
static temp_snapshot_t s_state;

int temp_service_init(void)
{
    memset(&s_state, 0, sizeof(s_state));
    s_lock = xSemaphoreCreateMutex();
    s_commands = xQueueCreate(1, sizeof(temp_command_t));
    return s_lock && s_commands;
}

int temp_service_set_target(int32_t value, uint32_t id)
{
    temp_command_t c = {value, id};
    return xQueueSend(s_commands, &c, 0) == pdPASS;
}

void temp_service_snapshot(temp_snapshot_t *out)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    *out = s_state;
    xSemaphoreGive(s_lock);
    /* Successful old data must never be presented as a live measurement. */
    if(!out->valid || (uint32_t)(HAL_GetTick()-out->last_good_ms)>2500u)
        out->valid = 0;
}

void temp_service_task(void *argument)
{
    temp_command_t command;
    uint8_t initialized = 0, failures = 0;
    (void)argument;
    for(;;) {
        temp_snapshot_t next;
        uint16_t pv = 0, alarm = 0;
        int good = 0;
        temp_service_snapshot(&next);
        if(!initialized) initialized = dev_temp_init() == 0;
        if(xQueueReceive(s_commands, &command, 0) == pdPASS) {
            int32_t raw = temp_calibration_to_module(command.value);
            /* Validate the conversion before narrowing to the protocol INT16S. */
            int ok = initialized && raw >= -32768 && raw <= 32767;
            if(ok) ok = temp_ctrl_set_temperature((uint16_t)(int16_t)raw) == MODBUS_OK;
            if(ok) ok = temp_ctrl_switch_ctrl_temperature(1) == MODBUS_OK;
            next.completed_id = command.id;
            next.write_ok = ok ? 1 : 0;
            if(ok) {
                next.applied_centi = command.value;
                next.target_valid = 1;
            } else {
                /* SV may have changed before a later enable ACK failed. */
                next.target_valid = 0;
            }
        }
        if(initialized && temp_ctrl_read_temperature(&pv) == MODBUS_OK &&
           temp_ctrl_read_alarm(&alarm) == MODBUS_OK) good = 1;
        if(good) {
            failures = 0;
            next.online = next.valid = 1;
            next.pv_centi = temp_calibration_to_display((int16_t)pv);
            next.alarm = alarm;
            next.last_good_ms = HAL_GetTick();
        } else {
            if(failures < 3) failures++;
            next.valid = 0;
            if(failures >= 3) next.online = 0;
        }
        xSemaphoreTake(s_lock, portMAX_DELAY);
        s_state = next;
        xSemaphoreGive(s_lock);
        osDelay(500);
    }
}

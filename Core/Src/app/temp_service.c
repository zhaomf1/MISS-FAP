#include "temp_service.h"
#include "temp_calibration.h"
#include "dev_temp_ctrl.h"
#include "modbus_rtu.h"
#include "cmsis_os2.h"
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "semphr.h"
#include <stdio.h>
#include <string.h>

/*
 * 温控服务模块：异步读取 PV、报警和设定值，并把 Modbus 完成结果交给应用服务。
 * 该模块只允许一个未完成的设定值写入，避免多个请求覆盖完成编号。
 */

typedef struct { int32_t value; uint32_t id; } temp_command_t;
static SemaphoreHandle_t s_lock;
static QueueHandle_t s_commands;
static temp_snapshot_t s_state;

/* Modbus 错误码名称供温控日志使用，便于区分超时、CRC和设备异常响应。 */
static const char *modbus_result_name(int result)
{
    switch(result) {
    case MODBUS_OK: return "OK";
    case MODBUS_ERR_PARAM: return "PARAM";
    case MODBUS_ERR_PORT: return "PORT";
    case MODBUS_ERR_TIMEOUT: return "TIMEOUT";
    case MODBUS_ERR_FRAME: return "FRAME";
    case MODBUS_ERR_CRC: return "CRC";
    case MODBUS_ERR_EXCEPTION: return "EXCEPTION";
    case MODBUS_ERR_RESPONSE: return "RESPONSE";
    default: return "UNKNOWN";
    }
}

/* 创建温控命令队列并清空初始快照。 */
int temp_service_init(void)
{
    memset(&s_state, 0, sizeof(s_state));
    s_lock = xSemaphoreCreateMutex();
    s_commands = xQueueCreate(1, sizeof(temp_command_t));
    return s_lock && s_commands;
}

/* 提交一次液体温度设定；实际 Modbus 操作由温控任务执行。 */
int temp_service_set_target(int32_t value, uint32_t id)
{
    temp_command_t c = {value, id};
    return xQueueSend(s_commands, &c, 0) == pdPASS;
}

/* 复制最新温度、报警和通信状态。 */
void temp_service_snapshot(temp_snapshot_t *out)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    *out = s_state;
    xSemaphoreGive(s_lock);
            /* 旧的成功数据不能继续被当作实时测量值使用。 */
    if(!out->valid || (uint32_t)(HAL_GetTick()-out->last_good_ms)>2500u)
        out->valid = 0;
}

/* 周期读取温控器并完成待处理的设定值写入。 */
void temp_service_task(void *argument)
{
    temp_command_t command;
    uint8_t initialized = 0, output_off_confirmed = 0;
    uint8_t failures = 0, poll_failure_logged = 0;
    uint32_t last_init_log_ms = 0, last_poll_log_ms = 0;
    (void)argument;
    for(;;) {
        temp_snapshot_t next;
        uint16_t pv = 0, alarm = 0;
        int good = 0;
        temp_service_snapshot(&next);
        if(!initialized) {
            int init_result = dev_temp_init();
            initialized = init_result == MODBUS_OK;
            if(initialized) {
                printf("[temp] controller init OK: timeout register configured\r\n");
            } else if(last_init_log_ms == 0 || (uint32_t)(HAL_GetTick() - last_init_log_ms) >= 5000u) {
                last_init_log_ms = HAL_GetTick();
                printf("[temp] controller init FAILED: timeout write=%s(%d), retrying\r\n",
                       modbus_result_name(init_result), init_result);
            }
        }
        /* 上电时明确关闭温控输出，防止温控器保留上次运行状态；用户设温后再开启。 */
        if(initialized && !output_off_confirmed) {
            int off_result = temp_ctrl_switch_ctrl_temperature(0);
            if(off_result == MODBUS_OK) {
                output_off_confirmed = 1;
                printf("[temp] boot control disable: OK\r\n");
            } else if(last_init_log_ms == 0 ||
                      (uint32_t)(HAL_GetTick() - last_init_log_ms) >= 5000u) {
                last_init_log_ms = HAL_GetTick();
                printf("[temp] boot control disable FAILED: %s(%d), retrying\r\n",
                       modbus_result_name(off_result), off_result);
            }
        }
        if(xQueueReceive(s_commands, &command, 0) == pdPASS) {
            int32_t raw = temp_calibration_to_module(command.value);
            int set_result = MODBUS_ERR_PARAM;
            int control_result = MODBUS_ERR_PARAM;
            /* 缩窄为协议 INT16S 之前，先检查转换结果是否有效。 */
            int ok = initialized && raw >= -32768 && raw <= 32767;
            printf("[temp] setpoint request id=%lu liquid=%ld.%02ldC module_raw=%ld: %s\r\n",
                   (unsigned long)command.id, (long)(command.value / 100),
                   (long)((command.value < 0 ? -command.value : command.value) % 100),
                   (long)raw, ok ? "begin write" : "rejected (not initialized/range)");
            if(ok) {
                set_result = temp_ctrl_set_temperature((uint16_t)(int16_t)raw);
                ok = set_result == MODBUS_OK;
                printf("[temp] SV register write: %s(%d)\r\n",
                       modbus_result_name(set_result), set_result);
            }
            if(ok) {
                control_result = temp_ctrl_switch_ctrl_temperature(1);
                ok = control_result == MODBUS_OK;
                printf("[temp] closed-loop enable: %s(%d)\r\n",
                       modbus_result_name(control_result), control_result);
            }
            next.completed_id = command.id;
            next.write_ok = ok ? 1 : 0;
            if(ok) {
                next.applied_centi = command.value;
                next.target_valid = 1;
            } else {
                /* 后续开启控制确认失败前，温控器 SV 可能已经发生变化。 */
                next.target_valid = 0;
            }
            printf("[temp] setpoint request id=%lu result=%s\r\n",
                   (unsigned long)command.id, ok ? "APPLIED" : "FAILED");
        }
        {
            int pv_result = MODBUS_ERR_PARAM;
            int alarm_result = MODBUS_ERR_PARAM;
            if(initialized) {
                pv_result = temp_ctrl_read_temperature(&pv);
                if(pv_result == MODBUS_OK) alarm_result = temp_ctrl_read_alarm(&alarm);
                if(pv_result == MODBUS_OK && alarm_result == MODBUS_OK) good = 1;
            }
            if(!good && (last_poll_log_ms == 0 ||
                         (uint32_t)(HAL_GetTick() - last_poll_log_ms) >= 5000u)) {
                last_poll_log_ms = HAL_GetTick();
                if(pv_result != MODBUS_OK) {
                    printf("[temp] poll FAILED: init=%u PV=%s(%d) ALM=SKIPPED\r\n",
                           (unsigned)initialized, modbus_result_name(pv_result), pv_result);
                } else {
                    printf("[temp] poll FAILED: init=%u PV=OK ALM=%s(%d)\r\n",
                           (unsigned)initialized, modbus_result_name(alarm_result), alarm_result);
                }
                poll_failure_logged = 1;
            }
        }
        if(good) {
            if(poll_failure_logged) {
                printf("[temp] polling recovered: PV=%d alarm=0x%04X\r\n",
                       (int)(int16_t)pv, (unsigned)alarm);
                poll_failure_logged = 0;
            }
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

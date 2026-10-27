#include "app_service.h"
#include "cmsis_os2.h"
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "semphr.h"
#include <stdio.h>
#include <string.h>

/*
 * 应用服务模块：统一处理用户命令、温控通信、舱门动作、计时状态机和报警。
 * 只有服务任务修改应用状态，界面通过队列提交命令并读取快照。
 */

static QueueHandle_t s_commands;
static SemaphoreHandle_t s_lock;
static app_snapshot_t s_published;
static app_snapshot_t s_app; /* 只有应用服务任务可以修改此实例。 */
static uint32_t s_next_id, s_pending_id, s_write_started_ms;
static int32_t s_requested_target;
static uint8_t s_auto_write;
static uint8_t s_boot_target_requested;

/* 将服务命令和拒绝原因转换为简洁日志，便于串口追踪按键后的业务结果。 */
static const char *command_name(app_command_kind_t kind)
{
    switch(kind) {
    case APP_SET_TEMP: return "SET_TEMP";
    case APP_SET_TIME: return "SET_TIME";
    case APP_SET_DELAY: return "SET_DELAY";
    case APP_START: return "START";
    case APP_STOP: return "STOP";
    case APP_MUTE: return "MUTE";
    case APP_OPEN: return "OPEN_DOOR";
    case APP_CLOSE: return "CLOSE_DOOR";
    default: return "UNKNOWN";
    }
}

static const char *message_name(app_message_t message)
{
    switch(message) {
    case APP_MSG_NONE: return "ACCEPTED";
    case APP_MSG_BUSY: return "REJECTED_BUSY";
    case APP_MSG_SET_TEMP: return "REJECTED_SETPOINT_NOT_CONFIRMED";
    case APP_MSG_TEMP_OFFLINE: return "REJECTED_TEMP_OFFLINE_OR_ALARM";
    case APP_MSG_WRITE_FAILED: return "TEMP_WRITE_FAILED";
    case APP_MSG_DOOR_FAILED: return "DOOR_ACTION_FAILED";
    case APP_MSG_SETTING_LOCKED: return "REJECTED_SETTING_LOCKED";
    default: return "UNKNOWN_RESULT";
    }
}

static const char *phase_name(inc_phase_t phase)
{
    switch(phase) {
    case INC_IDLE: return "IDLE";
    case INC_RUNNING: return "RUNNING";
    case INC_FINISHED: return "FINISHED";
    case INC_DELAY: return "DELAY";
    case INC_COOL_REQUEST: return "COOL_REQUEST";
    case INC_COOLING: return "COOLING";
    case INC_STORAGE: return "STORAGE";
    case INC_ERROR: return "ERROR";
    default: return "UNKNOWN";
    }
}

__WEAK int app_board_buzzer_set(int enabled) { (void)enabled; return 0; }

/* 初始化应用状态、命令队列和状态锁。 */
int app_service_init(void)
{
    memset(&s_app, 0, sizeof(s_app));
    s_boot_target_requested = 0;
    incubator_model_init(&s_app.run);
    s_published = s_app;
    s_commands = xQueueCreate(8, sizeof(app_command_t));
    s_lock = xSemaphoreCreateMutex();
    return s_commands && s_lock;
}

/* 非阻塞提交一条应用命令；返回 1 表示入队成功。 */
int app_service_submit(app_command_kind_t kind, int32_t value)
{
    app_command_t c = {kind, value};
    int accepted = s_commands && xQueueSend(s_commands, &c, 0) == pdPASS;
    printf("[app] enqueue %s value=%ld: %s\r\n", command_name(kind),
           (long)value, accepted ? "OK" : "QUEUE_FULL_OR_NOT_READY");
    return accepted;
}

/* 在锁保护下复制最新状态，供界面和调试模块读取。 */
void app_service_snapshot(app_snapshot_t *out)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    *out = s_published;
    xSemaphoreGive(s_lock);
}

/* 发起一次异步温度设定，并记录请求编号以匹配完成结果。 */
static int request_target(int32_t target, int automatic, uint32_t now)
{
    if(s_app.target_pending) return 0;
    if(++s_next_id == 0) ++s_next_id;
    if(!temp_service_set_target(target, s_next_id)) return 0;
    s_pending_id = s_next_id;
    s_requested_target = target;
    s_auto_write = automatic ? 1 : 0;
    s_app.target_pending = 1;
    s_write_started_ms = now;
    return 1;
}

/* 取消运行并恢复孵育设定值，保证下一次设置时温控器处于正确目标温度。 */
/* 停止当前运行，并将温控模块恢复到孵育设定温度。 */
static void stop_and_restore(uint32_t now)
{
    incubator_model_stop(&s_app.run);
    if(!s_app.target_pending && s_app.temp.online && s_app.temp.valid &&
       s_app.temp.applied_centi != s_app.run.target_centi)
        request_target(s_app.run.target_centi, 0, now);
}

/* 执行一条命令，同时检查温度、舱门和运行状态互锁条件。 */
static void command(const app_command_t *c, uint32_t now, uint8_t *door_request)
{
    s_app.message = APP_MSG_NONE;
    switch(c->kind) {
    case APP_SET_TEMP:
        /* 运行中禁止修改设定值；本次运行必须使用启动时保存的孵育参数。 */
        if(incubator_model_active(&s_app.run) && s_app.run.phase != INC_STORAGE)
            s_app.message = APP_MSG_SETTING_LOCKED;
        else if(c->value < APP_TEMP_MIN_CENTI || c->value > APP_TEMP_MAX_CENTI)
            s_app.message = APP_MSG_SET_TEMP;
        else if(!request_target(c->value, 0, now)) s_app.message = APP_MSG_BUSY;
        else s_boot_target_requested = 1;
        break;
    case APP_SET_TIME:
        if(incubator_model_active(&s_app.run)) s_app.message = APP_MSG_SETTING_LOCKED;
        else if(c->value >= 0 && c->value <= (int32_t)APP_TIME_MAX_S)
            s_app.run.time_s = (uint32_t)c->value;
        break;
    case APP_SET_DELAY:
        if(c->value < 0 || c->value > (int32_t)APP_DELAY_MAX_S) break;
        if(s_app.run.phase == INC_RUNNING) {
            /* 延迟阶段尚未开始，因此孵育倒计时期间仍允许修改延迟时间。 */
            s_app.run.delay_s = (uint16_t)c->value;
            s_app.run.run_delay_ms = (uint32_t)c->value * 1000u;
            s_app.run.delay_remaining_ms = s_app.run.run_delay_ms;
        } else if(incubator_model_active(&s_app.run)) {
            s_app.message = APP_MSG_SETTING_LOCKED;
        } else {
            s_app.run.delay_s = (uint16_t)c->value;
        }
        break;
    case APP_STOP:
        if(incubator_model_active(&s_app.run) || s_app.run.phase == INC_FINISHED ||
           s_app.run.phase == INC_ERROR)
            stop_and_restore(now);
        break;
    case APP_START:
        /* 开始只启动计时；舱门动作状态不参与温控或计时启动判定。 */
        if(s_app.target_pending || incubator_model_active(&s_app.run))
            s_app.message = APP_MSG_BUSY;
        else if(!s_app.temp.online || !s_app.temp.valid || s_app.temp.alarm)
            s_app.message = APP_MSG_TEMP_OFFLINE;
        else if(!s_app.temp.target_valid || s_app.run.phase == INC_ERROR)
            s_app.message = APP_MSG_SET_TEMP;
        else incubator_model_start(&s_app.run, now);
        break;
    case APP_MUTE:
        incubator_model_mute(&s_app.run); /* No change to phase/deadlines. */
        break;
    case APP_OPEN:
    case APP_CLOSE:
        if(*door_request || s_app.door == MOTOR_INSTALLING ||
           s_app.door == MOTOR_OPENING || s_app.door == MOTOR_CLOSING) {
            s_app.message = APP_MSG_BUSY;
        } else if(s_app.door == MOTOR_UNINIT || s_app.door == MOTOR_FAULT || s_app.door_fault) {
            /* 上电校准未完成或舱门故障时，允许用户明确请求一次重新安装校准。 */
            if(motor_init() == MOTOR_RET_OK) *door_request = c->kind == APP_OPEN ? 1 : 2;
            else s_app.message = APP_MSG_DOOR_FAILED;
        } else if(s_app.door_fault ||
                  (c->kind == APP_OPEN ? motor_open(MOTOR_SPEED_2) : motor_close(MOTOR_SPEED_2)) != MOTOR_RET_OK)
            s_app.message = APP_MSG_DOOR_FAILED;
        break;
    }
    printf("[app] command %s value=%ld result=%s phase=%s door=%d "
           "temp_online=%u temp_valid=%u alarm=%u target_valid=%u "
           "target_pending=%u door_fault=%u\r\n",
           command_name(c->kind), (long)c->value, message_name(s_app.message),
           phase_name(s_app.run.phase), (int)s_app.door, (unsigned)s_app.temp.online,
           (unsigned)s_app.temp.valid, (unsigned)s_app.temp.alarm,
           (unsigned)s_app.temp.target_valid, (unsigned)s_app.target_pending,
           (unsigned)s_app.door_fault);
}

/* 应用服务主循环：轮询子服务、推进状态机并发布界面快照。 */
void app_service_task(void *argument)
{
    app_command_t c;
    uint8_t door_request = 0, previous_buzzer = 2;
    uint32_t door_request_ms = 0;
    uint8_t at_temp = 0;
    motor_state_t previous_door = MOTOR_UNINIT;
    (void)argument;
    for(;;) {
        uint32_t now = HAL_GetTick();
        int32_t diff;
        temp_service_snapshot(&s_app.temp);
        s_app.door = motor_get_state();
        /* 舱门打开表示取出样品，结束保存阶段。 */
        if(s_app.door == MOTOR_OPEN && previous_door != MOTOR_OPEN &&
           (s_app.run.phase == INC_STORAGE || s_app.run.phase == INC_FINISHED))
            stop_and_restore(now);
        previous_door = s_app.door;
        s_app.door_fault = s_app.door == MOTOR_FAULT ||
            ((s_app.door == MOTOR_OPEN || s_app.door == MOTOR_CLOSED) && motor_feedback_age_ms() > 2500u);
        if(s_app.target_pending && s_app.temp.completed_id == s_pending_id) {
            s_app.target_pending = 0;
            s_app.write_fault = !s_app.temp.write_ok;
            printf("[app] setpoint completion id=%lu result=%s target=%ld.%02ldC\r\n",
                   (unsigned long)s_pending_id, s_app.temp.write_ok ? "CONFIRMED" : "FAILED",
                   (long)(s_requested_target / 100),
                   (long)((s_requested_target < 0 ? -s_requested_target : s_requested_target) % 100));
            if(s_auto_write) {
                incubator_model_cooling_result(&s_app.run, s_app.temp.write_ok);
                /* 降温设定写入期间按下停止时，温控器可能已经保持 4℃，需要恢复孵育设定值。 */
                if(s_app.temp.write_ok && s_app.run.phase == INC_IDLE)
                    request_target(s_app.run.target_centi, 0, now);
            } else if(s_app.temp.write_ok) {
                s_app.run.target_centi = s_requested_target;
                s_app.run.phase = INC_IDLE;
            }
            if(!s_app.temp.write_ok) s_app.message = APP_MSG_WRITE_FAILED;
        }
        if(s_app.target_pending && (uint32_t)(now-s_write_started_ms) > 10000u) {
            /* 在事务完成前保持互锁，避免第一条未知结果的写入尚未到达时又发送第二条设定值。 */
            s_app.message = APP_MSG_WRITE_FAILED;
            s_app.write_fault = 1;
        }
        while(xQueueReceive(s_commands, &c, 0) == pdPASS) {
            uint8_t before = door_request;
            command(&c, now, &door_request);
            if(!before && door_request) door_request_ms = now;
        }
        if(!s_boot_target_requested && !s_app.target_pending &&
           s_app.temp.online && s_app.temp.valid && !s_app.temp.alarm) {
            if(request_target(s_app.run.target_centi, 0, now))
                s_boot_target_requested = 1;
        }
        if(door_request) {
            if(s_app.door == MOTOR_OPEN || s_app.door == MOTOR_CLOSED) {
                motor_ret_t ret = door_request == 1 ? motor_open(MOTOR_SPEED_2) : motor_close(MOTOR_SPEED_2);
                if(ret == MOTOR_RET_OK) door_request = 0;
            }
            if((uint32_t)(now-door_request_ms) > 18000u) {
                door_request = 0;
                s_app.message = APP_MSG_DOOR_FAILED;
            }
        }
        /* 温度通信丢失时冻结倒计时；恢复后继续，暂停期间不扣除孵育时间。 */
        s_app.alarm = !s_app.temp.online || !s_app.temp.valid ||
                      s_app.temp.alarm || s_app.door_fault || s_app.write_fault;
        incubator_model_set_paused(&s_app.run, s_app.alarm, now);
        incubator_model_step(&s_app.run, now);
        if(s_app.run.cooling_request) {
            if(request_target(400, 1, now)) s_app.run.cooling_request = 0;
        }
        diff = s_app.temp.pv_centi - s_app.temp.applied_centi;
        if(diff < 0) diff = -diff;
        if(!s_app.temp.valid || !s_app.temp.target_valid || s_app.target_pending) at_temp = 0;
        else if(diff <= 30) at_temp = 1; /* 误差达到 ±0.3℃ 内进入达标。 */
        else if(diff > 50) at_temp = 0;  /* 超过 ±0.5℃ 退出达标，避免状态闪烁。 */
        s_app.at_temperature = at_temp;
        incubator_model_at_storage(&s_app.run, at_temp && s_app.temp.applied_centi == 400);
        if(previous_buzzer != s_app.run.buzzer) {
            s_app.buzzer_hardware = app_board_buzzer_set(s_app.run.buzzer) ? 1 : 0;
            previous_buzzer = s_app.run.buzzer;
        }
        xSemaphoreTake(s_lock, portMAX_DELAY);
        s_published = s_app;
        xSemaphoreGive(s_lock);
        osDelay(20);
    }
}

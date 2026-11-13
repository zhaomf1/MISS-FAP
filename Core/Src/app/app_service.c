#include "app_service.h"
#include "bsp_board_io.h"
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

/* 设定值写入失败后的自动重试: 每 WRITE_RETRY_PERIOD_MS 重发一次,
 * 最多 WRITE_RETRY_MAX 次; 用尽后解除 write_fault, 避免一次失败让报警永久锁存。 */
#define WRITE_RETRY_PERIOD_MS   10000u
#define WRITE_RETRY_MAX         5u

static uint8_t  s_write_retries;
static uint32_t s_write_retry_ms;
static uint8_t  s_last_alarm;   /* 报警边沿日志用 */

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

/* ==================== 温控风扇 (OUTPUT_1 = PB1) ==================== */
/*
 * 风扇为 12V 输出, 高电平有效。跟随温控运行状态动作:
 *   开启温控(START, 阶段 RUNNING/DELAY/COOL_REQUEST/COOLING/STORAGE)
 *       -> 立即拉高 PB1;
 *   关闭温控(STOP/超时结束/故障, 阶段 IDLE/ERROR)
 *       -> 保持运转 FAN_COOLDOWN_MS(1 分钟) 排出余热, 到期后拉低 PB1。
 * 1 分钟窗口内重新开启温控, 则取消待关闭计时, 风扇不断电。
 */
#define FAN_COOLDOWN_MS   60000u
#define FAN_LOG_PERIOD_MS 10000u   /* 风扇运行期间的状态心跳周期(排查后可调大/关闭) */

static uint8_t  s_fan_on;          /* 风扇当前输出状态 */
static uint8_t  s_fan_off_pending; /* 已排队等待延时关闭 */
static uint32_t s_fan_off_ms;      /* 计划关闭时刻 (HAL_GetTick 时基) */
static uint8_t  s_fan_last_active; /* 上一次温控运行状态, 用于边沿日志 */
static uint32_t s_fan_log_ms;      /* 上次风扇心跳日志时刻 */

/* 只在状态变化时真正操作引脚, 避免每 20ms 重复写 GPIO */
static void fan_apply(uint8_t on)
{
    if(s_fan_on == on) return;
    bsp_board_output_set(BSP_OUT_FAN, on ? 1 : 0);
    s_fan_on = on;
    printf("[fan] %s\r\n", on ? "ON" : "OFF");
}

/* 温控运行中: 立即开风扇并取消可能存在的延时关闭 */
static void fan_start(void)
{
    s_fan_off_pending = 0;
    fan_apply(1);
}

/* 温控已关闭: 启动一次 1 分钟延时关闭 */
static void fan_request_stop(uint32_t now)
{
    if(!s_fan_on || s_fan_off_pending) return;
    s_fan_off_pending = 1;
    s_fan_off_ms = now + FAN_COOLDOWN_MS;
    printf("[fan] cooldown start, stop in %lus\r\n",
           (unsigned long)(FAN_COOLDOWN_MS / 1000u));
}

/* 延时到期后关闭风扇; 由应用服务任务周期调用 */
static void fan_poll(uint32_t now)
{
    if(!s_fan_off_pending) return;
    if((int32_t)(now - s_fan_off_ms) < 0) return;
    s_fan_off_pending = 0;
    fan_apply(0);
}

/* 风扇诊断日志:
 *   1) 温控运行状态发生边沿时打一行, 说明"为什么"开/关;
 *   2) 风扇通电期间每 FAN_LOG_PERIOD_MS 打一行, 含 PB1 实际引脚回读。
 * 若逻辑已 ON 但 pin_read=0, 说明引脚/配置/后级硬件问题, 而非业务逻辑问题。 */
static void fan_trace(uint32_t now, uint8_t active)
{
    if(active != s_fan_last_active) {
        s_fan_last_active = active;
        printf("[fan] temp-control=%s phase=%s ctrl_on=%u -> fan=%s pending=%u\r\n",
               active ? "ON" : "OFF", phase_name(s_app.run.phase),
               (unsigned)s_app.temp.ctrl_on,
               s_fan_on ? "ON" : "OFF", (unsigned)s_fan_off_pending);
    }
    if(!s_fan_on) return;
    if((uint32_t)(now - s_fan_log_ms) < FAN_LOG_PERIOD_MS) return;
    s_fan_log_ms = now;
    printf("[fan] state=ON pin_read=%d pending=%u remain=%lums phase=%s\r\n",
           bsp_board_output_get(BSP_OUT_FAN) ? 1 : 0,
           (unsigned)s_fan_off_pending,
           (unsigned long)(s_fan_off_pending ? (s_fan_off_ms - now) : 0u),
           phase_name(s_app.run.phase));
}

/* 初始化应用状态、命令队列和状态锁。 */
int app_service_init(void)
{
    memset(&s_app, 0, sizeof(s_app));
    incubator_model_init(&s_app.run);
    s_published = s_app;
    s_commands = xQueueCreate(8, sizeof(app_command_t));
    s_lock = xSemaphoreCreateMutex();
    /* 上电即确认风扇引脚处于关闭状态, 便于对照后续 [fan] 日志 */
    bsp_board_output_set(BSP_OUT_FAN, 0);
    printf("[fan] init OFF pin_read=%d cooldown=%lums\r\n",
           bsp_board_output_get(BSP_OUT_FAN) ? 1 : 0,
           (unsigned long)(FAN_COOLDOWN_MS / 1000u));
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

/* 执行一条命令，同时检查温度与运行状态互锁条件。 */
static void command(const app_command_t *c, uint32_t now)
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
        break;
    case APP_SET_TIME:
        if(incubator_model_active(&s_app.run)) s_app.message = APP_MSG_SETTING_LOCKED;
        else if(c->value >= (int32_t)APP_TIME_MIN_S && c->value <= (int32_t)APP_TIME_MAX_S)
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
        else if(!s_app.at_temperature)
            s_app.message = APP_MSG_BUSY;
        else incubator_model_start(&s_app.run, now);
        break;
    case APP_MUTE:
        incubator_model_mute(&s_app.run); /* No change to phase/deadlines. */
        break;
    case APP_OPEN:
    case APP_CLOSE:
        /* 开关盖就是一条指令: 直接下发, 结果只用于提示"忙", 不产生故障告警 */
        if((c->kind == APP_OPEN ? motor_open(MOTOR_SPEED_1)
                                : motor_close(MOTOR_SPEED_1)) != MOTOR_RET_OK)
            s_app.message = APP_MSG_BUSY;
        break;
    }
    printf("[app] command %s value=%ld result=%s phase=%s door=%d "
           "temp_online=%u temp_valid=%u alarm=%u target_valid=%u "
           "target_pending=%u\r\n",
           command_name(c->kind), (long)c->value, message_name(s_app.message),
           phase_name(s_app.run.phase), (int)s_app.door, (unsigned)s_app.temp.online,
           (unsigned)s_app.temp.valid, (unsigned)s_app.temp.alarm,
           (unsigned)s_app.temp.target_valid, (unsigned)s_app.target_pending);
}

/* 应用服务主循环：轮询子服务、推进状态机并发布界面快照。 */
void app_service_task(void *argument)
{
    app_command_t c;
    uint8_t previous_buzzer = 2;
    uint8_t at_temp = 0;
    (void)argument;
    for(;;) {
        uint32_t now = HAL_GetTick();
        int32_t diff;
        temp_service_snapshot(&s_app.temp);
        s_app.door = motor_get_state();
        if(s_app.target_pending && s_app.temp.completed_id == s_pending_id) {
            s_app.target_pending = 0;
            s_app.write_fault = !s_app.temp.write_ok;
            /* 失败则安排自动重试, 成功则清零重试计数 */
            if(s_app.write_fault) {
                s_write_retries = 0;
                s_write_retry_ms = now + WRITE_RETRY_PERIOD_MS;
            } else {
                s_write_retries = 0;
            }
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
            if(!s_app.write_fault) {
                s_app.write_fault = 1;
                s_write_retries = 0;
                s_write_retry_ms = now + WRITE_RETRY_PERIOD_MS;
            }
        }
        /* 写入失败自动重试: 通信正常、温控器无报警且没有挂起事务时, 每 10s 重发一次。
         * 重试用尽仍失败 -> 解除 write_fault, 只保留本次提示, 不再让报警永久挂住。 */
        if(!s_app.temp.online) {
            /* 离线本身已由 temp_alarm 报警, 不再保留写入故障, 恢复后重新按需写入 */
            s_app.write_fault = 0;
            s_write_retries = 0;
        } else if(s_app.write_fault && !s_app.target_pending && !s_app.temp.alarm &&
                  (int32_t)(now - s_write_retry_ms) >= 0) {
            if(s_write_retries >= WRITE_RETRY_MAX) {
                s_app.write_fault = 0;
                printf("[app] write_fault retries exhausted (%u), cleared\r\n",
                       (unsigned)WRITE_RETRY_MAX);
            } else if(request_target(s_requested_target, s_auto_write, now)) {
                s_write_retries++;
                printf("[app] write_fault retry %u/%u target=%ld\r\n",
                       (unsigned)s_write_retries, (unsigned)WRITE_RETRY_MAX,
                       (long)s_requested_target);
                s_write_retry_ms = now + WRITE_RETRY_PERIOD_MS;
            }
        }
        while(xQueueReceive(s_commands, &c, 0) == pdPASS) {
            command(&c, now);
        }
        /* 报警只来源于温控链路; 舱门开/关不产生任何告警, 也不参与业务互锁。 */
        s_app.temp_alarm = !s_app.temp.online || !s_app.temp.valid ||
                           s_app.temp.alarm || s_app.write_fault;
        s_app.alarm = s_app.temp_alarm;
        if(s_app.alarm != s_last_alarm) {
            s_last_alarm = s_app.alarm;
            printf("[app] alarm %u->%u temp=%u write=%u online=%u valid=%u dev=0x%04X\r\n",
                   (unsigned)!s_app.alarm, (unsigned)s_app.alarm,
                   (unsigned)s_app.temp_alarm, (unsigned)s_app.write_fault,
                   (unsigned)s_app.temp.online, (unsigned)s_app.temp.valid,
                   (unsigned)s_app.temp.alarm);
        }
        /* 只有温控报警才冻结孵育倒计时; 开关盖只是独立动作, 不参与计时与流程。 */
        incubator_model_set_paused(&s_app.run, s_app.temp_alarm, now);
        incubator_model_step(&s_app.run, now);
        /* 风扇跟随温控: 温控器闭环开启(或孵育运行中)就常开;
         * 两者都关闭后延时 1 分钟关闭。 */
        {
            uint8_t fan_active = (s_app.temp.ctrl_on || incubator_model_active(&s_app.run)) ? 1u : 0u;
            if(fan_active) fan_start();
            else fan_request_stop(now);
            fan_poll(now);
            fan_trace(now, fan_active);
        }
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
